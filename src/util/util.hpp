#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace daia { namespace util {

using float2 = std::array<float, 2>;
using float3 = std::array<float, 3>;
using float4 = std::array<float, 4>;

using uint2 = std::array<uint32_t, 2>;
using uint3 = std::array<uint32_t, 3>;
using uint4 = std::array<uint32_t, 4>;

using int2 = std::array<int, 2>;
using int3 = std::array<int, 3>;
using int4 = std::array<int, 4>;

inline void print(std::string_view string)
{
  std::cout << string;
}

inline void println(std::string_view string)
{
  std::cout << string << '\n';
}

inline void flush()
{
  std::cout << std::flush;
}

template <typename... Args>
std::string format(std::string_view f, Args&&... args)
{
  return std::vformat(f, std::make_format_args(args...));
}

template <typename... Args>
void print(std::string_view f, Args&&... args)
{
  print(std::vformat(f, std::make_format_args(args...)));
}

template <typename... Args>
void println(std::string_view f, Args&&... args)
{
  println(std::vformat(f, std::make_format_args(args...)));
}

template <std::ranges::range T>
void distinct(T& range)
{
  std::sort(range.begin(), range.end());
  range.erase(std::unique(range.begin(), range.end()), range.end());
}

static std::string readAllText(const std::filesystem::path& path)
{
  std::ifstream file(path.string(), std::ios::ate | std::ios::binary);
  if (!file.is_open())
  {
    throw std::runtime_error("failed to open file!");
  }
  const auto size = file.tellg();
  std::string text(size, '\0');
  file.seekg(0);
  file.read(text.data(), size);
  file.close();
  return text;
}

}} // namespace daia::util
