#pragma once
#include "Types.h"
struct DescriptorLayoutBuilder {

	std::vector<VkDescriptorSetLayoutBinding> bindings;

	void AddBinding(uint32_t binding, VkDescriptorType type);
	void Clear();
	VkDescriptorSetLayout Build(VkShaderStageFlags shaderStages, void* pNext = nullptr, VkDescriptorSetLayoutCreateFlags flags = 0);
};

struct DescriptorAllocator {

	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	VkDescriptorPool pool;

	void InitPool(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios);
	void ClearDescriptors();
	void DestroyPool();

	VkDescriptorSet Allocate(VkDescriptorSetLayout layout);
};

struct DescriptorAllocatorGrowable {
public:
	struct PoolSizeRatio {
		VkDescriptorType type;
		float ratio;
	};

	void Init(uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
	void ClearPools();
	void DestroyPools();

	VkDescriptorSet Allocate(VkDescriptorSetLayout layout, void* pNext = nullptr);
private:
	VkDescriptorPool GetPool();
	VkDescriptorPool CreatePool(uint32_t setCount, std::span<PoolSizeRatio> poolRatios);

	std::vector<PoolSizeRatio> ratios;
	std::vector<VkDescriptorPool> fullPools;
	std::vector<VkDescriptorPool> readyPools;
	uint32_t setsPerPool;

};

struct DescriptorWriter {
	std::deque<VkDescriptorImageInfo> imageInfos;
	std::deque<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkWriteDescriptorSet> writes;

	void WriteImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type);
	void WriteBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type);

	void Clear();
	void UpdateSet(VkDescriptorSet set);
};