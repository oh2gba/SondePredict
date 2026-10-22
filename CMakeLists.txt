cmake_minimum_required(VERSION 3.21)

project(SondePredict
    VERSION 5.0.2
    DESCRIPTION "Radiosonde flight path and landing prediction"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SP_BUILD_TESTS "Build the unit tests" ON)
option(SP_BUILD_APP "Build the desktop application" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network Sql)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/cli)

if(SP_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets Quick QuickWidgets Location Positioning)
    add_subdirectory(src/app)
endif()

if(SP_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
