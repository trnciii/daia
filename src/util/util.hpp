#pragma once

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <utility>
#include <print>

namespace daia { namespace util {

inline void print(const std::string& string)
{
	std::cout << string;
}

inline void println(const std::string& string)
{
	std::cout << string <<std::endl;
}

inline void flush()
{
	std::cout << std::flush;
}

template<typename... Args>
void print(const std::string& f, Args&&... args)
{
	print(std::vformat(f, std::make_format_args(std::forward<Args>(args)...)));
}

template<typename... Args>
void println(const std::string& f, Args&&... args)
{
	println(std::vformat(f, std::make_format_args(std::forward<Args>(args)...)));
}

template<std::ranges::range T>
void distinct(T& range)
{
	std::sort(range.begin(), range.end());
	range.erase(std::unique(range.begin(), range.end()), range.end());
}

static std::string readAllText(const std::filesystem::path& path)
{
	std::ifstream file(path.string(), std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}
	const auto size = file.tellg();
	std::string text(size, '\0');
	file.seekg(0);
	file.read(text.data(), size);
	file.close();
	return text;
}

}} // daia::util
