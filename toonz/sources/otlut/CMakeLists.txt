cmake_minimum_required(VERSION 3.16)

# Also builds independently for CLI and numerical regression tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(otlut LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
endif()

set(OTLUT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../thirdparty/otlut/src")
add_library(otlut_core STATIC
    ${OTLUT_SOURCE_DIR}/lut_fit.cpp
    ${OTLUT_SOURCE_DIR}/palette_fit.cpp
    ${OTLUT_SOURCE_DIR}/lut_writer.cpp
)
set_target_properties(otlut_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(otlut_core PUBLIC ${OTLUT_SOURCE_DIR})

add_executable(otlut
    ${OTLUT_SOURCE_DIR}/main.cpp
    ${OTLUT_SOURCE_DIR}/image_loader.cpp
)
target_link_libraries(otlut PRIVATE otlut_core)
if(WIN32)
    target_link_libraries(otlut PRIVATE shell32)
endif()
install(TARGETS otlut RUNTIME DESTINATION bin)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../../thirdparty/otlut/LICENSE"
    DESTINATION share/opentoonz/licenses RENAME OTLUT-LICENSE)

install(FILES "${CMAKE_CURRENT_LIST_DIR}/../../../thirdparty/otlut/STB-LICENSE"
    DESTINATION share/opentoonz/licenses)

option(OTLUT_BUILD_TESTS "Build OTLUT numerical and CLI regression tests" OFF)
if(OTLUT_BUILD_TESTS)
    enable_testing()
    add_executable(otlut_tests tests/test_lut_writer.cpp tests/test_lut_fit.cpp
        tests/test_palette_fit.cpp)
    target_link_libraries(otlut_tests PRIVATE otlut_core)
    add_test(NAME otlut_core COMMAND otlut_tests)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(NAME otlut_image_pair COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/tests/run_single_image_integration.py
        --exe $<TARGET_FILE:otlut> --workdir ${CMAKE_CURRENT_BINARY_DIR}/image-pair)
endif()

if(OTLUT_BUILD_TESTS)
    add_test(NAME otlut_palette COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_LIST_DIR}/tests/run_palette_integration.py
        --exe $<TARGET_FILE:otlut> --workdir ${CMAKE_CURRENT_BINARY_DIR}/palette)
    if(TARGET Qt5::Core)
        add_executable(otlut_task_test tests/test_lut_task.cpp)
        target_link_libraries(otlut_task_test PRIVATE otlut_core Qt5::Core)
        add_test(NAME otlut_task COMMAND otlut_task_test)
        add_executable(otlut_reader_test tests/test_opentoonz_reader.cpp
            ../toonzlib/lut3d.cpp)
        target_include_directories(otlut_reader_test PRIVATE ../include)
        target_compile_definitions(otlut_reader_test PRIVATE TOONZLIB_EXPORTS)
        target_link_libraries(otlut_reader_test PRIVATE otlut_core Qt5::Core)
        add_test(NAME otlut_opentoonz_reader COMMAND otlut_reader_test)
    endif()
endif()
