#pragma once

#include <filesystem>
#include <iostream>
#include <tuple>

#include "../player/content/content.hpp"
#include "../player/pipeline/pipeline.hpp"
#include "../player/window.hpp"

namespace daia { namespace app {

class App
{
public:
  App(std::filesystem::path appPath)
  {
    _appRoot = appPath.parent_path();
  }

  void run(const std::vector<std::filesystem::path>& filePath)
  {
    _setup(filePath);

    while (!_window.shouldClose())
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

  player::Window _window;
  player::pipeline::Pipeline _pipeline;

  std::chrono::steady_clock::time_point _startTime = std::chrono::steady_clock::now();

  void _setup(const std::vector<std::filesystem::path>& filePaths)
  {
    // set app name, width, height

    _setupWindow();
    _setupPipeline(_window.getRequiredInstanceExtensions());

    if (filePaths.empty())
    {
      _pipeline.registerContent("default", std::make_shared<player::content::EmptyContent>(_width, _height));
    }
    else
    {
      for (const auto& path : filePaths)
      {
        _pipeline.registerContent(path.string(), std::make_shared<player::content::VideoContent>(path));
      }
    }
  }

  void _setupWindow()
  {
    _window = player::Window();

    auto setupInfo = player::Window::SetupInfo{
      .width = _width,
      .height = _height,
      .title = _appName,
      .icons = {
        daia::util::fromFile(_appRoot / "icon.png"),
      },
      .position = std::make_tuple(-1500, 800),
    };

    if (!_window.setup(setupInfo))
    {
      std::cout << "failed to setup main window" << std::endl;
    }
  }

  void _setupPipeline(const std::vector<const char*>& extensions)
  {
    auto info = player::pipeline::SetupArgs{
      .appRoot = _appRoot,
      .appName = _appName,
      .width = _width,
      .height = _height,
      .instanceExtensions = extensions,
      .enableValidationLayers = true,
      .window = _window,
    };

    if (!_pipeline.setup(info))
    {
      util::println("failed to setup pipeline");
    }
  }

  void _update()
  {
    _window.poll();
    const auto globalTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - _startTime).count() / 1000.0f;
    _pipeline.update(globalTime);
  }

  void _draw()
  {
    _pipeline.draw();
  }

  void _exit()
  {
    _pipeline.destroy();
    _window.close();
    player::Window::terminate();
  }
};

}} // namespace daia::app
