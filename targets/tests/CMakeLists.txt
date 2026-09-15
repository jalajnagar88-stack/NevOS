# NEVOS host test suite. Links no RTOS, opens no window, sleeps nowhere.
#   cmake -S targets/tests -B build/tests -GNinja && ctest --test-dir build/tests
cmake_minimum_required(VERSION 3.20)
project(nevos_tests C)

option(NEVOS_WERROR "Treat warnings as errors" ON)

get_filename_component(NEVOS_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. ABSOLUTE)
include(${NEVOS_ROOT}/cmake/nevos_host.cmake)

if(NOT EXISTS ${NEVOS_ROOT}/third_party/unity/src/unity.c)
  message(FATAL_ERROR "Unity is missing. Run: git submodule update --init --recursive")
endif()

add_library(unity STATIC ${NEVOS_ROOT}/third_party/unity/src/unity.c)
target_include_directories(unity PUBLIC ${NEVOS_ROOT}/third_party/unity/src)
target_compile_definitions(unity PUBLIC UNITY_INCLUDE_DOUBLE UNITY_OUTPUT_COLOR)

enable_testing()

foreach(src ${NEV_KERNEL_TEST_SRCS})
  get_filename_component(name ${src} NAME_WE)
  add_executable(${name} ${src})
  target_link_libraries(${name} PRIVATE nevos_core unity)
  set_target_properties(${name} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endforeach()
