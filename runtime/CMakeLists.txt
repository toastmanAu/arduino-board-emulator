cmake_minimum_required(VERSION 3.20)
project(boardghost_runtime CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BOARDGHOST_BUILD_TESTS "Build unit tests" ON)

# --- libsim_runtime: shims + runtime, no main() ---
add_library(sim_runtime STATIC
    src/sim_runtime.cpp
)

target_include_directories(sim_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/shims
)

target_compile_definitions(sim_runtime PUBLIC BOARDGHOST_SIM=1)

# sim_main is a separate object library; sketch builds link it,
# unit-test builds do not (they bring their own gtest_main).
add_library(sim_main OBJECT src/sim_main.cpp)
target_link_libraries(sim_main PUBLIC sim_runtime)

if(BOARDGHOST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
