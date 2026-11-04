cmake_minimum_required(VERSION 3.16)
project(ViewerScrubTests LANGUAGES CXX)

enable_testing()
add_executable(viewerscrubdrag_test viewerscrubdrag_test.cpp)
target_compile_features(viewerscrubdrag_test PRIVATE cxx_std_17)
add_test(NAME viewerscrubdrag COMMAND viewerscrubdrag_test)
