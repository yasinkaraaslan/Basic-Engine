#include "Materials.h"
#include "Pipelines.h"
#include "Engine.h"
#include "Initializers.h"

void MetallicRougness::BuildPipelines()
{
	Engine* engine = Engine::Get();
	VkDevice device = engine->GetDevice();
	VkShaderModule meshFragShader = Util::LoadShader("mesh.frag.spv");
	VkShaderModule meshVertexShader = Util::LoadShader("mesh.vert.spv");

	VkPushConstantRange matrixRange{};
	matrixRange.offset = 0;
	matrixRange.size = sizeof(DrawPushConstants);
	matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	DescriptorLayoutBuilder layoutBuilder;
	layoutBuilder.AddBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	layoutBuilder.AddBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	layoutBuilder.AddBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	materialLayout = layoutBuilder.Build(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

	VkDescriptorSetLayout layouts[] = { engine->GetSceneDataLayout(),
		materialLayout };

	VkPipelineLayoutCreateInfo meshLayoutInfo = Init::PipelineLayoutCreateInfo();
	meshLayoutInfo.setLayoutCount = 2;
	meshLayoutInfo.pSetLayouts = layouts;
	meshLayoutInfo.pPushConstantRanges = &matrixRange;
	meshLayoutInfo.pushConstantRangeCount = 1;

	VkPipelineLayout newLayout;
	VK_CHECK(vkCreatePipelineLayout(device, &meshLayoutInfo, nullptr, &newLayout));

	opaquePipeline.layout = newLayout;
	transparentPipeline.layout = newLayout;

	// build the stage-create-info for both vertex and fragment stages. This lets
	// the pipeline know the shader modules per stage
	PipelineBuilder pipelineBuilder;
	pipelineBuilder.SetShaders(meshVertexShader, meshFragShader);
	pipelineBuilder.SetInputTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
	pipelineBuilder.SetPolygonMode(VK_POLYGON_MODE_FILL);
	pipelineBuilder.SetCullMode(VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE);
	pipelineBuilder.SetMultisamplingNone();
	pipelineBuilder.DisableBlending();
	pipelineBuilder.EnableDepthTest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

	//render format
	pipelineBuilder.SetColorAttachmentFormat(engine->GetDrawImage().imageFormat);
	pipelineBuilder.SetDepthFormat(engine->GetDepthImage().imageFormat);

	// use the triangle layout we created
	pipelineBuilder.pipelineLayout = newLayout;

	// finally build the pipeline
	opaquePipeline.pipeline = pipelineBuilder.BuildPipeline(device);

	// create the transparent variant
	pipelineBuilder.EnableBlendingAdditive();

	pipelineBuilder.EnableDepthTest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

	transparentPipeline.pipeline = pipelineBuilder.BuildPipeline(device);

	vkDestroyShaderModule(device, meshFragShader, nullptr);
	vkDestroyShaderModule(device, meshVertexShader, nullptr);
}

void MetallicRougness::CleanResources()
{
	const VkDevice device = Engine::GetMainDevice();
	vkDestroyDescriptorSetLayout(device, materialLayout, nullptr);
	vkDestroyPipelineLayout(device, opaquePipeline.layout, nullptr); // same layout as the transparent pipeline
	vkDestroyPipeline(device, opaquePipeline.pipeline, nullptr);
	vkDestroyPipeline(device, transparentPipeline.pipeline, nullptr);
}

MaterialInstance MetallicRougness::WriteMaterial(MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator)
{
	MaterialInstance matData;
	matData.passType = pass;
	if (pass == MaterialPass::Transparent) {
		matData.pipeline = &transparentPipeline;
	}
	else {
		matData.pipeline = &opaquePipeline;
	}

	matData.materialSet = descriptorAllocator.Allocate(materialLayout);


	writer.Clear();
	writer.WriteBuffer(0, resources.dataBuffer, sizeof(MaterialConstants), resources.dataBufferOffset, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
	writer.WriteImage(1, resources.colorImage.imageView, resources.colorSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
	writer.WriteImage(2, resources.metalRoughImage.imageView, resources.metalRoughSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

	writer.UpdateSet(matData.materialSet);

	return matData;
}
