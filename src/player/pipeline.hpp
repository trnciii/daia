#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "../util/util.hpp"
#include "window.hpp"

namespace daia { namespace player{ namespace pipeline {

using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;

struct SetupInfo
{
	const std::filesystem::path appRoot;
	const std::string appName;
	uint32_t width;
	uint32_t height;
	std::vector<const char*> instanceExtensions;
	std::vector<const char*> deviceExtensions;
	std::vector<const char*> layers;
	const bool enableValidationLayers = false;
	const window::Window& window;

	void normalize()
	{
		deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		deviceExtensions.push_back(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME);
		deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME);

		// deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME);
		// deviceExtensions.push_back(VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME);

		deviceExtensions.push_back(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME);
		// deviceExtensions.push_back(VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);

		instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

		if(enableValidationLayers)
		{
			layers.push_back("VK_LAYER_KHRONOS_validation");
			instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		util::distinct(instanceExtensions);
		util::distinct(deviceExtensions);
		util::distinct(layers);
	}
};


class ViewportSet
{
public:
	struct BlankUboData
	{
		float4 colors[8];
	};

	struct ViewportSource
	{
		float x;
		float y;
		float width;
		float height;
		float4 color;
	};

	struct Viewport
	{
		vk::Viewport viewport;
		vk::Rect2D scissor;
	};

	const size_t add(const ViewportSource& source)
	{
		size_t index = sources.size();
		sources.emplace_back(source);
		return index;
	}

	const Viewport get(const size_t i, const vk::Extent2D& screenExtent) const
	{
		return {
			.viewport = vk::Viewport
			{
				.x = screenExtent.width * sources[i].x,
				.y = screenExtent.height * sources[i].y,
				.width = screenExtent.width * sources[i].width,
				.height = screenExtent.height * sources[i].height,
				.minDepth = 0,
				.maxDepth = 1,
			},
			.scissor = {
				.offset = {
					.x = static_cast<int>(screenExtent.width * sources[i].x),
					.y = static_cast<int>(screenExtent.height * sources[i].y),
				},
				.extent = {
					static_cast<uint32_t>(screenExtent.width * sources[i].width),
					static_cast<uint32_t>(screenExtent.height * sources[i].height),
				},
			}
		};
	}

	const size_t size()
	{
		return sources.size();
	}

	const BlankUboData getBlankUboData() const
	{
		BlankUboData data;
		for(int i=0; i<sources.size(); i++)
		{
			data.colors[i] = sources[i].color;
		}
		return data;
	}

private:
	std::vector<ViewportSource> sources;
};

struct PushConstant
{
	uint32_t viewportIndex;
};

inline std::vector<const char*> checkLayers(const std::vector<const char*>& requestedLayers)
{
	const auto availableLayers = vk::enumerateInstanceLayerProperties();
	std::vector<const char*> missing;
	for(const auto& r : requestedLayers)
	{
		if(availableLayers.end() == std::find_if(
			availableLayers.begin(), availableLayers.end(),
			[&r](const auto& a){return std::strcmp(a.layerName, r) == 0;}))
		{
			missing.push_back(r);
		}
	}
	return missing;
}



inline VkResult CreateDebugUtilsMessengerEXT(
	VkInstance instance,
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugUtilsMessengerEXT* pDebugMessenger)
{
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
	if (func != nullptr) {
		return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
	} else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

inline void DestroyDebugUtilsMessengerEXT(
	VkInstance instance,
	VkDebugUtilsMessengerEXT debugMessenger,
	const VkAllocationCallbacks* pAllocator)
{
	auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
	if (func != nullptr) {
		func(instance, debugMessenger, pAllocator);
	}
}

inline VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData)
{
	std::cerr << "Validation Layer: " << pCallbackData->pMessage << std::endl;
	return VK_FALSE;
}

inline vk::ShaderModule createShaderModule(const vk::Device& device, const std::filesystem::path& filepath)
{
	const auto code = util::readAllText(filepath);
	const auto createInfo = vk::ShaderModuleCreateInfo{
		.codeSize = code.size(),
		.pCode = reinterpret_cast<const uint32_t*>(code.data()),
	};
	return device.createShaderModule(createInfo, nullptr);
}

uint32_t findMemoryType(vk::PhysicalDevice physicalDevice, uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type!");
}

class Pipeline
{
private:
	static std::vector<const char*> checkDeviceExtensions(const vk::PhysicalDevice& device, const std::vector<const char*>& requestedExtensions)
	{
		const auto& extensions = device.enumerateDeviceExtensionProperties();
		std::vector<const char*> missing;
		for(const auto& r : requestedExtensions)
		{
			if(extensions.end() == std::find_if(
				extensions.begin(), extensions.end(),
				[&r](const auto& a){return std::strcmp(a.extensionName, r);}))
			{
				missing.push_back(r);
			}
		}
		return missing;
	}

public:
	bool setup(SetupInfo& info)
	{
		info.normalize();

		auto missingLayers = checkLayers(info.layers);
		if(missingLayers.size() > 0)
		{
			util::println("Layers not found");
			for(const auto& l : missingLayers)
			{
				util::println(l);
			}
			return false;
		}

		// instance
		vk::ApplicationInfo appInfo
		{
			.pApplicationName = info.appName.c_str(),
			.apiVersion = VK_API_VERSION_1_3,
		};

		_instance = vk::createInstance({
			.flags = vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR,
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

			if (CreateDebugUtilsMessengerEXT(_instance, &info, nullptr, &_debugMessenger) != VK_SUCCESS) {
				std::cerr << "Failed to set up debug messenger!" << std::endl;
				return false;
			}
		}

		// surface
		_surface = info.window.createSurface(_instance);

		// physical device
		{
			// todo: query by
			//   - device extensions
			//   - surface format
			//   - surface presentModes
			_physicalDevice = _instance.enumeratePhysicalDevices().front();
			const auto props = _physicalDevice.getQueueFamilyProperties();
			_queueFamilyIndex = props.size();
			for(int i=0; i<props.size(); i++)
			{
				const auto& prop = props[i];
				if(prop.queueFlags & vk::QueueFlagBits::eGraphics
					&& _physicalDevice.getSurfaceSupportKHR(i, _surface))
				{
					_queueFamilyIndex = i;
					break;
				}
			}
			if(_queueFamilyIndex == props.size())
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

			_device = _physicalDevice.createDevice({
				.flags = vk::DeviceCreateFlags(),
				.queueCreateInfoCount = 1,
				.pQueueCreateInfos = &deviceQueueCreateInfo,
				.enabledLayerCount = static_cast<uint32_t>(info.layers.size()),
				.ppEnabledLayerNames = info.layers.data(),
				.enabledExtensionCount  = static_cast<uint32_t>(info.deviceExtensions.size()),
				.ppEnabledExtensionNames = info.deviceExtensions.data(),
				.pEnabledFeatures = &deviceFeatures,
			});

			_grpahicsQueue = _device.getQueue(_queueFamilyIndex, 0);

			_loader = vk::DispatchLoaderDynamic(_instance, vkGetInstanceProcAddr, _device, vkGetDeviceProcAddr);
		}

		// command buffer
		_commandPool = _device.createCommandPool({
			.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
			.queueFamilyIndex = _queueFamilyIndex,
		});
		_commandBuffers = _device.allocateCommandBuffers({
			.commandPool = _commandPool,
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
			_swapchainExtent = capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()?
				capabilities.currentExtent
				: vk::Extent2D {
					.width = std::clamp(info.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
					.height = std::clamp(info.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
				};

			auto imageCount = capabilities.minImageCount + 1;
			if(capabilities.maxImageCount > 0 && capabilities.maxImageCount < imageCount )
			{
				imageCount = capabilities.maxImageCount;
			}

			_swapchain = _device.createSwapchainKHR({
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

			_swapchainImages = _device.getSwapchainImagesKHR(_swapchain);
			for(const auto& image : _swapchainImages)
			{
				_swapchainImageViews.emplace_back(_device.createImageView({
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
		_imageAquiredSemaphore = _device.createSemaphore({});
		_renderFinishedSemaphore = _device.createSemaphore({});

		// fence
		_drawFence = _device.createFence({
			.flags = vk::FenceCreateFlagBits::eSignaled,
		});

		// render pass
		{
			std::array<vk::AttachmentDescription, 1> attachmentDescriptions
			{
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

			const auto colorReference = vk::AttachmentReference
			{
				.attachment = 0,
				.layout = vk::ImageLayout::eColorAttachmentOptimal,
			};

			const auto subpass = vk::SubpassDescription
			{
				.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
				.colorAttachmentCount = 1,
				.pColorAttachments = &colorReference,
			};

			_renderPass = _device.createRenderPass({
				.attachmentCount = attachmentDescriptions.size(),
				.pAttachments = attachmentDescriptions.data(),
				.subpassCount = 1,
				.pSubpasses = &subpass,
			});
		}

		// pipeline
		{
			_vertShaderModule = createShaderModule(_device, info.appRoot/"shader/triangle.vert.spv");
			_fragShaderModule = createShaderModule(_device, info.appRoot/"shader/triangle.frag.spv");

			std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStageCreateInfo = {
				vk::PipelineShaderStageCreateInfo{
					.stage = vk::ShaderStageFlagBits::eVertex,
					.module = _vertShaderModule,
					.pName = "main",
				},
				vk::PipelineShaderStageCreateInfo{
					.stage = vk::ShaderStageFlagBits::eFragment,
					.module = _fragShaderModule,
					.pName = "main",
				}
			};

			const auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo
			{
				.vertexBindingDescriptionCount = 0,
				.vertexAttributeDescriptionCount = 0,
			};

			const auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo
			{
				.topology = vk::PrimitiveTopology::eTriangleList,
				.primitiveRestartEnable = false,
			};

			const auto rasterizer = vk::PipelineRasterizationStateCreateInfo
			{
				.depthClampEnable = false,
				.rasterizerDiscardEnable = false,
				.polygonMode = vk::PolygonMode::eFill,
				.cullMode = vk::CullModeFlagBits::eBack,
				.frontFace = vk::FrontFace::eClockwise,
				.depthBiasEnable = false,
				.lineWidth = 1.0,
			};

			const auto multisampling = vk::PipelineMultisampleStateCreateInfo
			{
				.rasterizationSamples = vk::SampleCountFlagBits::e1,
				.sampleShadingEnable = vk::False,
			};

			{
				const auto uboLayoutBinding = vk::DescriptorSetLayoutBinding{
					.binding  = 0,
					.descriptorType = vk::DescriptorType::eUniformBuffer,
					.descriptorCount = 1,
					.stageFlags = vk::ShaderStageFlagBits::eFragment,
				};

				_descriptorSetLayout = _device.createDescriptorSetLayout({
					.bindingCount = 1,
					.pBindings = &uboLayoutBinding,
				});

				const auto pushConstantRanges = vk::PushConstantRange{
					.stageFlags = vk::ShaderStageFlagBits::eFragment,
					.offset = 0,
					.size = sizeof(PushConstant),
				};

				_pipelineLayout = _device.createPipelineLayout({
					.setLayoutCount = 1,
					.pSetLayouts = &_descriptorSetLayout,
					.pushConstantRangeCount = 1,
					.pPushConstantRanges = &pushConstantRanges,
				});
			}

			const auto dynamicStates = std::vector<vk::DynamicState>{
				vk::DynamicState::eViewport,
				vk::DynamicState::eScissor,
			};
			const auto dynamicState = vk::PipelineDynamicStateCreateInfo
			{
				.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
				.pDynamicStates = dynamicStates.data(),
			};

			const auto colorBlendingAttachment = vk::PipelineColorBlendAttachmentState
			{
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

			const auto viewportState = vk::PipelineViewportStateCreateInfo
			{
				.viewportCount = 1,
				.scissorCount = 1,
			};

			vk::Result result;
			std::tie(result, _pipeline) = _device.createGraphicsPipeline(nullptr, {
				.stageCount = shaderStageCreateInfo.size(),
				.pStages = shaderStageCreateInfo.data(),
				.pVertexInputState = &vertexInputInfo,
				.pInputAssemblyState = &inputAssembly,
				.pViewportState = &viewportState,
				.pRasterizationState = &rasterizer,
				.pMultisampleState = &multisampling,
				.pColorBlendState = &colorBlending,
				.pDynamicState = &dynamicState,
				.layout = _pipelineLayout,
				.renderPass = _renderPass,
				.subpass = 0,
			});
			switch ( result )
			{
				case vk::Result::eSuccess: break;
				case vk::Result::ePipelineCompileRequiredEXT:
					// something meaningfull here
					break;
				default: assert( false );  // should never happen
			}
		}

		// framebuffers
		_frameBuffers.resize(_swapchainImageViews.size());
		std::transform(_swapchainImageViews.begin(), _swapchainImageViews.end(), _frameBuffers.begin(),
			[this](const auto& view)
			{
				return _device.createFramebuffer({
					.renderPass = _renderPass,
					.attachmentCount = 1,
					.pAttachments = &view,
					.width = _swapchainExtent.width,
					.height = _swapchainExtent.height,
					.layers = 1,
				});
			});

		// ubo
		{
			_blankViewUboBuffer = _device.createBuffer({
				.size = sizeof(ViewportSet::BlankUboData),
				.usage = vk::BufferUsageFlagBits::eUniformBuffer,
				.sharingMode = vk::SharingMode::eExclusive,
			});

			vk::MemoryRequirements memRequirements = _device.getBufferMemoryRequirements(_blankViewUboBuffer);
			_blankViewUboMemory = _device.allocateMemory({
				.allocationSize = memRequirements.size,
				.memoryTypeIndex = findMemoryType(
					_physicalDevice,
					memRequirements.memoryTypeBits,
					vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
			});

			_device.bindBufferMemory(_blankViewUboBuffer, _blankViewUboMemory, 0);
		}

		// descriptor set
		{
			const auto poolSize = vk::DescriptorPoolSize
			{
				.descriptorCount = 1, // num of buffers
			};
			_descriptorPool = _device.createDescriptorPool({
				.maxSets = 1,
				.poolSizeCount = 1,
				.pPoolSizes = &poolSize,
			});

			_descriptorSets = _device.allocateDescriptorSets({
				.descriptorPool = _descriptorPool,
				.descriptorSetCount = 1,
				.pSetLayouts = &_descriptorSetLayout,
			});
		}

		createViewports();

		return true;
	}

	void createViewports()
	{
		_viewports.add({0, 0, 0.5, 1, float4{0.4, 0.6, 0.2, 1.0}});
		_viewports.add({0.5, 0, 0.5, 1, float4{0.2, 0.6, 0.8, 1.0}});

		const auto uboData = _viewports.getBlankUboData();
		constexpr auto size = sizeof(ViewportSet::BlankUboData);
		void* data = _device.mapMemory(_blankViewUboMemory, 0, size);
		memcpy(data, &uboData, size);
		_device.unmapMemory(_blankViewUboMemory);

		const auto bufferInfo = vk::DescriptorBufferInfo
		{
			.buffer = _blankViewUboBuffer,
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
		_device.updateDescriptorSets(1, &write, 0, nullptr);

		// for each video
		{
			// video buffer
			const auto h265ProfileInfo = vk::VideoDecodeH265ProfileInfoKHR{
				.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN_10,
			};
			const auto h264ProfileInfo = vk::VideoDecodeH264ProfileInfoKHR{
				.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_BASELINE,
			};
			const auto h265profile = vk::VideoProfileInfoKHR{
				.pNext = &h265ProfileInfo,
				.videoCodecOperation = vk::VideoCodecOperationFlagBitsKHR::eDecodeH265,
				.chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
				.lumaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
				.chromaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
			};
			const auto h264profile = vk::VideoProfileInfoKHR{
				.pNext = &h264ProfileInfo,
				.videoCodecOperation = vk::VideoCodecOperationFlagBitsKHR::eDecodeH264,
				.chromaSubsampling = vk::VideoChromaSubsamplingFlagBitsKHR::e420,
				.lumaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
				.chromaBitDepth = vk::VideoComponentBitDepthFlagBitsKHR::e8,
			};

			const auto list = vk::VideoProfileListInfoKHR{
				.profileCount = 1,
				.pProfiles = &h264profile,
			};

			_videoBuffer = _device.createBuffer({
				.pNext = &list,
				.size = 1000000, // to be determined reasonably
				.usage = vk::BufferUsageFlagBits::eTransferSrc
					| vk::BufferUsageFlagBits::eVideoDecodeSrcKHR,
			}, nullptr, _loader);

			vk::MemoryRequirements requirements = _device.getBufferMemoryRequirements(_videoBuffer, _loader);
			_videoBufferMemory = _device.allocateMemory({
				.allocationSize = requirements.size,
				.memoryTypeIndex = findMemoryType(
					_physicalDevice,
					requirements.memoryTypeBits,
					vk::MemoryPropertyFlagBits::eHostCoherent)
			}, nullptr, _loader);

			const auto h265Prop = vk::ExtensionProperties{
				.extensionName = vk::ArrayWrapper1D<char, 256>(std::string_view(VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME)),
				.specVersion = 1,
			};

			const auto h264Prop = vk::ExtensionProperties{
				.extensionName = vk::ArrayWrapper1D<char, 256>(std::string_view(VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME)),
				.specVersion = 1,
			};

			const auto formats = _physicalDevice.getVideoFormatPropertiesKHR({
				.pNext = &list,
				.imageUsage = vk::ImageUsageFlagBits::eVideoDecodeDstKHR,
			}, _loader);

			{
				const auto len = formats.size();
				util::println("formats {}", len);
				for(const auto f : formats)
				{
					const auto n = static_cast<uint32_t>(f.format);
					util::println("  {}", n);
				}
			}

			_videoFormat = formats.front().format;

			_videoSession = _device.createVideoSessionKHR({
				.queueFamilyIndex = _queueFamilyIndex,
				.pVideoProfile = &h264profile,
				.pictureFormat = _videoFormat,
				.maxCodedExtent = {3840, 2160},
				.pStdHeaderVersion = &h264Prop,
			}, nullptr, _loader);

			const auto filepath = "Z:\\rendered\\C\\アイカツスターズ！_97_Bon Bon Voyage!_20180308_1080_1.mp4";
			_media.setup(filepath);
			_videoStreamIndex = _media.findStreams([](const auto& s){
				return s.codecpar->codec_type == AVMEDIA_TYPE_VIDEO;
			}).front();
		}
	}

	void recordCommand(const vk::CommandBuffer& commandBuffer, const uint32_t currentIndex)
	{
		commandBuffer.begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlags()});

		commandBuffer.beginRenderPass({
			.renderPass = _renderPass,
			.framebuffer = _frameBuffers[currentIndex],
			.renderArea = {
				.offset = {0, 0},
				.extent = _swapchainExtent,
			},
		}, vk::SubpassContents::eInline);

		commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, _pipeline);
		commandBuffer.bindDescriptorSets(
			vk::PipelineBindPoint::eGraphics,
			_pipelineLayout,
			0,
			1, _descriptorSets.data(),
			0, nullptr);

		for(uint32_t i=0; i<_viewports.size(); i++)
		{
			const auto pushConstant = PushConstant
			{
				.viewportIndex = i
			};
			const auto& vp = _viewports.get(i, _swapchainExtent);
			commandBuffer.setViewport(0, vp.viewport);
			commandBuffer.setScissor(0, vp.scissor);
			commandBuffer.pushConstants(_pipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(PushConstant), &pushConstant);
			commandBuffer.draw(9, 1, 0, 0);
		}

		commandBuffer.endRenderPass();

		commandBuffer.end();
	}

	void draw()
	{
		const auto& commandBuffer = _commandBuffers.front();
		const auto currentIndex = _device.acquireNextImageKHR(_swapchain, std::numeric_limits<uint64_t>::max(), _imageAquiredSemaphore, nullptr).value;
		_device.resetFences(_drawFence);

		recordCommand(commandBuffer, currentIndex);

		const auto waitDestinationStageMask = vk::PipelineStageFlags
		{
			vk::PipelineStageFlagBits::eColorAttachmentOutput,
		};
		_grpahicsQueue.submit({{
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &_imageAquiredSemaphore,
			.pWaitDstStageMask = &waitDestinationStageMask,
			.commandBufferCount = 1,
			.pCommandBuffers = &commandBuffer,
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &_renderFinishedSemaphore,
		}}, _drawFence);

		while(vk::Result::eTimeout == _device.waitForFences({_drawFence}, true, std::numeric_limits<uint64_t>::max()))
			;

		const auto result = _grpahicsQueue.presentKHR({
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &_renderFinishedSemaphore,
			.swapchainCount = 1,
			.pSwapchains = &_swapchain,
			.pImageIndices = &currentIndex,
		});
		switch ( result )
		{
			case vk::Result::eSuccess: break;
			case vk::Result::eSuboptimalKHR: std::cout << "vk::Queue::presentKHR returned vk::Result::eSuboptimalKHR !\n"; break;
			default: assert( false );  // an unexpected result is returned !
		}
	}

	void waitIdle()
	{
		_device.waitIdle();
	}

	void destroy()
	{
		_device.freeMemory(_videoBufferMemory);
		_device.destroyBuffer(_videoBuffer);

		_device.freeMemory(_blankViewUboMemory);
		_device.destroyBuffer(_blankViewUboBuffer);

		for(const auto& b : _frameBuffers)
		{
			_device.destroyFramebuffer(b);
		}

		_device.destroyPipeline(_pipeline);
		_device.destroyPipelineLayout(_pipelineLayout);

		_device.destroyRenderPass(_renderPass);

		_device.destroyShaderModule(_vertShaderModule);
		_device.destroyShaderModule(_fragShaderModule);

		_device.destroySemaphore(_imageAquiredSemaphore);
		_device.destroySemaphore(_renderFinishedSemaphore);
		_device.destroyFence(_drawFence);

		for(auto& view : _swapchainImageViews)
		{
			_device.destroyImageView(view, nullptr);
		}
		_device.destroySwapchainKHR(_swapchain);

		_device.destroyDescriptorPool(_descriptorPool);
		_device.destroyDescriptorSetLayout(_descriptorSetLayout);

		_device.freeCommandBuffers(_commandPool, _commandBuffers);
		_device.destroyCommandPool(_commandPool);
		_device.destroy();

		_instance.destroySurfaceKHR(_surface);
		DestroyDebugUtilsMessengerEXT(_instance, _debugMessenger, nullptr);
		_instance.destroy();
	}

private:
	vk::Instance _instance;
	VkDebugUtilsMessengerEXT _debugMessenger;
	vk::SurfaceKHR _surface;
	vk::PhysicalDevice _physicalDevice;
	vk::Device _device;

	vk::CommandPool _commandPool;
	std::vector<vk::CommandBuffer> _commandBuffers;

	uint32_t _queueFamilyIndex;
	vk::Queue _grpahicsQueue;

	vk::SwapchainKHR _swapchain;
	vk::Format _colorFormat;
	vk::Extent2D _swapchainExtent;
	std::vector<vk::Image> _swapchainImages;
	std::vector<vk::ImageView> _swapchainImageViews;
	std::vector<vk::Framebuffer> _frameBuffers;

	ViewportSet _viewports;

	vk::Semaphore _imageAquiredSemaphore;
	vk::Semaphore _renderFinishedSemaphore;
	vk::Fence _drawFence;

	vk::DescriptorSetLayout _descriptorSetLayout;
	vk::DescriptorPool _descriptorPool;
	std::vector<vk::DescriptorSet> _descriptorSets;

	vk::PipelineLayout _pipelineLayout;
	vk::Pipeline _pipeline;

	vk::RenderPass _renderPass;

	vk::ShaderModule _vertShaderModule;
	vk::ShaderModule _fragShaderModule;

	vk::Buffer _blankViewUboBuffer;
	vk::DeviceMemory _blankViewUboMemory;

	// for each video
	vk::VideoSessionKHR _videoSession;
	vk::Buffer _videoBuffer;
	vk::DeviceMemory _videoBufferMemory;

	vk::Format _videoFormat;

	vk::DispatchLoaderDynamic _loader;
};

}}} // daia::player::pipeline
