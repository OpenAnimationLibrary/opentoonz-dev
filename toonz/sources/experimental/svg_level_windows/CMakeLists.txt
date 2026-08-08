cmake_minimum_required(VERSION 3.16)

project(svg_level_probe LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "The experimental SVG Level probe is currently Windows-only.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Svg Widgets)

add_executable(svg_level_probe
  svg_level_probe.cpp
  svg_open_mode_dialog.cpp
  svg_open_mode_dialog.h
  svg_rasterization_service.cpp
  svg_rasterization_service.h
)

target_link_libraries(svg_level_probe PRIVATE
  Qt5::Core
  Qt5::Gui
  Qt5::Svg
  Qt5::Widgets
)

target_compile_definitions(svg_level_probe PRIVATE
  NOMINMAX
  WIN32_LEAN_AND_MEAN
)
