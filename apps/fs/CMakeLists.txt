list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# message("-------: ${CMAKE_MODULE_PATH}")
find_package(Crypto REQUIRED)
include_directories(${CRYPTO_INCLUDE_DIR})

file(GLOB SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc)
add_library(fsapp STATIC
${SRC_FILE}
)
target_include_directories(fsapp PUBLIC 
${CMAKE_CURRENT_SOURCE_DIR}/include
${CMAKE_CURRENT_SOURCE_DIR}../../tools
${CRYPTO_INCLUDE_DIR}
)

message("---: ${CRYPTO_LIBRARIES} ${CRYPTO_INCLUDE_DIR}")
target_link_libraries(fsapp rpc ${CRYPTO_LIBRARIES} ${CMAKE_DL_LIBS})


target_compile_definitions(fsapp PUBLIC SUBDIRECTORY=1)

