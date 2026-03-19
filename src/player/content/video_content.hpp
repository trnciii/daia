#pragma once

#include "../media/video.hpp"
#include "content_base.hpp"

namespace daia { namespace player { namespace content {

class VideoContent : public Content
{
public:
  void setup(const SetupArgs info) {}

  void destroy() {}

  util::uint2 size() const
  {
    return { static_cast<uint32_t>(_video.width()), static_cast<uint32_t>(_video.height()) };
  }

  bool update(const UpdateArgs info)
  {
    _frame = _video.getFrame(0);
    return true;
  }

  std::span<const uint32_t> data() const
  {
    return _frame;
  }

  VideoContent(const std::filesystem::path& path)
  {
    filePath = path;
    _video.setup(path);
  }

private:
  std::filesystem::path filePath;
  media::Video _video;
  std::vector<uint32_t> _frame;
};

}}} // namespace daia::player::content
