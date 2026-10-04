# SPDX-License-Identifier: BSD-3-Clause

include(FetchContent)

cmake_policy(PUSH)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(OT_OPENEXR_VERSION "3.5.0")
set(OT_IMATH_VERSION "3.2.3")

option(OT_USE_SYSTEM_OPENEXR
       "Use an installed OpenEXR 3.5 or newer instead of the pinned build"
       OFF)

if(OT_USE_SYSTEM_OPENEXR)
    find_package(OpenEXR ${OT_OPENEXR_VERSION} CONFIG REQUIRED)
    message(STATUS "Using system OpenEXR ${OpenEXR_VERSION}")
else()
    # Keep the application package self-contained. OpenEXR, Imath, and the
    # vendored compression backends are linked statically into image.
    function(ot_configure_openexr)
        set(BUILD_SHARED_LIBS OFF)
        set(BUILD_TESTING OFF)
        set(CMAKE_POSITION_INDEPENDENT_CODE ON)

        set(IMATH_INSTALL OFF)
        set(IMATH_INSTALL_PKG_CONFIG OFF)
        set(PYTHON OFF)
        set(PYBIND11 OFF)

        FetchContent_Declare(
            Imath
            URL https://github.com/AcademySoftwareFoundation/Imath/archive/5f27ba266d3ea1565e912570c30b5eafc89959f1.tar.gz
            URL_HASH SHA256=b3a8de8b9dfcf4e5387e1ab3ca67c858e7fd05d1faa2e55decc3ee715e034cb8
        )
        FetchContent_MakeAvailable(Imath)

        # Imath's project version variables are scoped to its subdirectory.
        # OpenEXR records them in its generated configuration header, so make
        # the pinned ABI information visible while configuring OpenEXR.
        set(Imath_VERSION "${OT_IMATH_VERSION}")
        set(Imath_VERSION_MAJOR 3)
        set(Imath_VERSION_MINOR 2)
        set(Imath_VERSION_PATCH 3)
        set(Imath_SOVERSION 30)
        set(CMAKE_DISABLE_FIND_PACKAGE_Imath TRUE)

        set(OPENEXR_INSTALL OFF)
        set(OPENEXR_INSTALL_PKG_CONFIG OFF)
        set(OPENEXR_BUILD_TOOLS OFF)
        set(OPENEXR_INSTALL_TOOLS OFF)
        set(OPENEXR_INSTALL_DEVELOPER_TOOLS OFF)
        set(OPENEXR_BUILD_EXAMPLES OFF)
        set(OPENEXR_BUILD_PYTHON OFF)
        set(OPENEXR_BUILD_OSS_FUZZ OFF)
        set(OPENEXR_VERSION_RELEASE_TYPE "" CACHE STRING "" FORCE)
        set(OPENEXR_FORCE_INTERNAL_DEFLATE ON)
        set(OPENEXR_FORCE_INTERNAL_OPENJPH ON)
        set(OPENEXR_FORCE_INTERNAL_ZSTD ON)

        FetchContent_Declare(
            OpenEXR
            URL https://github.com/AcademySoftwareFoundation/openexr/archive/5d838e01770cadb8f2a36c0d17018452fa7a3c0c.tar.gz
            URL_HASH SHA256=4a96d1371022041ac05444916afc90197d9a134af09f445a5d05b0cf02b54984
        )
        FetchContent_MakeAvailable(OpenEXR)
    endfunction()

    ot_configure_openexr()
    message(STATUS
            "Using pinned OpenEXR ${OT_OPENEXR_VERSION} with Imath ${OT_IMATH_VERSION}")
endif()

if(NOT TARGET OpenEXR::OpenEXR)
    message(FATAL_ERROR "OpenEXR::OpenEXR target is unavailable")
endif()

cmake_policy(POP)
