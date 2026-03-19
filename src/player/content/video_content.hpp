#pragma once

#include "../media/video.hpp"
#include "content_base.hpp"
#include <vulkan/vulkan_core.h>

namespace daia { namespace player { namespace content {

class VideoContent : public Content
{
public:
  const std::string type_name() const
  {
    return util::get_type_name<VideoContent>();
  }

  void setup(const SetupArgs info)
  {
    _isPlaying = true;
  }

  void destroy() {}

  util::uint2 size() const
  {
    return { static_cast<uint32_t>(_video.width()), static_cast<uint32_t>(_video.height()) };
  }

  bool update(const UpdateArgs info)
  {
    if (!_isPlaying)
    {
      return false;
    }

    auto result = _video.get_frame(0, _frame);
    if (result == media::Video::FrameStatus::Success)
    {
      return true;
    }
    if (result == media::Video::FrameStatus::Eof)
    {
      _isPlaying = false;
    }
    return _isPlaying;
  }

  std::span<const uint32_t> buffer() const
  {
    return _frame;
  }

  VideoContent(const std::filesystem::path& path)
  {
    filePath = path;
    _video.setup(path);
    auto size = _video.width() * _video.height();
    _frame.resize(size);
  }

private:
  std::filesystem::path filePath;
  media::Video _video;
  std::vector<uint32_t> _frame;
  bool _isPlaying;
};

}}} // namespace daia::player::content
