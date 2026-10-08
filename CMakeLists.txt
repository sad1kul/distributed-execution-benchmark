cmake_minimum_required(VERSION 3.20)

project(beyond_one_machine_workload LANGUAGES CXX)

find_package(Threads REQUIRED)

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
