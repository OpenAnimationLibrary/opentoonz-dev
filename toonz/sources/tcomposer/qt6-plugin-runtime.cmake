set(plugin_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(plugin_sources
    pluginhost.cpp
    plugin_tile_interface.cpp
    plugin_port_interface.cpp
    plugin_fxnode_interface.cpp
    plugin_param_interface.cpp
    toonz_params.cpp)
qt6_wrap_cpp(plugin_sources "${plugin_dir}/pluginhost.h")
add_library(toonzqt SHARED ${plugin_sources})
target_compile_definitions(toonzqt PRIVATE TOONZQT_EXPORTS QT_WIDGETS_LIB)
target_link_libraries(toonzqt PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    tnzcore tnzbase tnzext toonzlib sound ${GL_LIB})

