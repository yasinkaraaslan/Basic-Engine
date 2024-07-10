#pragma once
#include "Types.h"

namespace Util 
{
	constexpr const char* shaderPath = "../shaders/";
	VkShaderModule LoadShader(const char* filePath);
}

class PipelineBuilder
{
public:
	std::vector<VkPipelineShaderStageCreateInfo> shaderStages;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly;
	VkPipelineRasterizationStateCreateInfo rasterizer;
	VkPipelineColorBlendAttachmentState colorBlendAttachment;
	VkPipelineMultisampleStateCreateInfo multisampling;
	VkPipelineLayout pipelineLayout;
	VkPipelineDepthStencilStateCreateInfo depthStencil;
	VkPipelineRenderingCreateInfo renderInfo;
	VkFormat colorAttachmentFormat;

	PipelineBuilder() { Clear(); };
	void Clear();
	VkPipeline BuildPipeline(VkDevice device);

	void SetShaders(VkShaderModule vertexShader, VkShaderModule fragmentShader);
	void SetInputTopology(VkPrimitiveTopology topology);
	void SetPolygonMode(VkPolygonMode mode);
	void SetCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
	void SetMultisamplingNone();
	void SetColorAttachmentFormat(VkFormat format);
	void SetDepthFormat(VkFormat format);
	void DisableBlending();
	void DisableDepthTest();
	void EnableDepthTest(bool depthWriteEnable, VkCompareOp op);
	void EnableBlendingAdditive();
	void EnableBlendingAlphaBlend();
private:
};