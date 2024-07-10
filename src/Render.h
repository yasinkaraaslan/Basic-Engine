#pragma once
#include "Mesh.h"
struct SceneData 
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::mat4 viewproj;
	glm::vec4 ambientColor;
	glm::vec4 sunlightDirection; // w for sun power
	glm::vec4 sunlightColor;
};


struct RenderObject 
{
	uint32_t indexCount;
	uint32_t firstIndex;
	VkBuffer indexBuffer;

	MaterialInstance* material;
    Bounds bounds;
	glm::mat4 transform;
	VkDeviceAddress vertexBufferAddress;
};

struct DrawContext 
{
	std::vector<RenderObject> opaqueSurfaces;
    std::vector<RenderObject> transparentSurfaces;
};

class IRenderable 
{

	virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

class Node : public IRenderable
{
public:
    typedef std::shared_ptr<Node> Ptr;
    Ptr GetParent() { return _parent.lock(); };
    void SetParent(Ptr node) { _parent = node; };

    std::vector<Ptr>& GetChildren() { return _children; };
    Ptr GetChild(size_t index) { return _children.at(index); };

    void AddChild(Ptr node) { _children.push_back(node); };
    glm::mat4& GetLocalTransform() { return _localTransform; };
    glm::mat4& GetWorldTransform() { return _worldTransform; };

    void SetLocalTransform(const glm::mat4& transform);
    void SetWorldTransform(const glm::mat4& transform);

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        for (auto& c : _children) {
            c->Draw(topMatrix, ctx);
        }
    }
    void RefreshWorldTransform(const glm::mat4& parentMatrix);
private:

    // parent pointer must be a weak pointer to avoid circular dependencies
    std::weak_ptr<Node> _parent;
    std::vector<Ptr> _children;

    glm::mat4 _localTransform;
    glm::mat4 _worldTransform;


    
};
class MeshNode : public Node 
{
public:
    std::shared_ptr<MeshAsset> GetMesh() { return _mesh; };
    void SetMesh(const std::shared_ptr<MeshAsset> mesh) { _mesh = mesh; };
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;

private:
    std::shared_ptr<MeshAsset> _mesh;

};

class LoadedGLTF : public IRenderable
{
public:
    static std::optional<std::shared_ptr<LoadedGLTF>> Load(std::string_view filePath);
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);
    ~LoadedGLTF() { ClearAll(); };
private:
    void ClearAll();
    std::vector<VkSampler> _samplers;
    DescriptorAllocatorGrowable _descriptorPool;

    std::unordered_map<std::string, std::shared_ptr<MeshAsset>> _meshes;
    std::unordered_map<std::string, Node::Ptr> _nodes;
    std::unordered_map<std::string, AllocatedImage> _images;
    std::unordered_map<std::string, std::shared_ptr<Material>> _materials;

    // nodes that dont have a parent, for iterating through the file in tree order

    std::vector<Node::Ptr> _topNodes;

    AllocatedBuffer _materialDataBuffer;
};