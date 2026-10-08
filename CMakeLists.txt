cmake_minimum_required(VERSION 3.20)

project(beyond_one_machine_workload LANGUAGES CXX)

find_package(Threads REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

function(bom_target_defaults target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -O2 -Wall -Wextra -Wpedantic)
  endif()
endfunction()

add_library(bom_workload src/workload.cpp)
target_include_directories(bom_workload PUBLIC include)
bom_target_defaults(bom_workload)

add_library(bom_protocol src/protocol.cpp)
target_include_directories(bom_protocol PUBLIC include)
target_link_libraries(bom_protocol PUBLIC bom_workload)
bom_target_defaults(bom_protocol)

add_library(bom_execution src/execution.cpp)
target_include_directories(bom_execution PUBLIC include)
target_link_libraries(bom_execution PUBLIC bom_workload)
bom_target_defaults(bom_execution)

add_library(bom_network src/socket.cpp src/remote.cpp)
target_include_directories(bom_network PUBLIC include)
target_link_libraries(bom_network PUBLIC bom_protocol bom_execution Threads::Threads)
if(WIN32)
  target_link_libraries(bom_network PUBLIC ws2_32)
endif()
bom_target_defaults(bom_network)

add_library(bom_distributed src/distributed.cpp)
target_include_directories(bom_distributed PUBLIC include)
target_link_libraries(bom_distributed PUBLIC bom_network)
bom_target_defaults(bom_distributed)

add_library(bom_benchmark src/benchmark.cpp)
target_include_directories(bom_benchmark PUBLIC include)
target_link_libraries(bom_benchmark PUBLIC bom_distributed)
bom_target_defaults(bom_benchmark)
if(MSVC)
  set(BOM_RECORDED_FLAGS "/W4")
else()
  set(BOM_RECORDED_FLAGS "-O2 -Wall -Wextra -Wpedantic")
endif()
target_compile_definitions(bom_benchmark PRIVATE
  BOM_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
  BOM_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
  BOM_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  BOM_COMPILE_FLAGS="${BOM_RECORDED_FLAGS}")

add_executable(bom_benchmark_cli src/benchmark_main.cpp)
set_target_properties(bom_benchmark_cli PROPERTIES OUTPUT_NAME bom_benchmark)
target_link_libraries(bom_benchmark_cli PRIVATE bom_benchmark)
bom_target_defaults(bom_benchmark_cli)

add_executable(bom_worker src/worker_main.cpp)
target_link_libraries(bom_worker PRIVATE bom_network)
bom_target_defaults(bom_worker)

enable_testing()
add_executable(workload_test tests/workload_test.cpp)
target_link_libraries(workload_test PRIVATE bom_workload)
bom_target_defaults(workload_test)
add_test(NAME workload_test COMMAND workload_test)

add_executable(protocol_test tests/protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE bom_protocol)
bom_target_defaults(protocol_test)
add_test(NAME protocol_test COMMAND protocol_test)

add_executable(execution_test tests/execution_test.cpp)
target_link_libraries(execution_test PRIVATE bom_execution)
bom_target_defaults(execution_test)
add_test(NAME execution_test COMMAND execution_test)

add_executable(network_test tests/network_test.cpp)
target_link_libraries(network_test PRIVATE bom_network)
bom_target_defaults(network_test)
add_test(NAME network_test COMMAND network_test)
set_tests_properties(network_test PROPERTIES TIMEOUT 30)

add_executable(distributed_test tests/distributed_test.cpp)
target_link_libraries(distributed_test PRIVATE bom_distributed)
bom_target_defaults(distributed_test)
add_test(NAME distributed_test COMMAND distributed_test)
set_tests_properties(distributed_test PROPERTIES TIMEOUT 60)

add_executable(benchmark_test tests/benchmark_test.cpp)
target_link_libraries(benchmark_test PRIVATE bom_benchmark)
bom_target_defaults(benchmark_test)
add_test(NAME benchmark_test COMMAND benchmark_test)
set_tests_properties(benchmark_test PROPERTIES TIMEOUT 30)

add_test(
  NAME smoke_test
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/smoke_test.py
          --worker $<TARGET_FILE:bom_worker>
          --benchmark $<TARGET_FILE:bom_benchmark_cli>)
set_tests_properties(smoke_test PROPERTIES TIMEOUT 60 LABELS integration)

add_test(NAME analysis_test COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/analysis_test.py)
set_tests_properties(analysis_test PROPERTIES TIMEOUT 30)

add_test(NAME benchmark_help COMMAND bom_benchmark_cli --help)
add_test(NAME worker_help COMMAND bom_worker --help)
add_test(
  NAME benchmark_rejects_zero_tasks
  COMMAND bom_benchmark_cli --mode local-1 --dimension 2 --task-count 0 --base-seed 1
          --repetitions 1 --output-dir invalid-output)
set_tests_properties(benchmark_rejects_zero_tasks PROPERTIES WILL_FAIL TRUE)
add_test(
  NAME benchmark_rejects_zero_repetitions
  COMMAND bom_benchmark_cli --mode local-1 --dimension 2 --task-count 1 --base-seed 1
          --repetitions 0 --output-dir invalid-output)
set_tests_properties(benchmark_rejects_zero_repetitions PROPERTIES WILL_FAIL TRUE)
add_test(
  NAME benchmark_rejects_invalid_mode
  COMMAND bom_benchmark_cli --mode invalid --dimension 2 --task-count 1 --base-seed 1
          --repetitions 1 --output-dir invalid-output)
set_tests_properties(benchmark_rejects_invalid_mode PROPERTIES WILL_FAIL TRUE)
