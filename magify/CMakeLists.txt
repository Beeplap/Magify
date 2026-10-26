cmake_minimum_required(VERSION 3.16)
project(Magify C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include_directories(include third_party)

set(SOURCES
    src/main.c
    src/ipc.c
    src/registry.c
    src/com_extension.c
    src/ui_overlay.c
    src/ops_common.c
    src/ops_image.c
    src/ops_pdf.c
    assets/magify.rc
)

add_executable(magify WIN32 ${SOURCES})

target_link_libraries(magify PRIVATE
    gdi32
    user32
    shell32
    ole32
    uuid
    shlwapi
    dwmapi
    comctl32
    comdlg32
    advapi32
    gdiplus
)
