cmake_minimum_required(VERSION 3.10)

# Can be built/tested on its own, without OpenToonz, Qt, or a graphics context.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(OpenToonzGlb LANGUAGES CXX)
    set(GLB_STANDALONE ON)
else()
    set(GLB_STANDALONE OFF)
endif()

add_library(otglb STATIC glbloader.cpp glbloader.h)
target_compile_features(otglb PUBLIC cxx_std_17)
target_include_directories(otglb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(otglb PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(BUILD_GLB_LOADER_TOOLS "Build standalone GLB inspection and tests" ${GLB_STANDALONE})
if(BUILD_GLB_LOADER_TOOLS)
    enable_testing()
    add_executable(glb_inspect glb_inspect.cpp)
    target_link_libraries(glb_inspect PRIVATE otglb)
    add_executable(glbloader_test tests/glbloader_test.cpp)
    target_link_libraries(glbloader_test PRIVATE otglb)
    add_test(NAME glbloader COMMAND glbloader_test)
endif()
