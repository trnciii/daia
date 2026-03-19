
find_package(glfw3 3.3 REQUIRED)
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
  libavformat
  libavcodec
  libswscale
  libavutil
)

add_library(player_core INTERFACE)

target_link_libraries(player_core
	INTERFACE
		Vulkan::Vulkan
		glfw
		PkgConfig::FFMPEG
)

target_include_directories(player_core
	INTERFACE
		${CMAKE_CURRENT_SOURCE_DIR}
		../../external/stb
)


add_custom_target(
	compile_shaders ALL
	COMMAND
		glslc
		${CMAKE_CURRENT_SOURCE_DIR}/pipeline/shader/pane.vert
		-o ${CMAKE_CURRENT_BINARY_DIR}/pipeline/shader/pane.vert.spv
	COMMAND
		glslc -fshader-stage=frag
		${CMAKE_CURRENT_SOURCE_DIR}/pipeline/shader/pane.frag
		-o ${CMAKE_CURRENT_BINARY_DIR}/pipeline/shader/pane.frag.spv
	VERBATIM)

# on configuration
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/pipeline/shader)
