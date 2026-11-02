# Builds the plugin on its own (David's own build also builds it): cmake -B build && cmake --build build
# Point DAVID_SDK at the folder with david_plugin.h.
cmake_minimum_required(VERSION 3.20)
project(qoi_like C)
set(DAVID_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../../sdk" CACHE PATH "Folder with david_plugin.h")
add_library(qlike SHARED qoi_like.c)
target_include_directories(qlike PRIVATE ${DAVID_SDK})
set_target_properties(qlike PROPERTIES PREFIX "")
