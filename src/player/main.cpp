#include <iostream>

#include "app.hpp"

int main(int argc, char* argv[])
{
	using namespace daia;

	if(argc == 0)
	{
		return -1;
	}

	auto appPath = argv[0];

	try
	{
		auto app = player::App(appPath);
		app.run();
	}
	catch (const vk::SystemError& e)
	{
		std::cout << "vk::SystemError: " << e.what() << std::endl;
    return -1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return -1;
	}

	return 0;
}
