cmake_minimum_required(VERSION 3.16)
project(esp32_minecraft_host_tests LANGUAGES C)

include(CTest)
find_package(Threads REQUIRED)

set(SERVER_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
if(NOT EXISTS "${SERVER_ROOT}/include/registries.h"
   OR NOT EXISTS "${SERVER_ROOT}/src/registries.c")
  message(FATAL_ERROR "Generate real Minecraft registries before configuring host tests.")
endif()

file(GLOB SERVER_SOURCES CONFIGURE_DEPENDS "${SERVER_ROOT}/src/*.c")
set(CORE_SOURCES ${SERVER_SOURCES})
list(REMOVE_ITEM CORE_SOURCES "${SERVER_ROOT}/src/main.c")

function(configure_host_target target)
  target_include_directories(${target} PRIVATE "${SERVER_ROOT}/include")
  target_compile_features(${target} PRIVATE c_std_11)
  target_link_libraries(${target} PRIVATE Threads::Threads m)
endfunction()

add_executable(mcserver_native ${SERVER_SOURCES})
configure_host_target(mcserver_native)

foreach(test_name IN ITEMS chunks game)
  add_executable(test_${test_name} "${SERVER_ROOT}/tests/test_${test_name}.c" ${CORE_SOURCES})
  configure_host_target(test_${test_name})
  add_test(NAME ${test_name} COMMAND test_${test_name})
  set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endforeach()
