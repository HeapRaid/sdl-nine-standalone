project(common LANGUAGES C)

set(SOURCE_FILES
    compiler.h
    debug.h
    debug.c
    library.h
    library.c
)

add_library(common-nine STATIC ${SOURCE_FILES})
target_include_directories(common-nine PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(common-nine dl)
