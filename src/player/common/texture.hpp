#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "util.hpp"

namespace daia { namespace player { namespace common {

struct Texture
{
  vk::UniqueImage image;
  vk::UniqueDeviceMemory memory;
  vk::UniqueImageView view;
  vk::UniqueSampler sampler;
  vk::Extent2D extent;

  void setup(const vk::UniqueDevice& device, const vk::PhysicalDevice& physicalDevice, uint32_t width, uint32_t height)
  {
    const auto format = vk::Format::eR8G8B8A8Unorm;

    image = device->createImageUnique({
      .imageType = vk::ImageType::e2D,
      .format = format,
      .extent = { width, height, 1 },
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = vk::SampleCountFlagBits::e1,
      .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
      .sharingMode = vk::SharingMode::eExclusive,
      .initialLayout = vk::ImageLayout::eUndefined,
    });

    const auto memReqs = device->getImageMemoryRequirements(*image);
    memory = device->allocateMemoryUnique({
      .allocationSize = memReqs.size,
      .memoryTypeIndex = findMemoryType(
        physicalDevice,
        memReqs.memoryTypeBits,
        vk::MemoryPropertyFlagBits::eDeviceLocal),
    });
    device->bindImageMemory(*image, *memory, 0);

    view = device->createImageViewUnique(
      { .image = *image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {
          .aspectMask = vk::ImageAspectFlagBits::eColor,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
        } });

    sampler = device->createSamplerUnique(
      {
        .magFilter = vk::Filter::eLinear,
        .minFilter = vk::Filter::eLinear,
        .mipmapMode = vk::SamplerMipmapMode::eLinear,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge,
      });

    extent = vk::Extent2D{ width, height };
  }

  size_t calcBufferSize() const
  {
    return extent.width * extent.height * sizeof(uint32_t);
  }

  vk::DescriptorImageInfo createDescriptorInfo() const
  {
    return vk::DescriptorImageInfo{
      .sampler = *sampler,
      .imageView = *view,
      .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
    };
  }

  void destroy()
  {
    sampler.reset();
    view.reset();
    memory.reset();
    image.reset();
    extent = vk::Extent2D{ 0, 0 };
  }
};

}}} // namespace daia::player::common
