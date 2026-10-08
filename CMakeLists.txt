cmake_minimum_required(VERSION 3.20)

project(beyond_one_machine_workload LANGUAGES CXX)

add_library(bom_workload src/workload.cpp)
target_include_directories(bom_workload PUBLIC include)
target_compile_features(bom_workload PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(bom_workload PRIVATE /W4)
else()
  target_compile_options(bom_workload PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()

enable_testing()
add_executable(workload_test tests/workload_test.cpp)
target_link_libraries(workload_test PRIVATE bom_workload)
target_compile_features(workload_test PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(workload_test PRIVATE /W4)
else()
  target_compile_options(workload_test PRIVATE -O2 -Wall -Wextra -Wpedantic)
endif()
add_test(NAME workload_test COMMAND workload_test)
