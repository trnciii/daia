#include <iostream>

#include "app.hpp"

int main(int argc, char* argv[])
{
  if (argc == 0)
  {
    return -1;
  }

  auto appPath = argv[0];
  std::vector<std::string> filePaths;
  for (int i = 1; i < argc; i++)
  {
    filePaths.push_back(argv[i]);
  }

  try
  {
    auto app = daia::app::App(appPath);
    app.run(filePaths);
  } catch (const vk::SystemError& e)
  {
    std::cout << "vk::SystemError: " << e.what() << std::endl;
    return -1;
  } catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return -1;
  }

  return 0;
}
