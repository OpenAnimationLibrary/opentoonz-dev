add_executable(tcomposer
    tcomposer.cpp
)

target_link_libraries(tcomposer
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    toonzlib
    tfarm
    tnzstdfx
    sound
    image
    colorfx
    toonzqt
)

if(OPENTOONZ_QT6_TCOMPOSER)
    string(TIMESTAMP worker_date "%Y%m%d" UTC)
    set(TCOMPOSER_BUILD_DATE "${worker_date}" CACHE STRING "UTC worker build date (YYYYMMDD)")
    string(LENGTH "${TCOMPOSER_BUILD_DATE}" worker_date_length)
    if(NOT TCOMPOSER_BUILD_DATE MATCHES "^[0-9]+$" OR NOT worker_date_length EQUAL 8)
        message(FATAL_ERROR "TCOMPOSER_BUILD_DATE must be YYYYMMDD")
    endif()
    find_package(Git REQUIRED)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE TCOMPOSER_SOURCE_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result)
    if(NOT git_result EQUAL 0)
        message(FATAL_ERROR "Cannot identify the Qt6 worker source revision")
    endif()
    configure_file(rendererinfo.h.in rendererinfo.h @ONLY)
    target_include_directories(tcomposer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    set_target_properties(tcomposer PROPERTIES
        OUTPUT_NAME "tcomposer_qt6_${TCOMPOSER_BUILD_DATE}")
endif()
