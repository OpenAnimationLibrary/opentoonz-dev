cmake_minimum_required(VERSION 3.10)
project(handwriting_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)
find_package(Qt5 REQUIRED COMPONENTS Widgets Test)
enable_testing()
set(panel_dir ../../toonz/sources/toonz)
add_executable(handwriting_tests
    handwriting_test.cpp
    ${panel_dir}/handwritingsurface.cpp
    ${panel_dir}/handwritingsurface.h
)
target_include_directories(handwriting_tests PRIVATE ${panel_dir})
target_link_libraries(handwriting_tests PRIVATE Qt5::Widgets Qt5::Test)
if(WIN32)
    target_sources(handwriting_tests PRIVATE
        ${panel_dir}/winhandwritingrecognizer.cpp
        ${panel_dir}/winhandwritingrecognizer.h
    )
    target_link_libraries(handwriting_tests PRIVATE ole32 oleaut32)
endif()
add_test(NAME handwriting COMMAND handwriting_tests)
set_tests_properties(handwriting PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
