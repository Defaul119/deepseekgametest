cmake_minimum_required(VERSION 3.20)
project(retrofps VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

find_package(SDL2 REQUIRED)

set(RF_WARNINGS -Wall -Wextra -Wpedantic -Wno-unused-parameter)
set(RF_LINK_FLAGS -static-libgcc -static-libstdc++)

# GCC's POSIX threading runtime is still a DLL. Copy it next to every
# executable so the build runs without modifying PATH.
get_filename_component(RF_TOOLCHAIN_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(RF_WINPTHREAD_DLL "${RF_TOOLCHAIN_BIN}/libwinpthread-1.dll")
function(rf_copy_runtime target)
    if(EXISTS "${RF_WINPTHREAD_DLL}")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${RF_WINPTHREAD_DLL}" "$<TARGET_FILE_DIR:${target}>")
    endif()
endfunction()

file(GLOB_RECURSE RF_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/math/*.cpp
    ${CMAKE_SOURCE_DIR}/src/core/*.cpp
    ${CMAKE_SOURCE_DIR}/src/world/*.cpp
    ${CMAKE_SOURCE_DIR}/src/render/*.cpp
    ${CMAKE_SOURCE_DIR}/src/game/*.cpp
)

add_library(rf_core STATIC ${RF_CORE_SOURCES})
target_include_directories(rf_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(rf_core PRIVATE ${RF_WARNINGS})
set_target_properties(rf_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(retrofps
    src/main.cpp
    src/platform/platform.cpp
)
target_include_directories(retrofps PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(retrofps PRIVATE rf_core SDL2::SDL2)
target_compile_options(retrofps PRIVATE ${RF_WARNINGS})
target_compile_definitions(retrofps PRIVATE RF_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
target_link_options(retrofps PRIVATE ${RF_LINK_FLAGS})

# Ship the SDL2 runtime next to the executable.
add_custom_command(TARGET retrofps POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2>
        $<TARGET_FILE_DIR:retrofps>
)
rf_copy_runtime(retrofps)

option(RF_BUILD_TESTS "Build the mathematical/algorithmic test suite" ON)
if(RF_BUILD_TESTS)
    file(GLOB RF_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/*.cpp)
    if(RF_TEST_SOURCES)
        add_executable(retrofps_tests ${RF_TEST_SOURCES})
        target_link_libraries(retrofps_tests PRIVATE rf_core)
        target_compile_options(retrofps_tests PRIVATE ${RF_WARNINGS})
        target_compile_definitions(retrofps_tests PRIVATE RF_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
        target_link_options(retrofps_tests PRIVATE ${RF_LINK_FLAGS})
        rf_copy_runtime(retrofps_tests)
        enable_testing()
        add_test(NAME retrofps_tests COMMAND retrofps_tests)
        set_tests_properties(retrofps_tests PROPERTIES TIMEOUT 120)
    endif()
endif()

option(RF_BUILD_BENCH "Build spatial-query benchmarks" ON)
if(RF_BUILD_BENCH)
    file(GLOB RF_BENCH_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tools/*.cpp)
    if(RF_BENCH_SOURCES)
        add_executable(retrofps_bench ${RF_BENCH_SOURCES})
        target_link_libraries(retrofps_bench PRIVATE rf_core)
        target_compile_options(retrofps_bench PRIVATE ${RF_WARNINGS})
        target_compile_definitions(retrofps_bench PRIVATE
            RF_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
            RF_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
        target_link_options(retrofps_bench PRIVATE ${RF_LINK_FLAGS})
        rf_copy_runtime(retrofps_bench)
    endif()
endif()
