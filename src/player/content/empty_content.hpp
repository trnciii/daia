#pragma once

#include "content_base.hpp"

namespace daia { namespace player { namespace content {

class EmptyContent : public Content
{
public:
  void setup(const SetupArgs info) {}

  void destroy() {}

  util::uint2 size() const
  {
    return { _width, _height };
  }

  bool update(const UpdateArgs info)
  {
    if (_uploaded)
    {
      return false;
    }
    _uploaded = true;
    return true;
  }

  std::span<const uint32_t> data() const
  {
    return _data;
  }

  EmptyContent(uint32_t width, uint32_t height)
  {
    _width = width;
    _height = height;
    _data = std::vector<uint32_t>(_width * _height, 0xFF0000FF);
  }

private:
  uint32_t _width;
  uint32_t _height;
  std::vector<uint32_t> _data;
  bool _uploaded = false;
};

}}} // namespace daia::player::content
