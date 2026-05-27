cmake_minimum_required(VERSION 3.20)
project(boardghost_runtime CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BOARDGHOST_BUILD_TESTS "Build unit tests" ON)

# --- libsim_runtime: shims + runtime, no main() ---
add_library(sim_runtime STATIC
    src/sim_runtime.cpp
    src/sim_serial.cpp
    src/sim_spi.cpp
    src/sim_wire.cpp
)

target_include_directories(sim_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/shims
)

target_compile_definitions(sim_runtime PUBLIC BOARDGHOST_SIM=1)

find_package(SDL2 REQUIRED)
target_link_libraries(sim_runtime PUBLIC SDL2::SDL2)
if(NOT TARGET SDL2::SDL2)
    target_include_directories(sim_runtime PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(sim_runtime PUBLIC ${SDL2_LIBRARIES})
endif()

# LovyanGFX PC backend — compiled from source against SDL2.
set(LGFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/LovyanGFX)

# Direct cpp files in v1/
file(GLOB LGFX_V1_DIRECT CONFIGURE_DEPENDS
    ${LGFX_DIR}/src/lgfx/v1/*.cpp
)

# Panel, touch, misc subdirs (Panel_sdl needs panels and touch types)
file(GLOB LGFX_V1_PANEL CONFIGURE_DEPENDS ${LGFX_DIR}/src/lgfx/v1/panel/*.cpp)
file(GLOB LGFX_V1_TOUCH CONFIGURE_DEPENDS ${LGFX_DIR}/src/lgfx/v1/touch/*.cpp)
file(GLOB LGFX_V1_MISC  CONFIGURE_DEPENDS ${LGFX_DIR}/src/lgfx/v1/misc/*.cpp)

# SDL platform only
file(GLOB LGFX_SDL CONFIGURE_DEPENDS
    ${LGFX_DIR}/src/lgfx/v1/platforms/sdl/*.cpp
)

# Font / utility C sources
file(GLOB LGFX_FONTS    CONFIGURE_DEPENDS ${LGFX_DIR}/src/lgfx/Fonts/*.c)
file(GLOB LGFX_UTILITY  CONFIGURE_DEPENDS ${LGFX_DIR}/src/lgfx/utility/*.c)

add_library(lovyangfx_pc STATIC
    ${LGFX_V1_DIRECT}
    ${LGFX_V1_PANEL}
    ${LGFX_V1_TOUCH}
    ${LGFX_V1_MISC}
    ${LGFX_SDL}
    ${LGFX_FONTS}
    ${LGFX_UTILITY}
)
target_include_directories(lovyangfx_pc PUBLIC ${LGFX_DIR}/src)
target_link_libraries(lovyangfx_pc PUBLIC SDL2::SDL2)
target_compile_definitions(lovyangfx_pc PUBLIC LGFX_USE_V1=1 LGFX_SDL=1)
# Suppress warnings from third-party code so they don't drown out ours.
target_compile_options(lovyangfx_pc PRIVATE -w)

# Make LovyanGFX headers visible to the sim runtime so user sketches can include it.
target_link_libraries(sim_runtime PUBLIC lovyangfx_pc)

# sim_main is a separate object library; sketch builds link it,
# unit-test builds do not (they bring their own gtest_main).
add_library(sim_main OBJECT src/sim_main.cpp)
target_link_libraries(sim_main PUBLIC sim_runtime)

if(BOARDGHOST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
