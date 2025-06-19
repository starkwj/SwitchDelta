file(GLOB SRC_FILE 
${CMAKE_CURRENT_SOURCE_DIR}/*.cc 
)

add_library(rpc STATIC
${SRC_FILE}
)
target_link_libraries(rpc ljrdma)
target_include_directories(rpc PUBLIC 
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_CURRENT_SOURCE_DIR}../tools
)