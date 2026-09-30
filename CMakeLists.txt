cmake_minimum_required(VERSION 3.20)

project(xxwidgets
    VERSION 0.1.0
    DESCRIPTION "C widgets with native desktop controls and a terminal backend"
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(_xxwidgets_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_xxwidgets_top_level ON)
endif()

option(BUILD_SHARED_LIBS "Build xxwidgets as a shared library" OFF)
option(XXWIDGETS_BUILD_EXAMPLES "Build the native/TUI widget demos" ${_xxwidgets_top_level})
option(XXWIDGETS_BUILD_TESTS "Build xxwidgets tests" ${_xxwidgets_top_level})
set(XXWIDGETS_NATIVE_BACKEND "auto" CACHE STRING "Native backend: auto, win32, gtk3, cocoa, or none")
set_property(CACHE XXWIDGETS_NATIVE_BACKEND PROPERTY STRINGS auto win32 gtk3 cocoa none)

string(TOLOWER "${XXWIDGETS_NATIVE_BACKEND}" _xxwidgets_native)
if(_xxwidgets_native STREQUAL "auto")
    if(WIN32)
        set(_xxwidgets_native win32)
    elseif(APPLE)
        set(_xxwidgets_native cocoa)
    elseif(UNIX)
        set(_xxwidgets_native gtk3)
    else()
        set(_xxwidgets_native none)
    endif()
endif()

set(_xxwidgets_backend_source)
set(_xxwidgets_backend_libs)
if(_xxwidgets_native STREQUAL "win32")
    if(NOT WIN32)
        message(FATAL_ERROR "The win32 native backend requires Windows")
    endif()
    set(_xxwidgets_backend_source src/backends/xxwidgets_win32.c)
    set(_xxwidgets_backend_libs user32 gdi32 comctl32)
elseif(_xxwidgets_native STREQUAL "gtk3")
    if(NOT UNIX OR APPLE)
        message(FATAL_ERROR "The gtk3 native backend requires a Unix desktop")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XXWIDGETS_GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0>=3.16)
    set(_xxwidgets_backend_source src/backends/xxwidgets_gtk.c)
    set(_xxwidgets_backend_libs PkgConfig::XXWIDGETS_GTK3)
elseif(_xxwidgets_native STREQUAL "cocoa")
    if(NOT APPLE)
        message(FATAL_ERROR "The cocoa native backend requires macOS")
    endif()
    enable_language(OBJC)
    set_source_files_properties(src/backends/xxwidgets_cocoa.m PROPERTIES COMPILE_OPTIONS "-fno-objc-arc")
    find_library(XXWIDGETS_APPKIT_FRAMEWORK AppKit REQUIRED)
    set(_xxwidgets_backend_source src/backends/xxwidgets_cocoa.m)
    set(_xxwidgets_backend_libs "${XXWIDGETS_APPKIT_FRAMEWORK}")
elseif(NOT _xxwidgets_native STREQUAL "none")
    message(FATAL_ERROR "XXWIDGETS_NATIVE_BACKEND must be auto, win32, gtk3, cocoa, or none")
endif()

add_library(xxwidgets
    include/xxwidgets/xxwidgets.h
    src/xxwidgets_internal.h
    src/xxwidgets.c
    src/xxwidgets_archivebrowser.c
    src/backends/xxwidgets_tui.c
    ${_xxwidgets_backend_source}
)
add_library(xxwidgets::xxwidgets ALIAS xxwidgets)
target_compile_features(xxwidgets PUBLIC c_std_11)
set_target_properties(xxwidgets PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR}
)
target_include_directories(xxwidgets
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src
)
target_compile_definitions(xxwidgets PRIVATE XXWIDGETS_BUILDING_LIBRARY)
if(NOT _xxwidgets_native STREQUAL "none")
    target_compile_definitions(xxwidgets PRIVATE XXWIDGETS_HAS_NATIVE)
endif()
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(xxwidgets PUBLIC XXWIDGETS_STATIC)
endif()
target_link_libraries(xxwidgets PRIVATE ${_xxwidgets_backend_libs})
if(WIN32)
    target_compile_definitions(xxwidgets PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    # The TUI backend uses the console API; WinAPI controls require only these
    # additional libraries. Console APIs come from the default kernel32 link.
elseif(UNIX)
    target_compile_definitions(xxwidgets PRIVATE _POSIX_C_SOURCE=200809L)
endif()
if(MSVC)
    target_compile_options(xxwidgets PRIVATE /W4 /utf-8)
else()
    target_compile_options(xxwidgets PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(XXWIDGETS_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/widgets_demo.c")
    add_executable(xxwidgets_demo examples/widgets_demo.c)
    target_link_libraries(xxwidgets_demo PRIVATE xxwidgets::xxwidgets)
    if(MSVC)
        target_compile_options(xxwidgets_demo PRIVATE /utf-8)
    endif()
endif()

if(XXWIDGETS_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/hexview_demo.c")
    add_executable(xxwidgets_hexview_demo examples/hexview_demo.c)
    target_link_libraries(xxwidgets_hexview_demo PRIVATE xxwidgets::xxwidgets)
    if(MSVC)
        target_compile_options(xxwidgets_hexview_demo PRIVATE /utf-8)
    endif()
endif()

if(XXWIDGETS_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/archiveview_demo.c")
    add_executable(xxwidgets_archiveview_demo examples/archiveview_demo.c)
    target_link_libraries(xxwidgets_archiveview_demo PRIVATE xxwidgets::xxwidgets)
    if(MSVC)
        target_compile_options(xxwidgets_archiveview_demo PRIVATE /utf-8)
    endif()
endif()

if(XXWIDGETS_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/archivebrowser_demo.c")
    add_executable(xxwidgets_archivebrowser_demo examples/archivebrowser_demo.c)
    target_link_libraries(xxwidgets_archivebrowser_demo PRIVATE xxwidgets::xxwidgets)
    if(MSVC)
        target_compile_options(xxwidgets_archivebrowser_demo PRIVATE /utf-8)
    endif()
endif()

if(XXWIDGETS_BUILD_TESTS AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    include(CTest)
    if(BUILD_TESTING)
        # Deterministic API/lifecycle tests don't require a display or terminal.
        add_executable(xxwidgets_core_tests tests/test_core.c src/xxwidgets.c src/xxwidgets_archivebrowser.c)
        target_include_directories(xxwidgets_core_tests PRIVATE include src)
        target_compile_features(xxwidgets_core_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_STATIC)
        add_test(NAME xxwidgets_core COMMAND xxwidgets_core_tests)
        if(_xxwidgets_native STREQUAL "win32")
            add_executable(xxwidgets_win32_tests tests/test_win32.c)
            target_compile_definitions(xxwidgets_win32_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
            target_link_libraries(xxwidgets_win32_tests PRIVATE xxwidgets::xxwidgets user32 gdi32 comctl32)
            add_test(NAME xxwidgets_win32 COMMAND xxwidgets_win32_tests)
        endif()
        if(WIN32)
            add_executable(xxwidgets_tui_tests tests/test_tui_win32.c)
            target_compile_definitions(xxwidgets_tui_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
            target_link_libraries(xxwidgets_tui_tests PRIVATE xxwidgets::xxwidgets user32)
            add_test(NAME xxwidgets_tui COMMAND xxwidgets_tui_tests)
            set_tests_properties(xxwidgets_tui PROPERTIES TIMEOUT 15)
        endif()
    endif()
endif()

install(TARGETS xxwidgets EXPORT xxwidgetsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/xxwidgets DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(EXPORT xxwidgetsTargets FILE xxwidgetsTargets.cmake
    NAMESPACE xxwidgets:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xxwidgets)
configure_package_config_file(cmake/xxwidgetsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/xxwidgetsConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xxwidgets)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/xxwidgetsConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/xxwidgetsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/xxwidgetsConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/xxwidgets)
message(STATUS "xxwidgets native backend: ${_xxwidgets_native}; TUI backend: enabled")
