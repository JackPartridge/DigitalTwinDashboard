cmake_minimum_required(VERSION 3.20)
project(digitalTwinDashboard LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SDV_SANDBOX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../SDV_Sandbox" CACHE PATH "Path to SDV_Sandbox")
set(SDV_BUILD_DIR "${SDV_SANDBOX_DIR}/build" CACHE PATH "Path to SDV_Sandbox build tree")

find_package(Boost REQUIRED COMPONENTS system)

add_executable(digitalTwinDashboard
  src/main.cpp
  src/dashboardServer.cpp
)

target_include_directories(digitalTwinDashboard
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${SDV_SANDBOX_DIR}/src/sdk"
    "${SDV_SANDBOX_DIR}/src/common"
)

add_library(vecu_sdk SHARED IMPORTED)
set_target_properties(vecu_sdk PROPERTIES
  IMPORTED_LOCATION "${SDV_BUILD_DIR}/libvecu_sdk.so"
  IMPORTED_SONAME "libvecu_sdk.so.1"
)

add_library(sdvCommon STATIC IMPORTED)
set_target_properties(sdvCommon PROPERTIES
  IMPORTED_LOCATION "${SDV_BUILD_DIR}/libsdvCommon.a"
)

target_link_libraries(digitalTwinDashboard
  PRIVATE
    vecu_sdk
    sdvCommon
    Boost::system
    pthread
)

set_target_properties(digitalTwinDashboard PROPERTIES
  BUILD_RPATH "${SDV_BUILD_DIR}"
  INSTALL_RPATH "${SDV_BUILD_DIR}"
)
