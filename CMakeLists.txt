cmake_minimum_required(VERSION 3.1)
project(NetStore)

set(CMAKE_CXX_COMPILER g++-10)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -pthread -mcmodel=large")
set(CMAKE_EXPORT_COMPILE_COMMANDS True)

set(CMAKE_VERBOSE_MAKEFILEON ON)

find_library (mtree masstree "./third_party/masstree-beta/")
message(STATUS "masstree:" ${mtree})
# Common LIB
set(LIBRARIES ${LIBRARIES} gflags glog gtest)
include_directories(tools)

find_package(GTest REQUIRED)
enable_testing()

# disable boost warning
add_definitions(-DBOOST_COROUTINES_NO_DEPRECATION_WARNING -DCMAKE_EXPORT_COMPILE_COMMANDS=1)

#===============================================================================
# 1.Options exposed to the user
#===============================================================================
option(PERF "Compile for performance" ON)
option(ONCHIP "On-chip mem" ON)

if(PERF)
  message(STATUS "Compilation optimized for performance.")
  set(CMAKE_BUILD_TYPE Release)
  set(DEBUG OFF)
  set(TESTING OFF)
else(PERF)
  message(STATUS "Compilation not optimized for performance.")
  add_definitions(-DLOGDEBUG)
  set(CMAKE_BUILD_TYPE Debug)
  set(DEBUG ON)
  set(TESTING ON)
endif(PERF)

if(DEBUG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O0 -g")
else(DEBUG)
  add_compile_definitions(NDEBUG)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif(DEBUG)

if(ONCHIP)
  message(STATUS "On-chip mem")
  add_definitions(-DONCHIP_OK)
else(ONCHIP)
  message(STATUS "Without on-chip mem")
endif(ONCHIP)

#Options end
#===============================================================================
# 2.The app to compile. Only one app is compiled to reduce compile time. [from eRPC]
#===============================================================================
if (DEFINED APPNAME)
  set(APP ${APPNAME})
else(DEFINED APPNAME)
if(EXISTS "${CMAKE_SOURCE_DIR}/scripts/autorun_app_file")
  file(STRINGS "scripts/autorun_app_file" APP)
else()
  message(STATUS "No autorun_app_file found. No application will be compiled.")
  set(APP "cmake_example")
endif()
endif(DEFINED APPNAME)
message(STATUS "Compiling app = " ${APP})

if(APP STREQUAL "cmake_example")
  add_subdirectory(apps/${APP})
  set(LIBRARIES ${LIBRARIES} cmake_example) 
  message("use cmake_exmaple directory ok")
elseif(APP STREQUAL "rdma_rw_lat") # TODO:
  add_subdirectory(rdma)
  add_subdirectory(rdma_perf)
  set(LIBRARIES ${LIBRARIES} rdma_perf)
elseif(APP STREQUAL "rdma_example")
  add_subdirectory(rdma)
  set(LIBRARIES ${LIBRARIES} ljrdma memcached)
elseif(APP STREQUAL "rpc_example")
  include_directories("./third_party/libcuckoo/install/include/")
  add_subdirectory(rdma)
  add_subdirectory(rpc)
  set(LIBRARIES ${LIBRARIES} rpc profiler)
elseif(APP STREQUAL "juneberry")
  # include_directories("./third_party/libcuckoo/install/include/")
  add_subdirectory(apps/${APP})
  add_subdirectory(rdma)
  add_subdirectory(rpc)
  set(LIBRARIES ${LIBRARIES} juneberry)
elseif(APP STREQUAL "kv")
  add_subdirectory(apps/${APP})
  add_subdirectory(rdma)
  add_subdirectory(rpc)
  set(LIBRARIES_OBATCH ${LIBRARIES} rpc ${mtree})
  include_directories("./third_party/libcuckoo/install/include/")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -fcoroutines -Woverloaded-virtual -Werror=reorder -Werror=unused-result -Wno-address-of-packed-member")
  set(CMAKE_CXX_FLAGS_OBATCH "-I${CMAKE_CURRENT_SOURCE_DIR}/third_party/masstree-beta/")
  # include_directories("")

  file(GLOB APP_SRC apps/${APP}/*.cc)
  foreach (APP_FILE ${APP_SRC})
  # if (NOT APP_FILE STREQUAL "index_only_test.cc")
    get_filename_component(APP_FILE_NAME ${APP_FILE} NAME_WE)
    add_executable(${APP_FILE_NAME} ${APP_FILE} ${APP_ADDITIONAL_SOURCES} ${SOURCES})
    target_compile_options(${APP_FILE_NAME} PRIVATE
      SHELL:${CMAKE_CXX_FLAGS_OBATCH}
      -DOBATCH
    )
    target_link_libraries(${APP_FILE_NAME} ${GTEST_LIBRARIES} ${LIBRARIES_OBATCH} "-mcmodel=large")
  # endif()
  endforeach()
  
  # add_subdirectory(corobase)
  
  # add_subdirectory(third_party/corobase/masstree)
  # include_directories("./third_party/corobase/masstree/")
  # set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -fcoroutines")
  # set(LIBRARIES ${LIBRARIES} rpc)
  
  add_definitions(-DCACHELINE_SIZE=64)
  add_subdirectory(comt)
  set(CMAKE_CXX_FLAGS_COMT " -include ${CMAKE_CURRENT_SOURCE_DIR}/comt/config.h")
  #------------coroutine for index_only_test.cc---------------- 
  set(LIBRARIES_COMT ${tmp_libraries} rpc comt)
  add_executable(index_only_test_co apps/${APP}/index_only_test.cc) 
  target_compile_options(index_only_test_co PRIVATE
    SHELL:${CMAKE_CXX_FLAGS_COMT}
    -DADV_COROUTINE
    -DCOMT
    -O3 
    # -Woverloaded-virtual 
    # -Werror=reorder 
    # -Werror=unused-result 
    # -Wno-address-of-packed-member
  )
  target_link_libraries(index_only_test_co ${GTEST_LIBRARIES} ${LIBRARIES_COMT} )
  #------------coroutine for kv.cc---------------- 
  add_executable(kv_co apps/${APP}/kv.cc) 
  target_compile_options(kv_co PRIVATE
    SHELL:${CMAKE_CXX_FLAGS_COMT}
    -DADV_COROUTINE
    -DCOMT
    -O3 
    # -Woverloaded-virtual 
    # -Werror=reorder 
    # -Werror=unused-result 
    # -Wno-address-of-packed-member
  )
  target_link_libraries(kv_co ${GTEST_LIBRARIES} ${LIBRARIES_COMT} )

elseif(APP STREQUAL "fs")
  add_subdirectory(apps/${APP})
  include_directories("./third_party/libcuckoo/install/include/")
  add_subdirectory(rdma)
  add_subdirectory(rpc)
  set(LIBRARIES ${LIBRARIES} rpc fsapp)
elseif(APP STREQUAL "fpga_test")
  add_subdirectory(apps/${APP})
elseif(APP STREQUAL "coroutine_example")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wextra -pedantic -pedantic-errors -latomic -lm -fcoroutines")
  add_definitions(-DCACHELINE_SIZE=64)
  add_definitions(-DMASSTREE)
  include_directories(third_party/sparsehash/src)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}  -fcoroutines -Woverloaded-virtual -Werror=reorder -Werror=unused-result -Wno-address-of-packed-member")
  set(LIBRARIES ${LIBRARIES} boost_system boost_coroutine)
  # example

  file(GLOB APP_SRC apps/${APP}/coroutine*.cc)
  foreach (APP_FILE ${APP_SRC})
    get_filename_component(APP_FILE_NAME ${APP_FILE} NAME_WE)
    add_executable(${APP_FILE_NAME} ${APP_FILE} ${APP_ADDITIONAL_SOURCES} ${SOURCES})
    target_link_libraries(${APP_FILE_NAME} ${GTEST_LIBRARIES} ${LIBRARIES})
  endforeach()

  set(tmp_flags "${CMAKE_CXX_FLAGS}")
  set(tmp_libraries ${LIBRARIES})
  add_definitions("-DADV_COROUTINE")
  
  # kv-cbmt
  set(MASSTREE_CONFIG "--enable-max-key-len=1024 --with-malloc=malloc")

  if(DEBUG)
    set(CMAKE_CXX_FLAGS_CBMT "-include ${CMAKE_CURRENT_SOURCE_DIR}/cbmt/config-debug.h")
    # set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g")
  else()
    set(CMAKE_CXX_FLAGS_CBMT  "-include ${CMAKE_CURRENT_SOURCE_DIR}/cbmt/config-release.h")
    # set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
  endif()

  
  
  add_subdirectory(cbmt)
  set(LIBRARIES_CBMT ${tmp_libraries} cbmt)
  add_executable(cbmt_test apps/${APP}/coro_kv.cc)
  message(STATUS ${CMAKE_CXX_FLAGS_CBMT})
  target_compile_options(cbmt_test PRIVATE
    SHELL:${CMAKE_CXX_FLAGS_CBMT}
    -DCBMT
  )
  target_link_libraries(cbmt_test ${GTEST_LIBRARIES} ${LIBRARIES_CBMT})
  
  # kv-comt
  add_subdirectory(comt)
  set(CMAKE_CXX_FLAGS_COMT "-include ${CMAKE_CURRENT_SOURCE_DIR}/comt/config.h")
  set(LIBRARIES_COMT ${tmp_libraries} comt)
  add_executable(comt_test apps/${APP}/coro_kv.cc) 
  target_compile_options(comt_test PRIVATE
    SHELL:${CMAKE_CXX_FLAGS_COMT}
    -DCOMT
  )
  target_link_libraries(comt_test ${GTEST_LIBRARIES} ${LIBRARIES_COMT})
  
elseif(APP STREQUAL "nextapp")
  add_subdirectory(apps/${APP})
endif()

#===============================================================================
# 3.Test
#===============================================================================
set(GTEST_TESTS
    # hello_test
    # perf_test
)


if(TESTING)
  foreach(test_name IN LISTS GTEST_TESTS)
    add_executable(${test_name} tests/tools/${test_name}.cc)
    target_link_libraries(${test_name} ${GTEST_LIBRARIES} ${LIBRARIES})
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()


#===============================================================================
# 4.APP
#===============================================================================

if(APP STREQUAL "coroutine_example")
  # set(APP_ADDITIONAL_SOURCES ${APP_ADDITIONAL_SOURCES} cbmt/cbmt.cc)

elseif(APP STREQUAL "kv")

else()
  file(GLOB APP_SRC apps/${APP}/*.cc)
  foreach (APP_FILE ${APP_SRC})
    get_filename_component(APP_FILE_NAME ${APP_FILE} NAME_WE)
    add_executable(${APP_FILE_NAME} ${APP_FILE} ${APP_ADDITIONAL_SOURCES} ${SOURCES})
    target_link_libraries(${APP_FILE_NAME} ${GTEST_LIBRARIES} ${LIBRARIES})
  endforeach()
endif()