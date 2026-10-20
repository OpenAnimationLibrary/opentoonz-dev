cmake_minimum_required(VERSION 3.16)
project(SkeletonRigTests LANGUAGES CXX)
enable_testing()

add_executable(skeleton_rig_test
    rig_test.cpp
    ../../trig.cpp
    ../../../common/tgeometry/tgeometry.cpp)
target_compile_features(skeleton_rig_test PRIVATE cxx_std_17)
target_include_directories(skeleton_rig_test PRIVATE ../../../include)
target_compile_definitions(skeleton_rig_test PRIVATE TOONZLIB_EXPORTS
    TGEOMETRY_EXPORTS)
if(WIN32)
    target_compile_definitions(skeleton_rig_test PRIVATE NOMINMAX _USE_MATH_DEFINES)
elseif(APPLE)
    target_compile_definitions(skeleton_rig_test PRIVATE MACOSX)
elseif(UNIX)
    target_compile_definitions(skeleton_rig_test PRIVATE LINUX)
endif()
add_test(NAME skeleton_rig COMMAND skeleton_rig_test)
