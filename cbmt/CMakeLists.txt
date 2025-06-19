
file(GLOB SRC_FILE 
${CMAKE_CURRENT_SOURCE_DIR}/*.cc
${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-alloc.cpp
${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-config.cpp
${CMAKE_CURRENT_SOURCE_DIR}/dbcore/size-encode.cpp
${CMAKE_CURRENT_SOURCE_DIR}/dbcore/epoch.cpp
${CMAKE_CURRENT_SOURCE_DIR}/dbcore/mcs_lock.cpp
${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-coroutine.cpp


# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-thread.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/rcu.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/sm-log.cpp
# ${CMAKE_CURRENT_SOURCE_DIR}/dbcore/*.cpp


)

# target_include_directories()

add_library(cbmt STATIC
${SRC_FILE}
)
target_link_libraries(cbmt gflags glog gtest numa)
target_include_directories(cbmt PUBLIC 
${CMAKE_CURRENT_SOURCE_DIR}/operation
${CMAKE_CURRENT_SOURCE_DIR}
${CMAKE_CURRENT_SOURCE_DIR}/struct
${CMAKE_CURRENT_SOURCE_DIR}../tools
${CMAKE_CURRENT_SOURCE_DIR}../third_party/sparsehash/src
)

if(DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/config-debug.h")
    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${CMAKE_CURRENT_SOURCE_DIR}/config-release.h")
    # set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
endif()

set_target_properties(cbmt PROPERTIES COMPILE_FLAGS "-DADV_COROUTINE")