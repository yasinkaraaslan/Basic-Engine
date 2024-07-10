#pragma once
#include "Types.h"
#include <fastgltf/core.hpp>

namespace Util
{
	void TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout);
	void CopyImage(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize);
	std::optional<AllocatedImage> LoadImage(fastgltf::Asset& asset, fastgltf::Image& image);
	void GenerateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize);
};
