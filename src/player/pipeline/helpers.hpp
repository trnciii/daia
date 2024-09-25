#pragma once

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "../../util/util.hpp"

namespace daia { namespace player { namespace pipeline {

inline std::vector<const char*> checkLayers(const std::vector<const char*>& requestedLayers)
{
  const auto availableLayers = vk::enumerateInstanceLayerProperties();
  std::vector<const char*> missing;
  for (const auto& r : requestedLayers)
  {
    if (availableLayers.end() == std::find_if(availableLayers.begin(), availableLayers.end(), [&r](const auto& a) { return std::strcmp(a.layerName, r) == 0; }))
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
  if (func != nullptr)
  {
    return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
  }
  else
  {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

inline void DestroyDebugUtilsMessengerEXT(
  VkInstance instance,
  VkDebugUtilsMessengerEXT debugMessenger,
  const VkAllocationCallbacks* pAllocator)
{
  auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
  if (func != nullptr)
  {
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

inline vk::UniqueShaderModule createShaderModule(const vk::Device& device, const std::filesystem::path& filepath)
{
  const auto code = util::readAllText(filepath);
  const auto createInfo = vk::ShaderModuleCreateInfo{
    .codeSize = code.size(),
    .pCode = reinterpret_cast<const uint32_t*>(code.data()),
  };
  return device.createShaderModuleUnique(createInfo, nullptr);
}

}}} // namespace daia::player::pipeline
