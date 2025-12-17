cmake_minimum_required(VERSION 3.20)
project(StateCheckpointSystem VERSION 1.0.0 LANGUAGES CXX)

# C++20 standardı
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Options
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_EXAMPLES "Build example applications" ON)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Source files
file(GLOB_RECURSE CORE_SOURCES "src/core/*.cpp")
file(GLOB_RECURSE STATE_SOURCES "src/state/*.cpp")
file(GLOB_RECURSE LOGGER_SOURCES "src/logger/*.cpp")
file(GLOB_RECURSE ROLLBACK_SOURCES "src/rollback/*.cpp")
file(GLOB_RECURSE UTILS_SOURCES "src/utils/*.cpp")
file(GLOB_RECURSE REAL_PROCESS_SOURCES "src/real_process/*.cpp")

# Ana kütüphane
add_library(state_checkpoint STATIC
    ${CORE_SOURCES}
    ${STATE_SOURCES}
    ${LOGGER_SOURCES}
    ${ROLLBACK_SOURCES}
    ${UTILS_SOURCES}
    ${REAL_PROCESS_SOURCES}
)

target_include_directories(state_checkpoint PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

# Threading support
find_package(Threads REQUIRED)
target_link_libraries(state_checkpoint PUBLIC Threads::Threads)

# Main executable
add_executable(checkpoint_demo src/main.cpp)
target_link_libraries(checkpoint_demo PRIVATE state_checkpoint)

# CLI Application
add_executable(process_checkpoint_cli src/cli/process_checkpoint_cli.cpp)
target_link_libraries(process_checkpoint_cli PRIVATE state_checkpoint)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install rules
install(TARGETS state_checkpoint
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)

# Package configuration
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/StateCheckpointSystemConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
