#include "Render.h"
#include "Engine.h"
#include "Images.h"

#include <glm/gtx/matrix_decompose.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/gtx/quaternion.hpp>

VkFilter ExtractFilter(fastgltf::Filter filter)
{
    switch (filter) {
        // nearest samplers
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;

        // linear samplers
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode ExtractMipmapMode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;

    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

void Node::SetLocalTransform(const glm::mat4& transform)
{
    _localTransform = transform;
    RefreshWorldTransform(_worldTransform);
}

void Node::SetWorldTransform(const glm::mat4& transform)
{
	glm::mat4 difference = transform * glm::inverse(_worldTransform);
	_worldTransform = transform;
	_localTransform = difference * _localTransform;
}

void Node::RefreshWorldTransform(const glm::mat4& parentMatrix)
{
    _worldTransform = parentMatrix * _localTransform;
    for (auto c : _children) {
        c->RefreshWorldTransform(_worldTransform);
    }
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
	glm::mat4 nodeMatrix = topMatrix * GetWorldTransform();

	for (auto& s : _mesh->surfaces) {
		RenderObject def;
		def.indexCount = s.count;
		def.firstIndex = s.startIndex;
		def.indexBuffer =_mesh->meshBuffers.indexBuffer.buffer;
		def.material = &s.material->data;
        def.bounds = s.bounds;
		def.transform = nodeMatrix;
		def.vertexBufferAddress = _mesh->meshBuffers.vertexBufferAddress;

        if (s.material->data.passType == MaterialPass::Transparent)
            ctx.transparentSurfaces.push_back(def);
        else
            ctx.opaqueSurfaces.push_back(def);
	}

	Node::Draw(topMatrix, ctx);
}

std::optional<std::shared_ptr<LoadedGLTF>> LoadedGLTF::Load(std::string_view filePath)
{
    fmt::println("Loading GLTF: {}", filePath);
    Engine* engine = Engine::Get();
    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    LoadedGLTF& file = *scene.get();

    fastgltf::Parser parser{};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;
    // fastgltf::Options::LoadExternalImages;

    fastgltf::GltfDataBuffer data;
    data.loadFromFile(filePath);

    fastgltf::Asset gltf;

    std::filesystem::path path = filePath;

    auto type = fastgltf::determineGltfFileType(&data);
    if (type == fastgltf::GltfType::glTF)
    {
        auto load = parser.loadGltf(&data, path.parent_path(), gltfOptions);
        if (load)
            gltf = std::move(load.get());
        else
        {
            fmt::println("Failed to load glTF: {}", fastgltf::to_underlying(load.error()));
            return {};
        }
    }
    else if (type == fastgltf::GltfType::GLB)
    {
        auto load = parser.loadGltfBinary(&data, path.parent_path(), gltfOptions);
        if (load)
            gltf = std::move(load.get());
        else
        {
            fmt::println("Failed to load glTF: {}", fastgltf::to_underlying(load.error()));
            return {};
        }
    }
    else
    {
        fmt::println("Failed to determine glTF container");
        return {};
    }
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } };

    file._descriptorPool.Init(gltf.materials.size(), sizes);

    for (fastgltf::Sampler& sampler : gltf.samplers) {

        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = ExtractFilter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = ExtractFilter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampl.mipmapMode = ExtractMipmapMode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        VkSampler newSampler;
        vkCreateSampler(engine->GetDevice(), &sampl, nullptr, &newSampler);

        file._samplers.push_back(newSampler);
    }
    std::vector<std::shared_ptr<MeshAsset>> meshes;
    std::vector<Node::Ptr> nodes;
    std::vector<AllocatedImage> images;
    std::vector<std::shared_ptr<Material>> materials;

    for (fastgltf::Image& image : gltf.images)
    {
        std::optional<AllocatedImage> img = Util::LoadImage(gltf, image);

        if (img.has_value()) {
            images.push_back(*img);
            file._images[image.name.c_str()] = *img;
        }
        else 
        {
            // we failed to load, so lets give the slot a default white texture to not
            // completely break loading
            images.push_back(engine->GetErrorImage());
            fmt::println("glTF failed to load texture: {}", image.name);
        }
    }
    
    file._materialDataBuffer = engine->CreateBuffer(sizeof(MetallicRougness::MaterialConstants) * gltf.materials.size(),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    unsigned int dataIndex = 0;
    MetallicRougness::MaterialConstants* sceneMaterialConstants = (MetallicRougness::MaterialConstants*)file._materialDataBuffer.info.pMappedData;

    for (fastgltf::Material& mat : gltf.materials) {
        std::shared_ptr<Material> newMat = std::make_shared<Material>();
        materials.push_back(newMat);
        file._materials[mat.name.c_str()] = newMat;

        MetallicRougness::MaterialConstants constants;
        constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

        constants.metalRoughFactors.x = mat.pbrData.metallicFactor;
        constants.metalRoughFactors.y = mat.pbrData.roughnessFactor;
        // write material parameters to buffer
        sceneMaterialConstants[dataIndex] = constants;

        MaterialPass passType = MaterialPass::MainColor;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            passType = MaterialPass::Transparent;
        }

        MetallicRougness::MaterialResources materialResources;
        // default the material textures
        materialResources.colorImage = engine->GetWhiteImage();
        materialResources.colorSampler = engine->GetSamplerLinear();
        materialResources.metalRoughImage = engine->GetWhiteImage();
        materialResources.metalRoughSampler = engine->GetSamplerLinear();

        // set the uniform buffer for the material data
        materialResources.dataBuffer = file._materialDataBuffer.buffer;
        materialResources.dataBufferOffset = dataIndex * sizeof(MetallicRougness::MaterialConstants);
        // grab textures from gltf file
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

            materialResources.colorImage = images[img];
            materialResources.colorSampler = file._samplers[sampler];
        }
        // build material
        newMat->data = engine->GetMetalMaterial().WriteMaterial(passType, materialResources, file._descriptorPool);

        dataIndex++;
    }

    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        std::shared_ptr<MeshAsset> newMesh = std::make_shared<MeshAsset>();
        meshes.push_back(newMesh);
        file._meshes[mesh.name.c_str()] = newMesh;
        newMesh->name = mesh.name;

        indices.clear();
        vertices.clear();

        for (auto&& p : mesh.primitives) {
            GeoSurface newSurface;
            newSurface.startIndex = (uint32_t)indices.size();
            newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

            size_t initialVtx = vertices.size();

            {
                fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexaccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + initialVtx);
                    });
            }

            {
                fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                vertices.resize(vertices.size() + posAccessor.count);
                typedef glm::vec3 vec;
                fastgltf::iterateAccessorWithIndex <glm::vec3> (gltf, posAccessor,
                    [&](glm::vec3 v, size_t index) {
                        Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 1, 0, 0 };
                        newvtx.color = glm::vec4{ 1.f };
                        newvtx.uv_x = 0;
                        newvtx.uv_y = 0;
                        vertices[initialVtx + index] = newvtx;
                    });
            }

            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
                    [&](glm::vec3 v, size_t index) {
                        vertices[initialVtx + index].normal = v;
                    });
            }

            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
                    [&](glm::vec2 v, size_t index) {
                        vertices[initialVtx + index].uv_x = v.x;
                        vertices[initialVtx + index].uv_y = v.y;
                    });
            }

            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
                    [&](glm::vec4 v, size_t index) {
                        vertices[initialVtx + index].color = v;
                    });
            }
            newSurface.material = p.materialIndex.has_value() ? materials[p.materialIndex.value()] : materials[0];

            glm::vec3 minPos = vertices[initialVtx].position;
            glm::vec3 maxPos = vertices[initialVtx].position;
            for (int i = initialVtx; i < vertices.size(); i++) {
                minPos = glm::min(minPos, vertices[i].position);
                maxPos = glm::max(maxPos, vertices[i].position);
            }
            newSurface.bounds.origin = (maxPos + minPos) / 2.f;
            newSurface.bounds.extents = (maxPos - minPos) / 2.f;
            newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);

            newMesh->surfaces.push_back(newSurface);
        }

        newMesh->meshBuffers = engine->UploadMesh(indices, vertices);
    }

    for (fastgltf::Node& node : gltf.nodes) {
        std::shared_ptr<Node> newNode;

        if (node.meshIndex.has_value()) {
            newNode = std::make_shared<MeshNode>();
            static_cast<MeshNode*>(newNode.get())->SetMesh(meshes[*node.meshIndex]);
        }
        else {
            newNode = std::make_shared<Node>();
        }

        nodes.push_back(newNode);
        file._nodes[node.name.c_str()];

        std::visit(fastgltf::visitor{ [&](fastgltf::Node::TransformMatrix matrix) {
                                          memcpy(&newNode->GetLocalTransform(), matrix.data(), sizeof(matrix));
                                      },
                       [&](fastgltf::TRS transform) {
                           glm::vec3 tl(transform.translation[0], transform.translation[1],
                               transform.translation[2]);
                           glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1],
                               transform.rotation[2]);
                           glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

                           glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
                           glm::mat4 rm = glm::toMat4(rot);
                           glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

                           newNode->SetLocalTransform( tm * rm * sm);
                       } },
            node.transform);
    }

    for (int i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node& node = gltf.nodes[i];
        Node::Ptr& sceneNode = nodes[i];

        for (auto& c : node.children) {
            sceneNode->AddChild(nodes[c]);
            nodes[c]->SetParent(sceneNode);
        }
    }

    // find the top nodes, with no parents
    for (auto& node : nodes) {
        if (node->GetParent() == nullptr) {
            file._topNodes.push_back(node);
            node->RefreshWorldTransform(glm::mat4{1.f});
        }
    }

    return scene;
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    for (auto n : _topNodes)
    {
        n->Draw(topMatrix, ctx);
    }
}

void LoadedGLTF::ClearAll()
{
    Engine* engine = Engine::Get();

    _descriptorPool.DestroyPools();
    engine->DestroyBuffer(_materialDataBuffer);

    for (auto& [k, v] : _meshes) {

        engine->DestroyBuffer(v->meshBuffers.indexBuffer);
        engine->DestroyBuffer(v->meshBuffers.vertexBuffer);
    }

    for (auto& [k, v] : _images) {

        if (v.image == Engine::Get()->GetErrorImage().image) {
            //dont destroy the default images
            continue;
        }
        engine->DestroyImage(v);
    }

    for (auto& sampler : _samplers) {
        vkDestroySampler(engine->GetDevice(), sampler, nullptr);
    }
}

