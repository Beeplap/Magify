cmake_minimum_required(VERSION 3.20)
project(Magify LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

set(SOURCES
    src/main.c
    src/shell/mg_shell.c
    src/ui/mg_ui.cpp
    src/image/mg_image.c
    src/pdf/mg_pdf.c
    src/util/mg_arena.c
    third_party/miniz.c
    resources/resource.rc
)

add_executable(magify WIN32 ${SOURCES})

target_compile_definitions(magify PRIVATE
    UNICODE
    _UNICODE
    _CRT_SECURE_NO_WARNINGS
)

target_link_libraries(magify PRIVATE
    user32
    gdi32
    gdiplus
    msimg32
    shell32
    ole32
    shlwapi
    advapi32
    comdlg32
)

set_target_properties(magify PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/bin"
)
