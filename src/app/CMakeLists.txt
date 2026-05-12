add_executable(app main.cpp)

set_target_properties(app PROPERTIES
  OUTPUT_NAME daia
)

target_include_directories(app
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ../../external/cli11/include
)

target_link_libraries(app
  PRIVATE
    player
)

add_dependencies(app compile_shaders)

add_custom_command(
  TARGET app POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy
    ${CMAKE_CURRENT_SOURCE_DIR}/icon.png
    $<TARGET_FILE_DIR:app>/icon.png
)
