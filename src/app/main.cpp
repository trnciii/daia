#include <CLI/CLI.hpp>
#include <iostream>

#include "app.hpp"

int main(int argc, char* argv[])
{
  if (argc == 0)
  {
    return -1;
  }
  auto appPath = argv[0];

  CLI::App args{ "daia" };
  argv = args.ensure_utf8(argv);

  std::vector<std::filesystem::path> filePaths;
  args.add_option("file-paths", filePaths, "file paths");

  CLI11_PARSE(args, argc, argv);

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
