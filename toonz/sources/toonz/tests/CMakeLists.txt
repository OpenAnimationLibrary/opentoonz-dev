cmake_minimum_required(VERSION 3.16)
project(ToonzViewerTests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
enable_testing()
add_executable(viewerscrubdrag_test viewerscrubdrag_test.cpp)
add_test(NAME viewerscrubdrag COMMAND viewerscrubdrag_test)

option(BUILD_SPLASHIMAGE_TEST "Build the Qt splash image regression test" ON)
if(BUILD_SPLASHIMAGE_TEST)
set(CMAKE_AUTORCC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Svg Xml)
add_executable(splashimage_test splashimage_test.cpp splashimage_test.qrc)
target_link_libraries(splashimage_test PRIVATE Qt5::Core Qt5::Gui Qt5::Svg
                                               Qt5::Xml)
add_test(NAME splashimage COMMAND splashimage_test)
if(WIN32)
  # The offscreen plugin uses Qt's bundled font directory, which this Windows
  # SDK does not ship. Use the application's native system-font backend.
  set_tests_properties(splashimage PROPERTIES ENVIRONMENT
                                              "QT_QPA_PLATFORM=windows")
else()
  set_tests_properties(splashimage PROPERTIES ENVIRONMENT
                                              "QT_QPA_PLATFORM=offscreen")
endif()
endif()
