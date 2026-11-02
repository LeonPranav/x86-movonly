cmake_minimum_required(VERSION 3.16)
project(x86_movonly CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(x86movonly
  src/main.cpp
  src/parser.cpp
  src/rewrite.cpp
)
target_include_directories(x86movonly PRIVATE include)

install(TARGETS x86movonly RUNTIME DESTINATION bin)

enable_testing()
add_test(NAME translate_run_movcheck
  COMMAND bash ${CMAKE_SOURCE_DIR}/tests/run_all.sh)
set_tests_properties(translate_run_movcheck PROPERTIES
  ENVIRONMENT "X86MOVONLY=$<TARGET_FILE:x86movonly>")
