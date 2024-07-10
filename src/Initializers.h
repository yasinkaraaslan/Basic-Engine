#pragma once
#include "Types.h"

namespace Init {
	VkCommandPoolCreateInfo CommandPoolCreateInfo(uint32_t queueFamilyIndex, VkCommandPoolCreateFlags flag = 0);
	VkCommandBufferAllocateInfo CommandBufferAllocateInfo(VkCommandPool pool, uint32_t count);
	VkFenceCreateInfo FenceCreateInfo(VkFenceCreateFlags flags);
	VkSemaphoreCreateInfo SemaphoreCreateInfo(VkSemaphoreCreateFlags flags);
	VkCommandBufferBeginInfo CommandBufferBeginInfo(VkCommandBufferUsageFlags flags);
	VkImageSubresourceRange ImageSubresourceRange(VkImageAspectFlags aspectMask);
	VkSemaphoreSubmitInfo SemaphoreSubmitInfo(VkPipelineStageFlags2 stageMask, VkSemaphore semaphore);
	VkCommandBufferSubmitInfo CommandBufferSubmitInfo(VkCommandBuffer cmd);
	VkSubmitInfo2 SubmitInfo(VkCommandBufferSubmitInfo* cmd, VkSemaphoreSubmitInfo* signalSemaphoreInfo, VkSemaphoreSubmitInfo* waitSemaphoreInfo);
	VkImageCreateInfo ImageCreateInfo(VkFormat format, VkImageUsageFlags usageFlags, VkExtent3D extent);
	VkImageViewCreateInfo ImageViewCreateInfo(VkFormat format, VkImage image, VkImageAspectFlags aspectFlags);
	VkRenderingAttachmentInfo AttachmentInfo(VkImageView view, VkClearValue* clear, VkImageLayout layout);
	VkRenderingAttachmentInfo DepthAttachmentInfo(VkImageView view, VkImageLayout layout);
	VkRenderingInfo RenderingInfo(VkExtent2D renderExtent, VkRenderingAttachmentInfo* colorAttachment, VkRenderingAttachmentInfo* depthAttachment);
	VkPipelineLayoutCreateInfo PipelineLayoutCreateInfo();
	VkPipelineShaderStageCreateInfo PipelineShaderStageCreateInfo(VkShaderStageFlagBits stage, VkShaderModule shaderModule, const char* entry = "main");
}