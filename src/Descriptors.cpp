#include "Descriptors.h"
#include "Engine.h"


void DescriptorLayoutBuilder::AddBinding(uint32_t binding, VkDescriptorType type)
{
	VkDescriptorSetLayoutBinding newBind{};
	newBind.binding = binding;
	newBind.descriptorCount = 1;
	newBind.descriptorType = type;

	bindings.push_back(newBind);
}

void DescriptorLayoutBuilder::Clear()
{
	bindings.clear();
}

VkDescriptorSetLayout DescriptorLayoutBuilder::Build(VkShaderStageFlags shaderStages, void* pNext, VkDescriptorSetLayoutCreateFlags flags)
{
	for (auto& b : bindings)
	{
		b.stageFlags |= shaderStages;
	}
	VkDescriptorSetLayoutCreateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	info.pNext = pNext;
	info.pBindings = bindings.data();
	info.bindingCount = (uint32_t)bindings.size();
	info.flags = flags;

	VkDescriptorSetLayout set;

	VK_CHECK(vkCreateDescriptorSetLayout(Engine::GetMainDevice(), &info, nullptr, &set));

	return set;
}

void DescriptorAllocator::InitPool(uint32_t maxSets, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = uint32_t(ratio.ratio * maxSets)
			});
	}

	VkDescriptorPoolCreateInfo poolInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	poolInfo.flags = 0;
	poolInfo.maxSets = maxSets;
	poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();

	vkCreateDescriptorPool(Engine::GetMainDevice(), &poolInfo, nullptr, &pool);
}

void DescriptorAllocator::ClearDescriptors()
{
	vkResetDescriptorPool(Engine::GetMainDevice(), pool, 0);
}

void DescriptorAllocator::DestroyPool()
{
	vkDestroyDescriptorPool(Engine::GetMainDevice(), pool, nullptr);
}

VkDescriptorSet DescriptorAllocator::Allocate(VkDescriptorSetLayout layout)
{
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.pNext = nullptr;
	allocInfo.descriptorPool = pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet ds;

	VK_CHECK(vkAllocateDescriptorSets(Engine::GetMainDevice(), &allocInfo, &ds));
	return ds;
}

void DescriptorAllocatorGrowable::Init(uint32_t initialSets, std::span<PoolSizeRatio> poolRatios)
{
	ratios.clear();
	for (auto r : poolRatios) {
		ratios.push_back(r);
	}

	VkDescriptorPool newPool = CreatePool(initialSets, poolRatios);

	setsPerPool = initialSets * 1.5;

	readyPools.push_back(newPool);
}

void DescriptorAllocatorGrowable::ClearPools()
{
	for (auto p : readyPools) 
	{
		vkResetDescriptorPool(Engine::GetMainDevice(), p, 0);
	}
	for (auto p : fullPools) 
	{
		vkResetDescriptorPool(Engine::GetMainDevice(), p, 0);
		readyPools.push_back(p);
	}
	fullPools.clear();
}

void DescriptorAllocatorGrowable::DestroyPools()
{
	for (auto p : readyPools) 
	{
		vkDestroyDescriptorPool(Engine::GetMainDevice(), p, nullptr);
	}
	readyPools.clear();
	for (auto p : fullPools) 
	{
		vkDestroyDescriptorPool(Engine::GetMainDevice(), p, nullptr);
	}
	fullPools.clear();
}

VkDescriptorSet DescriptorAllocatorGrowable::Allocate(VkDescriptorSetLayout layout, void* pNext)
{
	VkDescriptorPool poolToUse = GetPool();

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.pNext = pNext;
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = poolToUse;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &layout;

	VkDescriptorSet ds;
	VkResult result = vkAllocateDescriptorSets(Engine::GetMainDevice(), &allocInfo, &ds);

	//allocation failed. Try again
	if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {

		fullPools.push_back(poolToUse);

		poolToUse = GetPool();
		allocInfo.descriptorPool = poolToUse;

		VK_CHECK(vkAllocateDescriptorSets(Engine::GetMainDevice(), &allocInfo, &ds));
	}

	readyPools.push_back(poolToUse);
	return ds;
}

VkDescriptorPool DescriptorAllocatorGrowable::GetPool()
{
	VkDescriptorPool newPool;
	if (readyPools.size() != 0) {
		newPool = readyPools.back();
		readyPools.pop_back();
	}
	else {
		newPool = CreatePool(setsPerPool, ratios);

		setsPerPool *= 1.5;
		if (setsPerPool > 4092) {
			setsPerPool = 4092;
		}
	}

	return newPool;
}

VkDescriptorPool DescriptorAllocatorGrowable::CreatePool(uint32_t setCount, std::span<PoolSizeRatio> poolRatios)
{
	std::vector<VkDescriptorPoolSize> poolSizes;
	for (PoolSizeRatio ratio : poolRatios) {
		poolSizes.push_back(VkDescriptorPoolSize{
			.type = ratio.type,
			.descriptorCount = uint32_t(ratio.ratio * setCount)
			});
	}

	VkDescriptorPoolCreateInfo poolInfo = {};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.flags = 0;
	poolInfo.maxSets = setCount;
	poolInfo.poolSizeCount = (uint32_t)poolSizes.size();
	poolInfo.pPoolSizes = poolSizes.data();

	VkDescriptorPool newPool;
	vkCreateDescriptorPool(Engine::GetMainDevice(), &poolInfo, nullptr, &newPool);
	return newPool;
}

void DescriptorWriter::WriteImage(int binding, VkImageView image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
{
	VkDescriptorImageInfo& info = imageInfos.emplace_back(VkDescriptorImageInfo{
		.sampler = sampler,
		.imageView = image,
		.imageLayout = layout
		});

	VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE; //left empty for now until we need to write it
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pImageInfo = &info;

	writes.push_back(write);
}

void DescriptorWriter::WriteBuffer(int binding, VkBuffer buffer, size_t size, size_t offset, VkDescriptorType type)
{
	VkDescriptorBufferInfo& info = bufferInfos.emplace_back(VkDescriptorBufferInfo{
		.buffer = buffer,
		.offset = offset,
		.range = size
		});

	VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };

	write.dstBinding = binding;
	write.dstSet = VK_NULL_HANDLE; //left empty for now until we need to write it
	write.descriptorCount = 1;
	write.descriptorType = type;
	write.pBufferInfo = &info;

	writes.push_back(write);
}

void DescriptorWriter::Clear()
{
	imageInfos.clear();
	writes.clear();
	bufferInfos.clear();
}

void DescriptorWriter::UpdateSet(VkDescriptorSet set)
{
	for (VkWriteDescriptorSet& write : writes) {
		write.dstSet = set;
	}

	vkUpdateDescriptorSets(Engine::GetMainDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
}
