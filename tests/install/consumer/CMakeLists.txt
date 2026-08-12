cmake_minimum_required(VERSION 4.3)
project(clapp_install_consumer LANGUAGES CXX)

find_package(clapp REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE clapp::clapp)
