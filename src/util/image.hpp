#pragma once

#include <cstdint>
#include <cwchar>
#include <filesystem>
#include <tuple>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace daia{ namespace util{ namespace image{

class Image
{
private:
	uint32_t _width;
	uint32_t _height;
	std::vector<uint8_t> _pixels;

public:
	const std::tuple<const uint32_t&, const uint32_t&> size() const
	{
		return std::make_tuple(_width, _height);
	}

	const uint32_t& width() const
	{
		return _width;
	}

	const uint32_t& height() const
	{
		return _height;
	}

	const size_t length() const
	{
		return _pixels.size();
	}

	const uint8_t* data() const
	{
		return _pixels.data();
	}

	std::vector<uint8_t>& pixels()
	{
		return _pixels;
	}

	void load(const std::filesystem::path& path)
	{
		int w, h;
		int ch = 4;
		uint8_t* pixels = stbi_load(path.string().c_str(), &w, &h, nullptr, ch);

		_width = w;
		_height = h;
		_pixels.resize(w*h*ch);
		memcpy(_pixels.data(), pixels, w*h*4);

		stbi_image_free(pixels);
	}
};

Image fromFile(const std::filesystem::path& path)
{
	auto ret = Image();
	ret.load(path);
	return ret;
}

}}} // daia::util::image