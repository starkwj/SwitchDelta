# add_definitions(SUBDIRECTORY=1) TODO: Why not work?

file(GLOB SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
add_library(cmake_example STATIC
${SRC_FILE}
)
target_include_directories(cmake_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(cmake_example PUBLIC SUBDIRECTORY=1)