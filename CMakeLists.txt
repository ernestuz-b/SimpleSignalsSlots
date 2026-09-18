cmake_minimum_required(VERSION 3.20)
project(SimpleSignalsSlots LANGUAGES CXX)

add_library(simple_signals_slots INTERFACE)
target_include_directories(simple_signals_slots INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_compile_features(simple_signals_slots INTERFACE cxx_std_20)

option(SIMPLE_SIGNALS_BUILD_TESTS "Build SimpleSignalsSlots tests" ON)
option(SIMPLE_SIGNALS_BUILD_EXAMPLES "Build SimpleSignalsSlots examples" ON)

if(SIMPLE_SIGNALS_BUILD_TESTS)
    enable_testing()
    add_executable(simple_signals_tests tests/test_signals.cpp)
    target_link_libraries(simple_signals_tests PRIVATE simple_signals_slots)
    add_test(NAME simple_signals_tests COMMAND simple_signals_tests)
endif()

if(SIMPLE_SIGNALS_BUILD_EXAMPLES)
    add_executable(simple_signals_example examples/example.cpp)
    target_link_libraries(simple_signals_example PRIVATE simple_signals_slots)
endif()
