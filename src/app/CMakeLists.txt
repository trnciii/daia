add_executable(daia main.cpp)

target_include_directories(daia
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}
		../../external/cli11/include
)


target_link_libraries(daia
  PRIVATE
    player_core
)

add_dependencies(daia compile_shaders)

add_custom_command(
  TARGET daia POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/icon.png
    $<TARGET_FILE_DIR:daia>/icon.png
)
