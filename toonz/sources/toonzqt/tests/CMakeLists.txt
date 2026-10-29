cmake_minimum_required(VERSION 3.16)
project(RepeatCommandTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
find_package(Qt5 5.14 REQUIRED COMPONENTS Widgets)

# This small Linux harness links the production command dispatcher and undo
# manager. Discard unrelated UI/file helpers without building all of OpenToonz.
if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "This standalone harness requires an ELF linker")
endif()

enable_testing()
add_executable(repeatcommand_test
    repeatcommand_test.cpp
    ../menubarcommand.cpp
    ../selection.cpp
    ../tselectionhandle.cpp
    ../../common/tcore/tundo.cpp
    ../../common/tgeometry/tgeometry.cpp
    ../../include/toonzqt/menubarcommand.h
    ../../include/toonzqt/tselectionhandle.h
    ../../include/tundo.h
)
target_include_directories(repeatcommand_test PRIVATE ../../include)
target_compile_definitions(repeatcommand_test PRIVATE LINUX TNZ_LITTLE_ENDIAN=1)
target_compile_options(repeatcommand_test PRIVATE -ffunction-sections -fdata-sections)
target_link_options(repeatcommand_test PRIVATE -Wl,--gc-sections)
target_link_libraries(repeatcommand_test PRIVATE Qt5::Widgets)
add_test(NAME repeatcommand COMMAND repeatcommand_test)
set_tests_properties(repeatcommand PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
