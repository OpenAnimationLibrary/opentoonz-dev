add_executable(channel_mapping_test channelmapping.cpp)
target_link_libraries(channel_mapping_test PRIVATE toonzlib tnzbase tnzcore)
add_test(NAME channel_mapping COMMAND channel_mapping_test)

add_executable(custom_channels_test customchannels.cpp)
target_link_libraries(custom_channels_test PRIVATE toonzlib tnzbase tnzcore Qt5::Core)
add_test(NAME custom_channels COMMAND custom_channels_test)
