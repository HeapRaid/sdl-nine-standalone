cmake_minimum_required(VERSION 3.11)

project(sdl-nine LANGUAGES C CXX)

option(NINE_DRI2_BACKEND "Enable DRI2 support" ON)
option(NINE_BUILD_SAMPLE "Build sample application" ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(D3D REQUIRED d3d)

find_package(ECM REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

find_package(SDL2 REQUIRED)
find_package(X11 REQUIRED)
find_package(X11_XCB REQUIRED)

if (NINE_DRI2_BACKEND)
    find_package(OpenGL REQUIRED)
    find_package(EGL REQUIRED)
    find_package(XCB REQUIRED PRESENT XFIXES DRI3 DRI2)
else()
    find_package(XCB REQUIRED PRESENT XFIXES DRI3)
endif()

add_subdirectory(common)
add_subdirectory(d3d9-nine)

if (NINE_BUILD_SAMPLE)
    add_executable(sdl-nine main.cpp)

    target_include_directories(sdl-nine PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(sdl-nine d3d9-nine ${SDL2_LIBRARIES})
endif()
