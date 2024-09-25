#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "../../util/util.hpp"
#include "../common/texture.hpp"
#include "../content/content_base.hpp"
#include "../window.hpp"
#include "helpers.hpp"
#include "viewport.hpp"

namespace daia { namespace player { namespace pipeline {

struct SetupArgs
{
  const std::filesystem::path appRoot;
  const std::string appName;
  uint32_t width;
  uint32_t height;
  std::vector<const char*> instanceExtensions;
  std::vector<const char*> deviceExtensions;
  std::vector<const char*> layers;
  const bool enableValidationLayers = false;
  const Window& window;

  void normalize()
  {
    deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    if (enableValidationLayers)
    {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    util::distinct(instanceExtensions);
    util::distinct(deviceExtensions);
    util::distinct(layers);
  }
};

class Pipeline
{
private:
  struct WrappedContent
  {
    std::shared_ptr<content::Content> content = nullptr;
    common::Texture texture;
  };

  static std::vector<const char*> checkDeviceExtensions(const vk::PhysicalDevice& device, const std::vector<const char*>& requestedExtensions)
  {
    const auto& extensions = device.enumerateDeviceExtensionProperties();
    std::vector<const char*> missing;
    for (const auto& r : requestedExtensions)
    {
      if (extensions.end() == std::find_if(extensions.begin(), extensions.end(), [&r](const auto& a) { return std::strcmp(a.extensionName, r) == 0; }))
      {
        missing.push_back(r);
      }
    }
    return missing;
  }

public:
  Pipeline() = default;
  ~Pipeline() { destroy(); }

  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;
  Pipeline(Pipeline&&) = default;
  Pipeline& operator=(Pipeline&&) = default;

  bool setup(SetupArgs& info)
  {
    info.normalize();

    auto missingLayers = checkLayers(info.layers);
    if (missingLayers.size() > 0)
    {
      util::println("Layers not found");
      for (const auto& l : missingLayers)
      {
        util::println(l);
      }
      return false;
    }

    // instance
    vk::ApplicationInfo appInfo{
      .pApplicationName = info.appName.c_str(),
      .apiVersion = VK_API_VERSION_1_3,
    };

    _instance = vk::createInstanceUnique({
      .pApplicationInfo = &appInfo,
      .enabledLayerCount = static_cast<uint32_t>(info.layers.size()),
      .ppEnabledLayerNames = info.layers.data(),
      .enabledExtensionCount = static_cast<uint32_t>(info.instanceExtensions.size()),
      .ppEnabledExtensionNames = info.instanceExtensions.data(),
    });

    // debug messenger
    {
      VkDebugUtilsMessengerCreateInfoEXT info = {};
      info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      info.pfnUserCallback = debugCallback;

      if (CreateDebugUtilsMessengerEXT(*_instance, &info, nullptr, &_debugMessenger) != VK_SUCCESS)
      {
        std::cerr << "Failed to set up debug messenger!" << std::endl;
        return false;
      }
    }

    // surface
    _surface = info.window.createSurface(*_instance);

    // physical device
    {
      // todo: query by
      //   - device extensions
      //   - surface format
      //   - surface presentModes
      _physicalDevice = _instance->enumeratePhysicalDevices().front();
      const auto props = _physicalDevice.getQueueFamilyProperties();
      _queueFamilyIndex = props.size();
      for (int i = 0; i < props.size(); i++)
      {
        const auto& prop = props[i];
        if (prop.queueFlags & vk::QueueFlagBits::eGraphics && _physicalDevice.getSurfaceSupportKHR(i, _surface))
        {
          _queueFamilyIndex = i;
          break;
        }
      }
      if (_queueFamilyIndex == props.size())
      {
        std::cerr << "Failed to find queue family property" << std::endl;
        return false;
      }
    }

    // logical device
    {
      const float queuePriority = 1;
      const auto deviceQueueCreateInfo = vk::DeviceQueueCreateInfo{
        .queueFamilyIndex = _queueFamilyIndex,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
      };

      vk::PhysicalDeviceFeatures deviceFeatures = {};

      _device = _physicalDevice.createDeviceUnique({
        .flags = vk::DeviceCreateFlags(),
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &deviceQueueCreateInfo,
        .enabledLayerCount = static_cast<uint32_t>(info.layers.size()),
        .ppEnabledLayerNames = info.layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(info.deviceExtensions.size()),
        .ppEnabledExtensionNames = info.deviceExtensions.data(),
        .pEnabledFeatures = &deviceFeatures,
      });

      _graphicsQueue = _device->getQueue(_queueFamilyIndex, 0);
    }

    // command buffer
    _commandPool = _device->createCommandPoolUnique({
      .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
      .queueFamilyIndex = _queueFamilyIndex,
    });
    _commandBuffers = _device->allocateCommandBuffers({
      .commandPool = *_commandPool,
      .level = vk::CommandBufferLevel::ePrimary,
      .commandBufferCount = 1,
    });

    // swapchain
    {
      const auto formats = _physicalDevice.getSurfaceFormatsKHR(_surface);
      // check capabilities
      _colorFormat = vk::Format::eB8G8R8A8Unorm;
      const auto colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
      const auto presentMode = vk::PresentModeKHR::eFifo;

      const auto capabilities = _physicalDevice.getSurfaceCapabilitiesKHR(_surface);
      _swapchainExtent = capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()
        ? capabilities.currentExtent
        : vk::Extent2D{
            .width = std::clamp(info.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            .height = std::clamp(info.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
          };

      auto imageCount = capabilities.minImageCount + 1;
      if (capabilities.maxImageCount > 0 && capabilities.maxImageCount < imageCount)
      {
        imageCount = capabilities.maxImageCount;
      }

      _swapchain = _device->createSwapchainKHRUnique({
        .surface = _surface,
        .minImageCount = imageCount,
        .imageFormat = _colorFormat,
        .imageColorSpace = colorSpace,
        .imageExtent = _swapchainExtent,
        .imageArrayLayers = 1,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE,
      });

      _swapchainImages = _device->getSwapchainImagesKHR(*_swapchain);
      for (const auto& image : _swapchainImages)
      {
        _swapchainImageViews.emplace_back(_device->createImageViewUnique({
          .image = image,
          .viewType = vk::ImageViewType::e2D,
          .format = _colorFormat,
          .components = {
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity,
          },
          .subresourceRange = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
          },
        }));
      }
    }

    // semaphores
    _imageAcquiredSemaphore = _device->createSemaphoreUnique({});
    _renderFinishedSemaphore = _device->createSemaphoreUnique({});

    // fence
    _drawFence = _device->createFenceUnique({
      .flags = vk::FenceCreateFlagBits::eSignaled,
    });

    // render pass
    {
      std::array<vk::AttachmentDescription, 1> attachmentDescriptions{
        // color
        vk::AttachmentDescription{
          .format = _colorFormat,
          .samples = vk::SampleCountFlagBits::e1,
          .loadOp = vk::AttachmentLoadOp::eDontCare,
          .storeOp = vk::AttachmentStoreOp::eStore,
          .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
          .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
          .initialLayout = vk::ImageLayout::eUndefined,
          .finalLayout = vk::ImageLayout::ePresentSrcKHR,
        },
      };

      const auto colorReference = vk::AttachmentReference{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal,
      };

      const auto subpass = vk::SubpassDescription{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorReference,
      };

      _renderPass = _device->createRenderPassUnique({
        .attachmentCount = attachmentDescriptions.size(),
        .pAttachments = attachmentDescriptions.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
      });
    }

    // pipeline
    {
      _vertShaderModule = createShaderModule(*_device, info.appRoot / "../player/pipeline/shader/pane.vert.spv");
      _fragShaderModule = createShaderModule(*_device, info.appRoot / "../player/pipeline/shader/pane.frag.spv");

      std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStageCreateInfo = {
        vk::PipelineShaderStageCreateInfo{
          .stage = vk::ShaderStageFlagBits::eVertex,
          .module = *_vertShaderModule,
          .pName = "main",
        },
        vk::PipelineShaderStageCreateInfo{
          .stage = vk::ShaderStageFlagBits::eFragment,
          .module = *_fragShaderModule,
          .pName = "main",
        }
      };

      const auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
      };

      const auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = false,
      };

      const auto rasterizer = vk::PipelineRasterizationStateCreateInfo{
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = false,
        .lineWidth = 1.0,
      };

      const auto multisampling = vk::PipelineMultisampleStateCreateInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
      };

      {
        std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
          vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBuffer,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
          },
          vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
          }
        };
        _descriptorSetLayout = _device->createDescriptorSetLayoutUnique({
          .bindingCount = static_cast<uint32_t>(bindings.size()),
          .pBindings = bindings.data(),
        });

        const auto pushConstantRanges = vk::PushConstantRange{
          .stageFlags = vk::ShaderStageFlagBits::eFragment,
          .offset = 0,
          .size = sizeof(PushConstant),
        };

        auto descriptorSetLayout = *_descriptorSetLayout;
        _pipelineLayout = _device->createPipelineLayoutUnique({
          .setLayoutCount = 1,
          .pSetLayouts = &descriptorSetLayout,
          .pushConstantRangeCount = 1,
          .pPushConstantRanges = &pushConstantRanges,
        });
      }

      const auto dynamicStates = std::vector<vk::DynamicState>{
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
      };
      const auto dynamicState = vk::PipelineDynamicStateCreateInfo{
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
      };

      const auto colorBlendingAttachment = vk::PipelineColorBlendAttachmentState{
        .blendEnable = false,
        .srcColorBlendFactor = vk::BlendFactor::eOne,
        .dstColorBlendFactor = vk::BlendFactor::eZero,
        .colorBlendOp = vk::BlendOp::eAdd,
        .srcAlphaBlendFactor = vk::BlendFactor::eOne,
        .dstAlphaBlendFactor = vk::BlendFactor::eZero,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
      };

      const auto colorBlending = vk::PipelineColorBlendStateCreateInfo{
        .logicOpEnable = false,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = 1,
        .pAttachments = &colorBlendingAttachment,
        .blendConstants = {},
      };

      const auto viewportState = vk::PipelineViewportStateCreateInfo{
        .viewportCount = 1,
        .scissorCount = 1,
      };

      auto [result, pipeline] = _device->createGraphicsPipelineUnique(
        nullptr,
        {
          .stageCount = shaderStageCreateInfo.size(),
          .pStages = shaderStageCreateInfo.data(),
          .pVertexInputState = &vertexInputInfo,
          .pInputAssemblyState = &inputAssembly,
          .pViewportState = &viewportState,
          .pRasterizationState = &rasterizer,
          .pMultisampleState = &multisampling,
          .pColorBlendState = &colorBlending,
          .pDynamicState = &dynamicState,
          .layout = *_pipelineLayout,
          .renderPass = *_renderPass,
          .subpass = 0,
        });
      switch (result)
      {
        case vk::Result::eSuccess:
          break;
        case vk::Result::ePipelineCompileRequiredEXT:
          // something meaningfull here
          break;
        default:
          assert(false); // should never happen
      }
      _pipeline = std::move(pipeline);
    }

    // framebuffers
    _frameBuffers.resize(_swapchainImageViews.size());
    std::transform(_swapchainImageViews.begin(), _swapchainImageViews.end(), _frameBuffers.begin(), [this](const auto& view) {
      auto viewHandle = *view;
      return _device->createFramebufferUnique({
        .renderPass = *_renderPass,
        .attachmentCount = 1,
        .pAttachments = &viewHandle,
        .width = _swapchainExtent.width,
        .height = _swapchainExtent.height,
        .layers = 1,
      });
    });

    // ubo
    {
      _blankViewUboBuffer = _device->createBufferUnique({
        .size = sizeof(ViewportSet::BlankUboData),
        .usage = vk::BufferUsageFlagBits::eUniformBuffer,
        .sharingMode = vk::SharingMode::eExclusive,
      });

      vk::MemoryRequirements memRequirements = _device->getBufferMemoryRequirements(*_blankViewUboBuffer);
      _blankViewUboMemory = _device->allocateMemoryUnique(
        { .allocationSize = memRequirements.size,
          .memoryTypeIndex = common::findMemoryType(
            _physicalDevice,
            memRequirements.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent) });

      _device->bindBufferMemory(*_blankViewUboBuffer, *_blankViewUboMemory, 0);
    }

    // descriptor set
    {
      std::array<vk::DescriptorPoolSize, 2> poolSizes{
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eUniformBuffer, .descriptorCount = 1 },
        vk::DescriptorPoolSize{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1 },
      };

      _descriptorPool = _device->createDescriptorPoolUnique({
        .maxSets = 2,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes.data(),
      });

      auto descriptorSetLayout = *_descriptorSetLayout;
      _descriptorSets = _device->allocateDescriptorSets({
        .descriptorPool = *_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &descriptorSetLayout,
      });
    }

    createViewports();

    return true;
  }

  void createViewports()
  {
    _viewports.add({ 0.1, 0.1, 0.3, 0.6, util::float4{ 0.4, 0.6, 0.2, 1.0 } });
    _viewports.add({ 0.5, 0, 0.5, 1, util::float4{ 0.2, 0.6, 0.8, 1.0 } });

    const auto uboData = _viewports.getBlankUboData();
    constexpr auto size = sizeof(ViewportSet::BlankUboData);
    void* data = _device->mapMemory(*_blankViewUboMemory, 0, size);
    memcpy(data, &uboData, size);
    _device->unmapMemory(*_blankViewUboMemory);

    const auto bufferInfo = vk::DescriptorBufferInfo{
      .buffer = *_blankViewUboBuffer,
      .offset = 0,
      .range = sizeof(ViewportSet::BlankUboData),
    };
    const auto write = vk::WriteDescriptorSet{
      .dstSet = _descriptorSets.front(),
      .dstBinding = 0,
      .descriptorCount = 1,
      .descriptorType = vk::DescriptorType::eUniformBuffer,
      .pBufferInfo = &bufferInfo,
    };
    _device->updateDescriptorSets(1, &write, 0, nullptr);
  }

  void recordCommand(const vk::CommandBuffer& commandBuffer, const uint32_t currentIndex)
  {
    commandBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlags() });

    commandBuffer.beginRenderPass(
      {
        .renderPass = *_renderPass,
        .framebuffer = *_frameBuffers[currentIndex],
        .renderArea = {
          .offset = { 0, 0 },
          .extent = _swapchainExtent,
        },
      },
      vk::SubpassContents::eInline);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *_pipeline);
    commandBuffer.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics,
      *_pipelineLayout,
      0,
      1,
      _descriptorSets.data(),
      0,
      nullptr);

    for (uint32_t i = 0; i < _viewports.size(); i++)
    {
      const auto pushConstant = PushConstant{
        .viewportIndex = i
      };
      const auto& vp = _viewports.get(i, _swapchainExtent);
      commandBuffer.setViewport(0, vp.viewport);
      commandBuffer.setScissor(0, vp.scissor);
      commandBuffer.pushConstants(*_pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstant), &pushConstant);
      commandBuffer.draw(9, 1, 0, 0);
    }

    commandBuffer.endRenderPass();

    commandBuffer.end();
  }

  void draw()
  {
    const auto& commandBuffer = _commandBuffers.front();

    const auto swapchain = *_swapchain;
    const auto imageAcquiredSemaphore = *_imageAcquiredSemaphore;
    const auto renderFinishedSemaphore = *_renderFinishedSemaphore;

    const auto currentIndex = _device->acquireNextImageKHR(swapchain, std::numeric_limits<uint64_t>::max(), imageAcquiredSemaphore, nullptr).value;
    _device->resetFences(*_drawFence);

    recordCommand(commandBuffer, currentIndex);

    const auto waitDestinationStageMask = vk::PipelineStageFlags{
      vk::PipelineStageFlagBits::eColorAttachmentOutput,
    };
    _graphicsQueue.submit(
      { {
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAcquiredSemaphore,
        .pWaitDstStageMask = &waitDestinationStageMask,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphore,
      } },
      *_drawFence);

    while (vk::Result::eTimeout == _device->waitForFences({ *_drawFence }, true, std::numeric_limits<uint64_t>::max()))
      ;

    const auto result = _graphicsQueue.presentKHR({
      .waitSemaphoreCount = 1,
      .pWaitSemaphores = &renderFinishedSemaphore,
      .swapchainCount = 1,
      .pSwapchains = &swapchain,
      .pImageIndices = &currentIndex,
    });
    switch (result)
    {
      case vk::Result::eSuccess:
        break;
      case vk::Result::eSuboptimalKHR:
        std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n";
        break;
      default:
        assert(false); // an unexpected result is returned !
    }
  }

  void update(double globalTime)
  {
    _device->resetFences(*_drawFence);

    auto& commandBuffer = _commandBuffers.front();

    commandBuffer.begin(vk::CommandBufferBeginInfo{ .flags = vk::CommandBufferUsageFlags() });

    std::vector<vk::UniqueBuffer> buffers;
    std::vector<vk::UniqueDeviceMemory> memories; // 一つに

    for (const auto& [_, t] : _contents)
    {
      const auto& viewport = _viewports.get(0, _swapchainExtent);
      const auto& [content, texture] = t;

      if (content->update({
            .time = globalTime,
            .width = viewport.viewport.width,
            .height = viewport.viewport.height,
          }))
      {
        // cpu texture upload

        const auto size = texture.calcBufferSize();

        const auto& stagingBuffer = buffers.emplace_back(_device->createBufferUnique({
          .size = size,
          .usage = vk::BufferUsageFlagBits::eTransferSrc,
          .sharingMode = vk::SharingMode::eExclusive,
        }));

        const auto memReqs = _device->getBufferMemoryRequirements(*stagingBuffer);
        const auto& stagingMemory = memories.emplace_back(_device->allocateMemoryUnique(
          {
            .allocationSize = memReqs.size,
            .memoryTypeIndex = common::findMemoryType(
              _physicalDevice,
              memReqs.memoryTypeBits,
              vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent),
          }));

        _device->bindBufferMemory(*stagingBuffer, *stagingMemory, 0);
        void* mapped = _device->mapMemory(*stagingMemory, 0, size);
        memcpy(mapped, content->data().data(), size);
        _device->unmapMemory(*stagingMemory);

        commandBuffer.pipelineBarrier(
          vk::PipelineStageFlagBits::eTopOfPipe,
          vk::PipelineStageFlagBits::eTransfer,
          {},
          nullptr,
          nullptr,
          vk::ImageMemoryBarrier{
            .srcAccessMask = {},
            .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
            .oldLayout = vk::ImageLayout::eUndefined,
            .newLayout = vk::ImageLayout::eTransferDstOptimal,
            .image = *texture.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } });

        auto [w, h] = content->size();
        commandBuffer.copyBufferToImage(
          *stagingBuffer,
          *texture.image,
          vk::ImageLayout::eTransferDstOptimal,
          vk::BufferImageCopy{
            .bufferOffset = 0,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
              .aspectMask = vk::ImageAspectFlagBits::eColor,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { w, h, 1 },
          });

        commandBuffer.pipelineBarrier(
          vk::PipelineStageFlagBits::eTransfer,
          vk::PipelineStageFlagBits::eFragmentShader,
          {},
          nullptr,
          nullptr,
          vk::ImageMemoryBarrier{
            .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
            .dstAccessMask = vk::AccessFlagBits::eShaderRead,
            .oldLayout = vk::ImageLayout::eTransferDstOptimal,
            .newLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
            .image = *texture.image,
            .subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } });
      }
    }

    commandBuffer.end();

    _graphicsQueue.submit(
      { {
        .waitSemaphoreCount = 0,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
      } },
      *_drawFence);

    while (vk::Result::eTimeout == _device->waitForFences({ *_drawFence }, true, std::numeric_limits<uint64_t>::max()))
      ;

    _graphicsQueue.waitIdle();
    commandBuffer.reset();
  }

  bool registerContent(std::string key, std::shared_ptr<content::Content> content)
  {
    if (_contents.contains(key))
    {
      return false;
    }

    content->setup({
      .device = _device,
      .physicalDevice = _physicalDevice,
    });

    // setup texture
    auto [w, h] = content->size();
    auto texture = common::Texture();
    texture.setup(_device, _physicalDevice, w, h);

    const auto imageInfo = texture.createDescriptorInfo();
    const auto write = vk::WriteDescriptorSet{
      .dstSet = _descriptorSets.front(),
      .dstBinding = 1,
      .descriptorCount = 1,
      .descriptorType = vk::DescriptorType::eCombinedImageSampler,
      .pImageInfo = &imageInfo,
    };
    _device->updateDescriptorSets(1, &write, 0, nullptr);

    _contents[std::move(key)] = {
      .content = std::move(content),
      .texture = std::move(texture),
    };

    return true;
  }

  void unregisterContent(std::string key)
  {
    if (auto it = _contents.find(key); it != _contents.end())
    {
      it->second.content->destroy();
      it->second.texture.destroy();
      _contents.erase(it);
    }
  }

  void unregisterAllContents()
  {
    for (auto& [_, t] : _contents)
    {
      t.content->destroy();
      t.texture.destroy();
    }
    _contents.clear();
  }

  void destroy()
  {
    if (!_instance)
      return;

    if (_device)
      _device->waitIdle();

    // Reset device-owned resources
    unregisterAllContents();
    _blankViewUboMemory.reset();
    _blankViewUboBuffer.reset();
    _frameBuffers.clear();
    _pipeline.reset();
    _pipelineLayout.reset();
    _renderPass.reset();
    _vertShaderModule.reset();
    _fragShaderModule.reset();
    _imageAcquiredSemaphore.reset();
    _renderFinishedSemaphore.reset();
    _drawFence.reset();
    _swapchainImageViews.clear();
    _swapchain.reset();
    _descriptorPool.reset();
    _descriptorSetLayout.reset();
    _commandBuffers.clear();
    _commandPool.reset();
    _device.reset();

    // Manually managed instance-owned resources
    _instance->destroySurfaceKHR(_surface);
    _surface = nullptr;
    DestroyDebugUtilsMessengerEXT(*_instance, _debugMessenger, nullptr);
    _debugMessenger = {};
    _instance.reset();
  }

private:
  vk::UniqueInstance _instance;
  VkDebugUtilsMessengerEXT _debugMessenger = {};
  vk::SurfaceKHR _surface;
  vk::PhysicalDevice _physicalDevice;

  vk::UniqueDevice _device;

  uint32_t _queueFamilyIndex = 0;
  vk::Queue _graphicsQueue;

  vk::UniqueCommandPool _commandPool;
  std::vector<vk::CommandBuffer> _commandBuffers;

  vk::UniqueSwapchainKHR _swapchain;
  vk::Format _colorFormat = {};
  vk::Extent2D _swapchainExtent = {};
  std::vector<vk::Image> _swapchainImages;
  std::vector<vk::UniqueImageView> _swapchainImageViews;
  std::vector<vk::UniqueFramebuffer> _frameBuffers;

  ViewportSet _viewports;

  vk::UniqueSemaphore _imageAcquiredSemaphore;
  vk::UniqueSemaphore _renderFinishedSemaphore;
  vk::UniqueFence _drawFence;

  vk::UniqueDescriptorSetLayout _descriptorSetLayout;
  vk::UniqueDescriptorPool _descriptorPool;
  std::vector<vk::DescriptorSet> _descriptorSets;

  vk::UniquePipelineLayout _pipelineLayout;
  vk::UniquePipeline _pipeline;

  vk::UniqueRenderPass _renderPass;

  vk::UniqueShaderModule _vertShaderModule;
  vk::UniqueShaderModule _fragShaderModule;

  vk::UniqueBuffer _blankViewUboBuffer;
  vk::UniqueDeviceMemory _blankViewUboMemory;

  std::unordered_map<std::string, WrappedContent> _contents;
};

}}} // namespace daia::player::pipeline
