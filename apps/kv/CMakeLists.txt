# add_definitions(SUBDIRECTORY=1) TODO: Why not work?

file(GLOB SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
add_library(kvapp STATIC
${SRC_FILE}
)
target_include_directories(kvapp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(kvapp PUBLIC SUBDIRECTORY=1)