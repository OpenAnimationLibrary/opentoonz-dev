add_executable(raster_palette_save_test native_save_test.cpp)
target_link_libraries(raster_palette_save_test PRIVATE
    toonzlib tnzcore tnzbase Qt5::Core Qt5::Gui)
set_target_properties(raster_palette_save_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/palette-tests")
