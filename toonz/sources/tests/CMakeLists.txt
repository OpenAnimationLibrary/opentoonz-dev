add_executable(svg_parser_regression svg_parser_regression.cpp)

target_include_directories(svg_parser_regression PRIVATE ${CMAKE_SOURCE_DIR})

target_link_libraries(svg_parser_regression
    Qt5::Core
    Qt5::Gui
    Qt5::Network
    Qt5::OpenGL
    Qt5::Svg
    Qt5::Xml
    Qt5::Script
    Qt5::Widgets
    Qt5::PrintSupport
    Qt5::Multimedia
    tnzcore
    tnzbase
    toonzlib
)

if(BUILD_ENV_APPLE)
    add_custom_command(
        TARGET svg_parser_regression POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:tnzcore>
            $<TARGET_FILE:tnzbase>
            $<TARGET_FILE:tnzext>
            $<TARGET_FILE:toonzlib>
            $<TARGET_FILE_DIR:svg_parser_regression>
        VERBATIM
    )
endif()

foreach(svg_case normal id width height rgb-first rgb-second)
    add_test(NAME svg_parser_${svg_case}
        COMMAND svg_parser_regression ${svg_case})
endforeach()
