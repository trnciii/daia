#pragma once

#include <cstdint>
#include <vector>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#include "../../util/util.hpp"

namespace daia { namespace player { namespace pipeline {

struct PushConstant
{
  uint32_t viewportIndex;
};

struct ViewportSource
{
  float x;
  float y;
  float width;
  float height;
  util::float4 color;
};

class ViewportSet
{
public:
  struct BlankUboData
  {
    util::float4 colors[8];
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
      .viewport = vk::Viewport{
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

  const size_t size() const
  {
    return sources.size();
  }

  const BlankUboData getBlankUboData() const
  {
    BlankUboData data;
    for (int i = 0; i < sources.size(); i++)
    {
      data.colors[i] = sources[i].color;
    }
    return data;
  }

private:
  std::vector<ViewportSource> sources;
};

}}} // namespace daia::player::pipeline
