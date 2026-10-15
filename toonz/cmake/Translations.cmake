# Kept outside the application project so translation builds can be tested
# without compiling OpenToonz or its rendering dependencies.
if(NOT DEFINED OT_TRANSLATIONS_SOURCE_DIR)
    get_filename_component(OT_TRANSLATIONS_SOURCE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../sources/translations" ABSOLUTE)
endif()
if(NOT DEFINED OT_TRANSLATIONS_OUTPUT_DIR)
    set(OT_TRANSLATIONS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/../../stuff/config/loc")
endif()

function(add_compile_translations_command module ts_name)
    set(qm_files)
    list(LENGTH LANG_CODES num_languages)
    list(LENGTH LANG_DISPLAY_NAMES num_display_names)
    if(NOT num_languages EQUAL num_display_names)
        message(FATAL_ERROR "Translation language lists must have equal lengths")
    endif()
    math(EXPR last_index "${num_languages} - 1")
    foreach(idx RANGE 0 ${last_index})
        list(GET LANG_CODES ${idx} lang)
        list(GET LANG_DISPLAY_NAMES ${idx} display_name)
        set(ts_file "${OT_TRANSLATIONS_SOURCE_DIR}/${lang}/${ts_name}.ts")
        set(qm_dir "${OT_TRANSLATIONS_OUTPUT_DIR}/${display_name}")
        set(qm_file "${qm_dir}/${ts_name}.qm")
        if(NOT EXISTS "${ts_file}")
            message(FATAL_ERROR "Missing translation source: ${ts_file}")
        endif()
        # An OUTPUT rule rebuilds after a .ts-only edit or deleted .qm even
        # when the corresponding C++ library does not need to relink.
        add_custom_command(
            OUTPUT "${qm_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${qm_dir}"
            COMMAND ${Qt5_LRELEASE_EXECUTABLE} "${ts_file}" -qm "${qm_file}.tmp"
            COMMAND ${CMAKE_COMMAND} -E rename "${qm_file}.tmp" "${qm_file}"
            DEPENDS "${ts_file}"
            COMMENT "lrelease ${display_name}/${ts_name}.qm"
            VERBATIM)
        list(APPEND qm_files "${qm_file}")
    endforeach()
    add_custom_target(${module}_${ts_name}_translations DEPENDS ${qm_files})
    add_dependencies(${module} ${module}_${ts_name}_translations)
endfunction()
