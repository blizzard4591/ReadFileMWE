cmake_minimum_required(VERSION 3.16)

project(MinimalExample CXX)

set(CXX ON)

list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR})
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Find includes in corresponding build directories
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (MSVC)
	add_definitions(/std:c++20)
	add_definitions(/DNOMINMAX)
	add_definitions(/MP)
else()
	add_definitions(-std=c++20)
endif()

# Add base folder for better inclusion paths
include_directories("${PROJECT_SOURCE_DIR}/src")

# Main Sources
add_executable(${CMAKE_PROJECT_NAME} ${PROJECT_SOURCE_DIR}/src/mwe.cpp)
add_executable(sleepHelper ${PROJECT_SOURCE_DIR}/src/sleepHelper.cpp)

cmake_policy(SET CMP0135 NEW)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE psapi)

set(CMAKE_CXX_STANDARD 20)
