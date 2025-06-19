file(GLOB SRC_FILE 
${CMAKE_CURRENT_SOURCE_DIR}/*.cc 
${CMAKE_CURRENT_SOURCE_DIR}/operation/*.cc
)

add_library(ljrdma STATIC
${SRC_FILE}
)
target_link_libraries(ljrdma ibverbs memcached boost_system boost_coroutine)
target_include_directories(ljrdma PUBLIC 
${CMAKE_CURRENT_SOURCE_DIR}/operation
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_CURRENT_SOURCE_DIR}/struct
${CMAKE_CURRENT_SOURCE_DIR}../tools
)