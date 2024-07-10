#pragma once
#include "Descriptors.h"
enum class MaterialPass :uint8_t {
	MainColor,
	Transparent,
	Other
};

struct MaterialPipeline {
	VkPipeline pipeline;
	VkPipelineLayout layout;
};

struct MaterialInstance {
	MaterialPipeline* pipeline;
	VkDescriptorSet materialSet;
	MaterialPass passType;
};

struct Material {
	MaterialInstance data;
};

struct MetallicRougness {
	MaterialPipeline opaquePipeline;
	MaterialPipeline transparentPipeline;

	VkDescriptorSetLayout materialLayout;

	struct MaterialConstants {
		glm::vec4 colorFactors;
		glm::vec4 metalRoughFactors;
		//padding, we need it anyway for uniform buffers
		glm::vec4 extra[14];
	};

	struct MaterialResources {
		AllocatedImage colorImage;
		VkSampler colorSampler;
		AllocatedImage metalRoughImage;
		VkSampler metalRoughSampler;
		VkBuffer dataBuffer;
		uint32_t dataBufferOffset;
	};

	DescriptorWriter writer;

	void BuildPipelines();
	void CleanResources();

	MaterialInstance WriteMaterial(MaterialPass pass, const MaterialResources& resources, DescriptorAllocatorGrowable& descriptorAllocator);
};
