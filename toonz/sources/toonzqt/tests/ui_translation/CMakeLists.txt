cmake_minimum_required(VERSION 3.14)
project(UiTranslationTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt5 5.5 REQUIRED COMPONENTS Core Widgets LinguistTools)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(OT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(OT_TRANSLATIONS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/catalogs")
include("${OT_ROOT}/toonz/cmake/Translations.cmake")

# Exercise every source catalog, including the not-yet-distributed Bokmal set.
file(GLOB language_dirs LIST_DIRECTORIES TRUE "${OT_TRANSLATIONS_SOURCE_DIR}/*")
foreach(directory ${language_dirs})
    if(IS_DIRECTORY "${directory}")
        get_filename_component(language "${directory}" NAME)
        list(APPEND LANG_CODES "${language}")
        list(APPEND LANG_DISPLAY_NAMES "${language}")
    endif()
endforeach()

foreach(module toonz toonzqt tnztools qt_ui)
    add_custom_target(test_${module})
    add_compile_translations_command(test_${module} ${module})
endforeach()
add_custom_target(catalogs ALL)
add_dependencies(catalogs test_toonz test_toonzqt test_tnztools test_qt_ui)

add_executable(ui_translation_test ui_translation_test.cpp ../../uitranslation.cpp)
target_include_directories(ui_translation_test PRIVATE "${OT_ROOT}/toonz/sources/include")
target_compile_definitions(ui_translation_test PRIVATE TOONZQT_EXPORTS)
if(UNIX AND NOT APPLE)
    target_compile_definitions(ui_translation_test PRIVATE LINUX)
elseif(APPLE)
    target_compile_definitions(ui_translation_test PRIVATE MACOSX)
endif()
target_link_libraries(ui_translation_test PRIVATE Qt5::Core Qt5::Widgets)
add_dependencies(ui_translation_test catalogs)

enable_testing()
add_test(NAME ui_translation_runtime COMMAND ui_translation_test "${OT_TRANSLATIONS_OUTPUT_DIR}")
set_tests_properties(ui_translation_runtime PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
add_test(NAME ui_translation_audit
    COMMAND ${Python3_EXECUTABLE} "${OT_ROOT}/doc/tools/audit_ui_translations.py" --root "${OT_ROOT}")
add_test(NAME ui_translation_audit_regressions
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_audit.py")
add_test(NAME ui_translation_incremental_build
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/test_translation_build.py"
        --root "${OT_ROOT}" --cmake "${CMAKE_COMMAND}"
        --lrelease "$<TARGET_FILE:Qt5::lrelease>")
