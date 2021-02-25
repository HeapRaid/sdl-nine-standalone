project(d3d9-nine LANGUAGES C)

set(SOURCE_FILES
    backend.h
    backend.c
    d3d9_sdl.c
    d3dadapter9.h
    d3dadapter9.c
    dri3.c
    present.h
    present.c
    shader_validator.h
    shader_validator.c
    xcb_present.h
    xcb_present.c
)

if (NINE_DRI2_BACKEND)
    add_definitions(-DD3D9NINE_DRI2)
    list(APPEND SOURCE_FILES dri2.c)
endif()

add_library(d3d9-nine STATIC ${SOURCE_FILES})
target_include_directories(d3d9-nine
    PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${D3D_INCLUDE_DIRS}
    PRIVATE
    ${X11_INCLUDE_DIR}
    ${X11_XCB_INCLUDE_DIR}
    ${XCB_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
)
if (NINE_DRI2_BACKEND)
    target_include_directories(d3d9-nine PRIVATE ${EGL_INCLUDE_DIRS} ${OPENGL_INCLUDE_DIR})
endif()

target_link_libraries(d3d9-nine
    common-nine
    ${X11_LIBRARIES}
    ${X11_XCB_LIBRARIES}
    ${XCB_LIBRARIES}
    ${SDL2_LIBRARIES}
)
