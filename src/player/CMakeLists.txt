
find_package(glfw3 3.3 REQUIRED)
find_package(Vulkan REQUIRED)

add_executable(daia main.cpp)

target_link_libraries(daia
	Vulkan::Vulkan
	glfw
)

target_include_directories(daia
	PRIVATE
		../../external/stb
)


add_custom_target(
	compile_shaders ALL
	COMMAND
		glslc
		${CMAKE_CURRENT_SOURCE_DIR}/shader/triangle.vert
		-o ${CMAKE_CURRENT_BINARY_DIR}/shader/triangle.vert.spv
	COMMAND
		glslc -fshader-stage=frag
		${CMAKE_CURRENT_SOURCE_DIR}/shader/triangle.frag
		-o ${CMAKE_CURRENT_BINARY_DIR}/shader/triangle.frag.spv
	VERBATIM)

# on configuration
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shader)

# post build
add_custom_command(
	TARGET daia POST_BUILD
	COMMAND
		${CMAKE_COMMAND} -E copy
		${CMAKE_CURRENT_SOURCE_DIR}/icon.png
		${CMAKE_CURRENT_BINARY_DIR}/icon.png)
