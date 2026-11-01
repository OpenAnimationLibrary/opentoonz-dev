cmake_minimum_required(VERSION 3.10)
project(splashimage_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTORCC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Gui Svg Xml)
add_executable(splashimage_test splashimage_test.cpp splashimage_test.qrc)
target_link_libraries(splashimage_test PRIVATE Qt5::Core Qt5::Gui Qt5::Svg
                                               Qt5::Xml)
enable_testing()
add_test(NAME splashimage COMMAND splashimage_test)
set_tests_properties(splashimage PROPERTIES ENVIRONMENT
                                            "QT_QPA_PLATFORM=offscreen")
