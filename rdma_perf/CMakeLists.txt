file(GLOB SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)
add_library(rdma_perf STATIC
${SRC_FILE}
)
target_link_libraries(rdma_perf ljrdma memcached)
target_include_directories(rdma_perf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} {CMAKE_CURRENT_SOURCE_DIR}/../tools) 