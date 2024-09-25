#pragma once

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdint.h>
#include <string>
#include <tuple>
#include <vector>

#define VULKAN_HPP_NO_CONSTRUCTORS
#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "../util/image.hpp"

namespace daia{ namespace window{

struct SetupInfo
{
	uint32_t width;
	uint32_t height;
	std::string	title;
	std::vector<util::image::Image> icons;
	std::optional<std::tuple<int, int>> position;
};

class Window
{
private:
	inline static bool _initialized;

	static bool _initialize()
	{
		if(_initialized)
		{
			return true;
		}

		_initialized = glfwInit();
		return _initialized;
	}

	static void keyCallback(GLFWwindow* handle, int key, int scancode, int action, int mods)
	{
		if (action == GLFW_PRESS && key == GLFW_KEY_W && (mods & GLFW_MOD_CONTROL))
		{
			glfwSetWindowShouldClose(handle, GLFW_TRUE);
		}
	}

public:
	bool setup(const SetupInfo& info)
	{
		if(!_initialize())
		{
			return false;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

		_handle = glfwCreateWindow(info.width, info.height, info.title.c_str(), NULL, NULL);

		glfwSetKeyCallback(_handle, keyCallback);

		if(info.position.has_value())
		{
			auto& [x, y] = info.position.value();
			glfwSetWindowPos(_handle, x, y);
		}

		std::vector<GLFWimage> icons;
		std::transform(info.icons.begin(), info.icons.end(), std::back_inserter(icons),
			[](const auto& i)
			{
				return GLFWimage{
					.width = static_cast<int>(i.width()),
					.height = static_cast<int>(i.height()),
					.pixels = const_cast<uint8_t*>(i.data()),
				};
			});
		glfwSetWindowIcon(_handle, icons.size(), icons.data());

		return _handle != nullptr;
	}

	bool shouldClose()
	{
		return glfwWindowShouldClose(_handle);
	}

	void poll() const
	{
		glfwPollEvents();
	}

	void close()
	{
		glfwDestroyWindow(_handle);
	}

	static std::vector<const char*> getRequiredInstanceExtensions()
	{
		uint32_t count = 0;
		const auto extensions = glfwGetRequiredInstanceExtensions(&count);
		return std::vector(extensions, extensions + count);
	}

	static void terminate()
	{
		glfwTerminate();
	}

	vk::SurfaceKHR createSurface(const vk::Instance& instance) const
	{
		VkSurfaceKHR surface;
		auto result = glfwCreateWindowSurface(instance, _handle, nullptr, &surface);
		switch (result) {
			case VK_SUCCESS:
				break;
			case VK_ERROR_EXTENSION_NOT_PRESENT:
				std::cerr << "Error: Required Vulkan extension not present!" << std::endl;
				break;
			case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
				std::cerr << "Error: Native window is already in use!" << std::endl;
				break;
			case VK_ERROR_SURFACE_LOST_KHR:
				std::cerr << "Error: Surface lost!" << std::endl;
				break;
			case VK_ERROR_OUT_OF_HOST_MEMORY:
			case VK_ERROR_OUT_OF_DEVICE_MEMORY:
				std::cerr << "Error: Out of memory!" << std::endl;
				break;
			case VK_ERROR_INITIALIZATION_FAILED:
				std::cerr << "Error: Initialization failed!" << std::endl;
				break;
			default:
				std::cerr << "Failed to create window surface! Error code: " << result << std::endl;
				break;
		}
		return surface;
	}

private:
	GLFWwindow* _handle = nullptr;
};

}} // daia::window
