# Keep extraction explicit: .ts files are maintained sources, never build outputs.
if(NOT DEFINED OT_TRANSLATIONS_SOURCE_DIR)
    get_filename_component(OT_TRANSLATIONS_SOURCE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../sources/translations" ABSOLUTE)
endif()
if(NOT DEFINED OT_TRANSLATIONS_OUTPUT_DIR)
    set(OT_TRANSLATIONS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/../../stuff/config/loc")
endif()
# Keep command and declared-output paths identical, including Windows short paths.
file(MAKE_DIRECTORY "${OT_TRANSLATIONS_OUTPUT_DIR}")
get_filename_component(OT_TRANSLATIONS_OUTPUT_DIR
    "${OT_TRANSLATIONS_OUTPUT_DIR}" REALPATH)

function(add_translation module)
    set(source_list "")
    foreach(source IN LISTS ARGN)
        get_filename_component(source_path "${source}" ABSOLUTE)
        # Generated MOC/resource sources are not translation inputs.
        if(EXISTS "${source_path}" AND NOT IS_DIRECTORY "${source_path}")
            # Qt 5 reads response files using the Windows local code page.
            # Relative paths keep Unicode checkout prefixes out of that file.
            file(RELATIVE_PATH relative_source "${CMAKE_CURRENT_SOURCE_DIR}" "${source_path}")
            string(APPEND source_list "${relative_source}\n")
        endif()
    endforeach()
    set(list_file "${CMAKE_CURRENT_BINARY_DIR}/${module}_translation_sources.txt")
    file(WRITE "${list_file}" "${source_list}")
    get_directory_property(include_dirs INCLUDE_DIRECTORIES)
    set(include_options)
    foreach(directory IN LISTS include_dirs)
        list(APPEND include_options "-I${directory}")
    endforeach()
    set(catalogs)
    set(release_commands)
    foreach(language IN LISTS LANG_CODES)
        set(catalog "${OT_TRANSLATIONS_SOURCE_DIR}/${language}/${module}.ts")
        list(APPEND catalogs "${catalog}")
        # Preserve the explicit update target's historical intermediate .qm files.
        set(output_dir "${CMAKE_BINARY_DIR}/loc/${language}")
        set(output "${output_dir}/${module}.qm")
        list(APPEND release_commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
            COMMAND "${Qt5_LRELEASE_EXECUTABLE}" "${catalog}" -qm "${output}.tmp"
            COMMAND "${CMAKE_COMMAND}" -E rename "${output}.tmp" "${output}")
    endforeach()
    add_custom_target(translate_update_${module}
        COMMAND "${Qt5_LUPDATE_EXECUTABLE}" "@${list_file}" ${include_options}
            -ts ${catalogs}
        ${release_commands}
        COMMENT "Explicitly update ${module} translation sources"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        VERBATIM)
    set_target_properties(translate_update_${module} PROPERTIES
        EXCLUDE_FROM_DEFAULT_BUILD TRUE)
endfunction()

set(OT_TRANSLATIONS_COMPILE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CompileTranslation.cmake")

function(add_compile_translations_command module ts_name)
    if(TARGET "${Qt5_LRELEASE_EXECUTABLE}")
        set(release_command "$<TARGET_FILE:${Qt5_LRELEASE_EXECUTABLE}>")
    else()
        set(release_command "${Qt5_LRELEASE_EXECUTABLE}")
    endif()
    list(LENGTH LANG_CODES language_count)
    list(LENGTH LANG_DISPLAY_NAMES display_count)
    if(NOT language_count EQUAL display_count OR language_count EQUAL 0)
        message(FATAL_ERROR "Translation language lists must be nonempty and equal in length")
    endif()
    math(EXPR last_index "${language_count} - 1")
    set(outputs)
    foreach(index RANGE 0 ${last_index})
        list(GET LANG_CODES ${index} language)
        list(GET LANG_DISPLAY_NAMES ${index} display_name)
        set(source "${OT_TRANSLATIONS_SOURCE_DIR}/${language}/${ts_name}.ts")
        if(NOT EXISTS "${source}")
            message(FATAL_ERROR "Missing translation source: ${source}")
        endif()
        set(output_dir "${OT_TRANSLATIONS_OUTPUT_DIR}/${display_name}")
        set(output "${output_dir}/${ts_name}.qm")
        set(temporary_dir "${CMAKE_CURRENT_BINARY_DIR}/translation-tmp/$<CONFIG>/${language}")
        set(temporary "${temporary_dir}/${ts_name}.qm")
        add_custom_command(
            OUTPUT "${output}"
            COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${source}" "-DOUTPUT=${output}"
                "-DTEMPORARY=${temporary}" "-DLRELEASE=${release_command}"
                -P "${OT_TRANSLATIONS_COMPILE_SCRIPT}"
            DEPENDS "${source}" "${Qt5_LRELEASE_EXECUTABLE}"
                "${OT_TRANSLATIONS_COMPILE_SCRIPT}"
            COMMENT "lrelease ${display_name}/${ts_name}.qm"
            VERBATIM)
        list(APPEND outputs "${output}")
    endforeach()
    add_custom_target(${module}_${ts_name}_translations DEPENDS ${outputs})
    add_dependencies(${module} ${module}_${ts_name}_translations)
endfunction()
