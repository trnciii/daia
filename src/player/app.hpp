#pragma once

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <iostream>
#include <tuple>

#include "window.hpp"
#include "pipeline.hpp"

namespace daia { namespace player {

class App
{
public:
	App(std::filesystem::path appPath)
	{
		_appRoot = appPath.parent_path();
	}

	void run()
	{
		_setup();

		while(!_window.shouldClose())
		{
			_update();
			_draw();
		}

		_exit();
	}

private:
	std::string _appName = "daia";
	uint32_t _width = 1024;
	uint32_t _height = 512;

	std::filesystem::path _appRoot;
	window::Window _window;
	pipeline::Pipeline _pipeline;


	void _setup()
	{
		// set app name, width, height

		_setupWindow();
		_setupPipeline(_window.getRequiredInstanceExtensions());
	}

	void _setupWindow()
	{
		_window = window::Window();

		auto setupInfo = window::SetupInfo
	 	{
			.width = _width,
			.height = _height,
			.title = _appName,
			.icons = {
				util::image::fromFile(_appRoot/"icon.png"),
			},
			.position = std::make_tuple(-1500, 800),
		};

		if(!_window.setup(setupInfo))
		{
			std::cout << "failed to setup main window" << std::endl;
		}
	}

	void _setupPipeline(const std::vector<const char*>& extensions)
	{
		auto info = pipeline::SetupInfo{
			.appRoot = _appRoot,
			.appName = _appName,
			.width = _width,
			.height = _height,
			.instanceExtensions = extensions,
			.enableValidationLayers = true,
			.window = _window,
		};

		if(!_pipeline.setup(info))
		{
			util::println("failed to setup pipeline");
		}
	}

	void _update()
	{
		_window.poll();
	}

	void _draw()
	{
		_pipeline.draw();
	}

	void _exit()
	{
		_pipeline.waitIdle();
		_pipeline.destroy();
		_window.close();
		window::Window::terminate();
	}
};

}} // daia.player
