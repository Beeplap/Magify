cmake_minimum_required(VERSION 3.20)
project(Magify LANGUAGES C)

# Use the same maintained C11 application as Makefile and setup.bat.
add_subdirectory(magify)
set_target_properties(magify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
