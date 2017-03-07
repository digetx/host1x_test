cmake_minimum_required(VERSION 2.6)
project(host1x_test)

find_package(PkgConfig)
pkg_search_module(DRM REQUIRED libdrm)

add_executable(host1x_test main.cpp gem.cpp util.cpp)
set_target_properties(host1x_test PROPERTIES CXX_STANDARD 14)
set_target_properties(host1x_test PROPERTIES CXX_STANDARD_REQUIRED ON)

target_include_directories(host1x_test PUBLIC ${DRM_INCLUDE_DIRS})

install(TARGETS host1x_test RUNTIME DESTINATION bin)
