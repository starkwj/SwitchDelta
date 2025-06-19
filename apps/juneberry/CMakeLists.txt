file(GLOB SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
add_library(juneberry STATIC
${SRC_FILE}
)

target_include_directories(juneberry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(juneberry PUBLIC SUBDIRECTORY=1)

target_link_libraries(juneberry rpc profiler "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/libembedded_memcached.a" event)