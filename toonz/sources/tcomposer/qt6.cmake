find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Network OpenGL OpenGLWidgets Svg Xml Widgets PrintSupport
    Multimedia MultimediaWidgets SerialPort UiTools Core5Compat)

foreach(component Core Gui Network OpenGL Svg Xml Widgets PrintSupport
                  Multimedia MultimediaWidgets SerialPort UiTools)
    add_library(Qt5::${component} ALIAS Qt6::${component})
endforeach()

set(Qt5Core_FOUND TRUE)
set(Qt5Core_VERSION "${Qt6Core_VERSION}")
set(Qt5Core_LIBRARIES Qt6::Core)
set(Qt5Widgets_INCLUDE_DIRS "${Qt6Widgets_INCLUDE_DIRS}")
link_libraries(Qt6::Core5Compat Qt6::OpenGLWidgets)

macro(qt5_wrap_cpp)
    qt6_wrap_cpp(${ARGV})
endmacro()
macro(qt5_add_resources)
    qt6_add_resources(${ARGV})
endmacro()

