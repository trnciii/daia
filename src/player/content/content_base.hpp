#pragma once

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "../../util/util.hpp"

namespace daia { namespace player { namespace content {

struct SetupArgs
{
  const vk::UniqueDevice& device;
  const vk::PhysicalDevice& physicalDevice;
};

struct UpdateArgs
{
  double time = 0;

  // ペイン系をそのうちまとめる
  float width;
  float height;
};

class Content
{
public:
  virtual ~Content() = default;
  virtual void setup(const SetupArgs info) = 0;
  virtual void destroy() = 0;
  virtual bool update(const UpdateArgs info) = 0;
  virtual util::uint2 size() const = 0;
  virtual std::span<const uint32_t> data() const = 0;
};

}}} // namespace daia::player::content
