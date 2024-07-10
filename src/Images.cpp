#include "Images.h"
#include "Initializers.h"
#include "Engine.h"
#include <SDL_image.h>



void Util::TransitionImage(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout newLayout)
{
	VkImageMemoryBarrier2 imageBarrier{
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2
	};
	imageBarrier.pNext = nullptr;
	imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
	imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

	imageBarrier.oldLayout = currentLayout;
	imageBarrier.newLayout = newLayout;

	VkImageAspectFlags aspectMask = (newLayout == VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
	imageBarrier.subresourceRange = Init::ImageSubresourceRange(aspectMask);
	imageBarrier.image = image;
	VkDependencyInfo depInfo{};
	depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	depInfo.pNext = nullptr;
	depInfo.imageMemoryBarrierCount = 1;
	depInfo.pImageMemoryBarriers = &imageBarrier;

	vkCmdPipelineBarrier2(cmd, &depInfo);
}

void Util::CopyImage(VkCommandBuffer cmd, VkImage src, VkImage dst, VkExtent2D srcSize, VkExtent2D dstSize)
{
	VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
	blitInfo.dstImage = dst;
	blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	blitInfo.srcImage = src;
	blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	blitInfo.filter = VK_FILTER_LINEAR;
	blitInfo.regionCount = 1;
	blitInfo.pRegions = &blitRegion;

	vkCmdBlitImage2(cmd, &blitInfo);
}

//SDL_image extension
SDL_Surface* IMG_LoadFromMemory(void* buffer, int size)
{
    SDL_RWops* rw = SDL_RWFromMem(buffer, size);
    SDL_Surface* temp = IMG_Load_RW(rw, 1);

    if (!temp)
    {
        fmt::println("IMG_Load_RW Error: {}", IMG_GetError());
        return nullptr;
    }

    //Convert the image to optimal display format
    SDL_Surface* image = SDL_ConvertSurfaceFormat(temp, SDL_PIXELFORMAT_RGBA32, 0);

    SDL_FreeSurface(temp);

    return image;
}

std::optional<AllocatedImage> Util::LoadImage(fastgltf::Asset& asset, fastgltf::Image& image)
{
    AllocatedImage newImage{};

    std::visit(
        fastgltf::visitor{
            [](auto& arg) {
            fmt::println("Unhandled type: {}", typeid(arg).name());
            },
            [&](fastgltf::sources::URI& filePath) {
                assert(filePath.fileByteOffset == 0);
                assert(filePath.uri.isLocalPath());

                const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());

                SDL_Surface* surface = IMG_Load(path.c_str());
                if (surface) {
                    VkExtent3D imageSize;
                    imageSize.width = surface->w;
                    imageSize.height = surface->h;
                    imageSize.depth = 1;


                    newImage = Engine::Get()->CreateImage(surface->pixels, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    SDL_FreeSurface(surface);
                }
            },
            [&](fastgltf::sources::Array& vector) {
                SDL_Surface* surface = IMG_LoadFromMemory(vector.bytes.data(), static_cast<int>(vector.bytes.size()));  
                if (surface) {
                    VkExtent3D imageSize;
                    imageSize.width = surface->w;
                    imageSize.height = surface->h;
                    imageSize.depth = 1;


                    newImage = Engine::Get()->CreateImage(surface->pixels, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);

                    SDL_FreeSurface(surface);
                }
            },
            [&](fastgltf::sources::BufferView& view) {
                auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                auto& buffer = asset.buffers[bufferView.bufferIndex];

                std::visit(fastgltf::visitor {
                    [](auto& arg) {
                    fmt::println("Buffer view unhandled type    : {}", typeid(arg).name());
                    },
                    [&](fastgltf::sources::Array& vector) {
                        SDL_Surface* surface = IMG_LoadFromMemory(vector.bytes.data() + bufferView.byteOffset, static_cast<int>(bufferView.byteLength));
                        if (surface) {
                            VkExtent3D imageSize;
                            imageSize.width = surface->w;
                            imageSize.height = surface->h;
                            imageSize.depth = 1;


                            newImage = Engine::Get()->CreateImage(surface->pixels, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT, true);
                            SDL_FreeSurface(surface);

                        }
                    }
                }, buffer.data);
            },
        },
        image.data);

    if (newImage.image == VK_NULL_HANDLE) {
        return {};
    }
    else {
        return newImage;
    }
}

void Util::GenerateMipmaps(VkCommandBuffer cmd, VkImage image, VkExtent2D imageSize)
{
    int mipLevels = int(std::floor(std::log2(std::max(imageSize.width, imageSize.height)))) + 1;
    for (int mip = 0; mip < mipLevels; mip++) {

        VkExtent2D halfSize = imageSize;
        halfSize.width /= 2;
        halfSize.height /= 2;

        VkImageMemoryBarrier2 imageBarrier{ .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .pNext = nullptr };

        imageBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        imageBarrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        imageBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT | VK_ACCESS_2_MEMORY_READ_BIT;

        imageBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageBarrier.subresourceRange = Init::ImageSubresourceRange(aspectMask);
        imageBarrier.subresourceRange.levelCount = 1;
        imageBarrier.subresourceRange.baseMipLevel = mip;
        imageBarrier.image = image;

        VkDependencyInfo depInfo{ .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .pNext = nullptr };
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(cmd, &depInfo);

        if (mip < mipLevels - 1) {
            VkImageBlit2 blitRegion{ .sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2, .pNext = nullptr };

            blitRegion.srcOffsets[1].x = imageSize.width;
            blitRegion.srcOffsets[1].y = imageSize.height;
            blitRegion.srcOffsets[1].z = 1;

            blitRegion.dstOffsets[1].x = halfSize.width;
            blitRegion.dstOffsets[1].y = halfSize.height;
            blitRegion.dstOffsets[1].z = 1;

            blitRegion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.srcSubresource.baseArrayLayer = 0;
            blitRegion.srcSubresource.layerCount = 1;
            blitRegion.srcSubresource.mipLevel = mip;

            blitRegion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blitRegion.dstSubresource.baseArrayLayer = 0;
            blitRegion.dstSubresource.layerCount = 1;
            blitRegion.dstSubresource.mipLevel = mip + 1;

            VkBlitImageInfo2 blitInfo{ .sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, .pNext = nullptr };
            blitInfo.dstImage = image;
            blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            blitInfo.srcImage = image;
            blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            blitInfo.filter = VK_FILTER_LINEAR;
            blitInfo.regionCount = 1;
            blitInfo.pRegions = &blitRegion;

            vkCmdBlitImage2(cmd, &blitInfo);

            imageSize = halfSize;
        }
    }

    TransitionImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

}

