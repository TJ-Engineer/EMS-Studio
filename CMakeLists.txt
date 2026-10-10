# SPDX-FileCopyrightText: 2026 Open Engineering Platform contributors
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.28)

project(
    OpenEngineeringPlatform
    VERSION 0.1.0
    DESCRIPTION "Open mechanical CAD and electromagnetic simulation platform"
    LANGUAGES CXX
)

include(CTest)
include(cmake/OepTargetDefaults.cmake)

option(OEP_BUILD_APPS "Build desktop application entry points" ON)
option(OEP_BUILD_WORKERS "Build model and simulation worker entry points" ON)
option(OEP_ENABLE_STRICT_WARNINGS "Treat compiler warnings as errors" OFF)
option(OEP_ENABLE_OCCT_ADAPTER "Build the Open CASCADE geometry-kernel adapter" OFF)
set(OEP_OCCT_ROOT "" CACHE PATH "Optional Open CASCADE installation prefix")
set(OEP_OCCT_VERSION "7.6.3" CACHE STRING "Open CASCADE version exposed by the adapter")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(modules)

if(OEP_BUILD_APPS)
    add_subdirectory(apps)
endif()

if(OEP_BUILD_WORKERS)
    add_subdirectory(workers)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
