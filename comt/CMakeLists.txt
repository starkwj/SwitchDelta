
add_definitions("-DADV_COROUTINE")
file(GLOB SRC_FILE 
${CMAKE_CURRENT_SOURCE_DIR}/*.cc
${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-alloc.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-config.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/size-encode.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/epoch.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/mcs_lock.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-coroutine.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-thread.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/rcu.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-log.cpp
)

# target_include_directories()

add_library(comt STATIC
${SRC_FILE}
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/config.h")


# set_target_properties(cbmt PROPERTIES COMPILE_FLAGS "-DADV_COROUTINE")
target_link_libraries(comt gflags glog gtest numa)
target_include_directories(comt PUBLIC 
${CMAKE_CURRENT_SOURCE_DIR}/operation
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_CURRENT_SOURCE_DIR}/struct
${CMAKE_CURRENT_SOURCE_DIR}../tools
${CMAKE_CURRENT_SOURCE_DIR}../third_party/sparsehash/src
)

