cmake_minimum_required(VERSION 3.25...3.31)

project(LogHunter
    VERSION 0.0.2
    DESCRIPTION "Windows event log (EVTX) analysis and threat detection"
    HOMEPAGE_URL "https://github.com/satt105/Loghunter"
    LANGUAGES C CXX)

set(LOGHUNTER_PUBLISHER "Satt")
set(LOGHUNTER_AUTHOR "Thomas Bouret alias satt105")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(WIN32)
    set(CMAKE_INSTALL_BINDIR "." CACHE PATH "Installation folder of the executables")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Concurrent Gui Widgets Svg LinguistTools)
qt_standard_project_setup(REQUIRES 6.8)

# ── Common compile options ───────────────────────────────────────────────────
add_library(loghunter_options INTERFACE)
if(MSVC)
    target_compile_options(loghunter_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor
        /wd4251)
    target_compile_definitions(loghunter_options INTERFACE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(loghunter_options INTERFACE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()
target_compile_definitions(loghunter_options INTERFACE
    QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_USE_QSTRINGBUILDER)

# ── Vendored third-party dependencies ────────────────────────────────────────
add_subdirectory(third_party)

# ── Version (generated header) ───────────────────────────────────────────────
configure_file(cmake/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/loghunter_version.h @ONLY)

# ── Languages: one translations/loghunter_<code>.ts file per language ────────
file(GLOB LOGHUNTER_TRANSLATIONS CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/translations/loghunter_*.ts)

add_subdirectory(src)

include(cmake/Installation.cmake)
