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
option(XXWIDGETS_BUILD_SETTINGS "Build the xxfclib settings adapter for options dialogs" OFF)
option(XXWIDGETS_BUILD_INSPECTORS "Build native binary inspection using xxfclib" ${_xxwidgets_top_level})
if(NOT DEFINED XXWIDGETS_XXFCLIB_DIR)
    set(XXWIDGETS_XXFCLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xxfclib" CACHE PATH "xxfclib headers and optional settings adapter sources")
endif()
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

include("${XXWIDGETS_XXFCLIB_DIR}/cmake/xxfclib_codepages.cmake")
add_library(xxwidgets_codepages OBJECT ${XXFC_CODEPAGE_SOURCES})
target_compile_features(xxwidgets_codepages PRIVATE c_std_11)
target_compile_definitions(xxwidgets_codepages PRIVATE XXFC_STATIC)
target_include_directories(xxwidgets_codepages PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
set_target_properties(xxwidgets_codepages PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(xxwidgets_scan_colors OBJECT "${XXWIDGETS_XXFCLIB_DIR}/src/scan/xx_scan_color.c")
target_compile_features(xxwidgets_scan_colors PRIVATE c_std_11)
target_compile_definitions(xxwidgets_scan_colors PRIVATE XXFC_STATIC)
target_include_directories(xxwidgets_scan_colors PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
set_target_properties(xxwidgets_scan_colors PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(xxwidgets
    $<TARGET_OBJECTS:xxwidgets_scan_colors>
    $<TARGET_OBJECTS:xxwidgets_codepages>
    include/xxwidgets/xxwidgets.h
    include/xxwidgets/xxwidgets_combobox.h
    include/xxwidgets/xxwidgets_data_struct_table.h
    include/xxwidgets/xxwidgets_data_struct_view.h
    include/xxwidgets/xxwidgets_canvas.h
    include/xxwidgets/xxwidgets_entropy.h
    include/xxwidgets/xxwidgets_hex_options.h
    include/xxwidgets/xxwidgets_process.h
    include/xxwidgets/xxwidget_systemprocessesviewer.h
    include/xxwidgets/xxwidget_binaryviewer.h
    include/xxwidgets/xxwidget_resourcesviewer.h
    include/xxwidgets/xxwidget_importsviewer.h
    include/xxwidgets/xxwidget_exportsviewer.h
    include/xxwidgets/xxwidget_symbolsviewer.h
    include/xxwidgets/xxwidgets_scan_panel.h
    include/xxwidgets/xxwidgets_scan_options.h
    include/xxwidgets/xxwidgets_search.h
    include/xxwidgets/xxwidgets_forms.h
    include/xxwidgets/xxwidgets_font_options.h
    include/xxwidgets/xxwidgets_context_options.h
    include/xxwidgets/xxwidgets_optimization_options.h
    src/xxwidgets_internal.h
    src/xxwidgets.c
    src/xxwidgets_edit.c
    src/xxwidgets_window.c
    src/xxwidgets_listbox.c
    src/xxwidgets_archiveview.c
    src/xxwidgets_hex_options.c
    src/xxwidgets_hexview.c
    src/xxwidgets_hex_columns.c
    src/xxwidgets_shortcuts.c
    src/xxwidgets_archivebrowser.c
    src/xxwidgets_dialog.c
    src/xxwidgets_about.c
    src/xxwidgets_process.c
    src/xxwidget_systemprocessesviewer.c
    src/xxwidget_systemprocessesviewer_platform.c
    src/xxwidget_binaryviewer.c
    src/xxwidget_resourcesviewer.c
    src/xxwidget_importsviewer.c
    src/xxwidget_exportsviewer.c
    src/xxwidget_symbolsviewer.c
    src/xxwidgets_combobox.c
    src/xxwidgets_data_struct_table.c
    src/xxwidgets_data_struct_view.c
    src/xxwidgets_canvas.c
    src/xxwidgets_entropy.c
    src/xxwidgets_scanresults.c
    src/xxwidgets_treeview.c
    src/xxwidgets_scan_panel.c
    src/xxwidgets_scan_options.c
    src/xxwidgets_search.c
    src/xxwidgets_forms.c
    src/xxwidgets_font_options.c
    src/xxwidgets_context_options.c
    src/xxwidgets_optimization_options.c
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
        $<BUILD_INTERFACE:${XXWIDGETS_XXFCLIB_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE src "${XXWIDGETS_XXFCLIB_DIR}/include"
)
target_compile_definitions(xxwidgets PRIVATE XXWIDGETS_BUILDING_LIBRARY XXFC_STATIC)
if(NOT _xxwidgets_native STREQUAL "none")
    target_compile_definitions(xxwidgets PRIVATE XXWIDGETS_HAS_NATIVE)
endif()
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(xxwidgets PUBLIC XXWIDGETS_STATIC)
endif()
target_link_libraries(xxwidgets PRIVATE ${_xxwidgets_backend_libs})
if(WIN32)
    target_compile_definitions(xxwidgets PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(xxwidgets PRIVATE comdlg32 advapi32 shell32 user32)
    # TUI uses the console API and user32 for clipboard support.
elseif(UNIX)
    target_link_libraries(xxwidgets PRIVATE m)
    target_compile_definitions(xxwidgets PRIVATE _POSIX_C_SOURCE=200809L)
    if(APPLE)
        # AppKit's transitive system headers also need Darwin's BSD types.
        target_compile_definitions(xxwidgets PRIVATE _DARWIN_C_SOURCE)
    endif()
endif()
if(MSVC)
    target_compile_options(xxwidgets PRIVATE /W4 /utf-8)
else()
    target_compile_options(xxwidgets PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(XXWIDGETS_BUILD_INSPECTORS)
    if(NOT TARGET xxfclib::xxfclib)
        set(XXFC_BUILD_PROFILE "formats" CACHE STRING "xxfclib source profile")
        add_subdirectory("${XXWIDGETS_XXFCLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xxfclib-inspect")
    endif()
    add_library(xxwidgets_inspect STATIC src/xxwidgets_binary_inspect.c)
    add_library(xxwidgets::inspect ALIAS xxwidgets_inspect)
    set_target_properties(xxwidgets_inspect PROPERTIES EXPORT_NAME inspect POSITION_INDEPENDENT_CODE ON)
    target_compile_features(xxwidgets_inspect PUBLIC c_std_11)
    target_link_libraries(xxwidgets_inspect PUBLIC xxwidgets::xxwidgets xxfclib::xxfclib)
    if(MSVC)
        target_compile_options(xxwidgets_inspect PRIVATE /W4 /utf-8)
    else()
        target_compile_options(xxwidgets_inspect PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(XXWIDGETS_BUILD_SETTINGS)
    include("${XXWIDGETS_XXFCLIB_DIR}/xxsettings.cmake")
    add_library(xxwidgets_xxsettings STATIC ${XXSETTINGS_SOURCES})
    set_target_properties(xxwidgets_xxsettings PROPERTIES EXPORT_NAME xxsettings POSITION_INDEPENDENT_CODE ON)
    target_compile_features(xxwidgets_xxsettings PUBLIC c_std_11)
    target_compile_definitions(xxwidgets_xxsettings PUBLIC XXFC_STATIC)
    target_include_directories(xxwidgets_xxsettings PUBLIC
        $<BUILD_INTERFACE:${XXSETTINGS_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(xxwidgets_xxsettings PUBLIC ${XXSETTINGS_LIBRARIES})
    add_library(xxwidgets_settings STATIC src/xxwidgets_settings.c)
    add_library(xxwidgets::settings ALIAS xxwidgets_settings)
    set_target_properties(xxwidgets_settings PROPERTIES EXPORT_NAME settings POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(xxwidgets_settings PUBLIC xxwidgets::xxwidgets xxwidgets_xxsettings)
    if(MSVC)
        target_compile_options(xxwidgets_xxsettings PRIVATE /utf-8)
        target_compile_options(xxwidgets_settings PRIVATE /W4 /utf-8)
        target_compile_definitions(xxwidgets_xxsettings PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(xxwidgets_settings PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

set(_xxwidgets_demo_targets)
function(xxwidgets_add_demo target source)
    add_executable(${target} ${source} examples/demo_common.h)
    target_link_libraries(${target} PRIVATE xxwidgets::xxwidgets)
    if(_xxwidgets_native STREQUAL "cocoa")
        target_sources(${target} PRIVATE examples/demo_cocoa_smoke.m)
        target_compile_definitions(${target} PRIVATE _DARWIN_C_SOURCE XXWIDGETS_DEMO_COCOA)
        target_link_libraries(${target} PRIVATE "-framework AppKit")
    endif()
    if(WIN32)
        # Native modal smoke checks in demo_common.h use this system library.
        target_link_libraries(${target} PRIVATE user32)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_definitions(${target} PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    set(_xxwidgets_demo_targets ${_xxwidgets_demo_targets} ${target} PARENT_SCOPE)
endfunction()

if(XXWIDGETS_BUILD_EXAMPLES)
    xxwidgets_add_demo(xxwidgets_demo examples/widgets_demo.c)
    foreach(_xxwidgets_control IN ITEMS window label button edit checkbox listbox progress)
        xxwidgets_add_demo(xxwidgets_${_xxwidgets_control}_demo examples/control_demo.c)
        string(TOUPPER "${_xxwidgets_control}" _xxwidgets_control_kind)
        target_compile_definitions(xxwidgets_${_xxwidgets_control}_demo PRIVATE
            XXWIDGETS_DEMO_KIND=XXWIDGETS_${_xxwidgets_control_kind})
    endforeach()
    foreach(_xxwidgets_view IN ITEMS hexview archiveview archivebrowser scanresults treeview combobox options about process)
        xxwidgets_add_demo(xxwidgets_${_xxwidgets_view}_demo examples/${_xxwidgets_view}_demo.c)
    endforeach()
    xxwidgets_add_demo(xxwidget_systemprocessesviewer_demo examples/systemprocessesviewer_demo.c)
    if(XXWIDGETS_BUILD_INSPECTORS)
        xxwidgets_add_demo(xxwidgets_binaryviewer_demo examples/binaryviewer_demo.c)
        target_link_libraries(xxwidgets_binaryviewer_demo PRIVATE xxwidgets::inspect)
        foreach(_xxwidgets_binary IN ITEMS resources imports exports symbols)
            xxwidgets_add_demo(xxwidget_${_xxwidgets_binary}viewer_demo examples/binaryviewer_demo.c)
            target_link_libraries(xxwidget_${_xxwidgets_binary}viewer_demo PRIVATE xxwidgets::inspect)
            string(TOUPPER "${_xxwidgets_binary}" _xxwidgets_binary_kind)
            target_compile_definitions(xxwidget_${_xxwidgets_binary}viewer_demo PRIVATE XXWIDGETS_BINARY_DEMO_KIND=XXWIDGET_${_xxwidgets_binary_kind}VIEWER)
        endforeach()
    endif()
    xxwidgets_add_demo(xxwidgets_entropy_demo examples/entropy_demo.c)
    if(APPLE)
        set_target_properties(xxwidgets_hexview_demo PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER "org.xxwidgets.hexview-demo" MACOSX_BUNDLE_BUNDLE_NAME "Hex view")
        set_target_properties(xxwidgets_entropy_demo PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_GUI_IDENTIFIER "org.xxwidgets.entropy-demo" MACOSX_BUNDLE_BUNDLE_NAME "Entropy")
    endif()
    xxwidgets_add_demo(xxwidgets_scan_panel_demo examples/scan_panel_demo.c)
    xxwidgets_add_demo(xxwidgets_scan_options_demo examples/scan_options_demo.c)
    xxwidgets_add_demo(xxwidgets_font_options_demo examples/font_options_demo.c)
    xxwidgets_add_demo(xxwidgets_context_options_demo examples/context_options_demo.c)
    xxwidgets_add_demo(xxwidgets_optimization_options_demo examples/optimization_options_demo.c)
    if(XXWIDGETS_BUILD_SETTINGS)
        xxwidgets_add_demo(xxwidgets_settings_demo examples/settings_demo.c)
        target_link_libraries(xxwidgets_settings_demo PRIVATE xxwidgets::settings)
    endif()
endif()

if(XXWIDGETS_BUILD_TESTS AND IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    include(CTest)
    if(BUILD_TESTING)
        # Deterministic API/lifecycle tests don't require a display or terminal.
        add_executable(xxwidgets_core_tests
            $<TARGET_OBJECTS:xxwidgets_scan_colors>
    $<TARGET_OBJECTS:xxwidgets_codepages>
            tests/test_core.c
            src/xxwidgets.c
            src/xxwidgets_edit.c
            src/xxwidgets_window.c
            src/xxwidgets_listbox.c
            src/xxwidgets_archiveview.c
            src/xxwidgets_hex_options.c
            src/xxwidgets_hexview.c
    src/xxwidgets_hex_columns.c
            src/xxwidgets_archivebrowser.c
            src/xxwidgets_dialog.c
            src/xxwidgets_about.c
            src/xxwidgets_combobox.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_process.c tests/test_process.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidget_systemprocessesviewer.c src/xxwidget_systemprocessesviewer_platform.c tests/test_systemprocessesviewer.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidget_binaryviewer.c src/xxwidget_resourcesviewer.c src/xxwidget_importsviewer.c src/xxwidget_exportsviewer.c src/xxwidget_symbolsviewer.c tests/test_binaryviewers.c)
        if(APPLE)
            target_compile_definitions(xxwidgets_core_tests PRIVATE _DARWIN_C_SOURCE)
        endif()
        if(WIN32)
            target_link_libraries(xxwidgets_core_tests PRIVATE shell32)
        endif()
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_font_options.c)
        target_include_directories(xxwidgets_core_tests PRIVATE include src "${XXWIDGETS_XXFCLIB_DIR}/include")
        target_compile_features(xxwidgets_core_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_STATIC XXWIDGETS_TEST_CLOCK XXFC_STATIC)
        if(UNIX)
            target_compile_definitions(xxwidgets_core_tests PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_shortcuts.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_scanresults.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_data_struct_table.c src/xxwidgets_data_struct_view.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_treeview.c src/xxwidgets_scan_panel.c tests/test_scan_panel.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_shortcuts.c tests/test_hex_columns.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_file_drops.c tests/test_canvas.c src/xxwidgets_canvas.c src/xxwidgets_entropy.c tests/test_entropy.c)
        if(UNIX)
            target_link_libraries(xxwidgets_core_tests PRIVATE m)
        endif()
        if(XXWIDGETS_BUILD_SETTINGS)
            target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_settings.c)
            target_link_libraries(xxwidgets_core_tests PRIVATE xxwidgets_xxsettings)
            target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_WITH_SETTINGS)
        endif()
        add_test(NAME xxwidgets_core COMMAND xxwidgets_core_tests)
        # Reuse the deterministic core's production sources with an independent
        # fake backend so search modal transactions run without a desktop.
        get_target_property(_xxwidgets_search_sources xxwidgets_core_tests SOURCES)
        list(FILTER _xxwidgets_search_sources EXCLUDE REGEX "^tests/")
        add_executable(xxwidgets_search_tests tests/test_search_dialog.c
            src/xxwidgets_search.c src/xxwidgets_forms.c ${_xxwidgets_search_sources})
        target_include_directories(xxwidgets_search_tests PRIVATE include src "${XXWIDGETS_XXFCLIB_DIR}/include")
        target_compile_features(xxwidgets_search_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_search_tests PRIVATE XXWIDGETS_STATIC XXWIDGETS_TEST_CLOCK XXFC_STATIC)
        if(UNIX)
            target_compile_definitions(xxwidgets_search_tests PRIVATE _POSIX_C_SOURCE=200809L)
            target_link_libraries(xxwidgets_search_tests PRIVATE m)
        endif()
        if(XXWIDGETS_BUILD_SETTINGS)
            target_link_libraries(xxwidgets_search_tests PRIVATE xxwidgets_xxsettings)
            target_compile_definitions(xxwidgets_search_tests PRIVATE XXWIDGETS_WITH_SETTINGS)
        endif()
        add_test(NAME xxwidgets_search COMMAND xxwidgets_search_tests)
        set_tests_properties(xxwidgets_search PROPERTIES TIMEOUT 15 LABELS "smoke;search")
        if(EXISTS "${XXWIDGETS_XXFCLIB_DIR}/tests/test_codepages.c")
            add_executable(xxwidgets_codepage_tests "${XXWIDGETS_XXFCLIB_DIR}/tests/test_codepages.c" $<TARGET_OBJECTS:xxwidgets_codepages>)
            target_include_directories(xxwidgets_codepage_tests PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
            target_compile_definitions(xxwidgets_codepage_tests PRIVATE XXFC_STATIC)
            add_test(NAME xxwidgets_codepages COMMAND xxwidgets_codepage_tests)
        endif()
        if(_xxwidgets_native STREQUAL "cocoa")
            add_executable(xxwidgets_cocoa_widgets_tests tests/test_cocoa_widgets.m)
            target_compile_options(xxwidgets_cocoa_widgets_tests PRIVATE -fno-objc-arc)
            target_include_directories(xxwidgets_cocoa_widgets_tests PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
            target_link_libraries(xxwidgets_cocoa_widgets_tests PRIVATE xxwidgets::xxwidgets "-framework AppKit")
            add_test(NAME xxwidgets_cocoa_widgets COMMAND xxwidgets_cocoa_widgets_tests)
            set_tests_properties(xxwidgets_cocoa_widgets PROPERTIES TIMEOUT 30)
            add_executable(xxwidgets_cocoa_hex_tests tests/test_cocoa_hex.m)
            target_compile_options(xxwidgets_cocoa_hex_tests PRIVATE -fno-objc-arc)
            target_link_libraries(xxwidgets_cocoa_hex_tests PRIVATE xxwidgets::xxwidgets "-framework AppKit")
            add_test(NAME xxwidgets_cocoa_hex COMMAND xxwidgets_cocoa_hex_tests)
            add_executable(xxwidgets_cocoa_drop_tests tests/test_cocoa_drop.m)
            target_compile_options(xxwidgets_cocoa_drop_tests PRIVATE -fno-objc-arc)
            target_link_libraries(xxwidgets_cocoa_drop_tests PRIVATE xxwidgets::xxwidgets "-framework AppKit")
            add_test(NAME xxwidgets_cocoa_drop COMMAND xxwidgets_cocoa_drop_tests)
            set_tests_properties(xxwidgets_cocoa_drop PROPERTIES TIMEOUT 30)
            set_tests_properties(xxwidgets_cocoa_widgets xxwidgets_cocoa_hex xxwidgets_cocoa_drop
                PROPERTIES RESOURCE_LOCK xxwidgets_macos_display)
        endif()
        if(_xxwidgets_native STREQUAL "win32")
            add_executable(xxwidgets_win32_tests tests/test_win32.c)
            target_include_directories(xxwidgets_win32_tests PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
            target_compile_definitions(xxwidgets_win32_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
            target_link_libraries(xxwidgets_win32_tests PRIVATE xxwidgets::xxwidgets user32 gdi32 comctl32)
            add_test(NAME xxwidgets_win32 COMMAND xxwidgets_win32_tests)
            set_tests_properties(xxwidgets_win32 PROPERTIES TIMEOUT 30)
            add_test(NAME xxwidgets_win32_data_struct COMMAND xxwidgets_win32_tests --data-struct-table)
            set_tests_properties(xxwidgets_win32_data_struct PROPERTIES TIMEOUT 15)
        endif()
        if(_xxwidgets_native STREQUAL "win32")
            foreach(_xxwidgets_options IN ITEMS scan_options font_options context_options optimization_options)
                add_executable(xxwidgets_${_xxwidgets_options}_tests tests/test_${_xxwidgets_options}.c)
                target_link_libraries(xxwidgets_${_xxwidgets_options}_tests PRIVATE xxwidgets::xxwidgets)
                target_link_libraries(xxwidgets_${_xxwidgets_options}_tests PRIVATE user32 gdi32 comctl32 advapi32)
                add_test(NAME xxwidgets_${_xxwidgets_options} COMMAND xxwidgets_${_xxwidgets_options}_tests)
                set_tests_properties(xxwidgets_${_xxwidgets_options} PROPERTIES TIMEOUT 30)
            endforeach()
        endif()
        if(_xxwidgets_native STREQUAL "cocoa")
            add_executable(xxwidgets_optimization_options_tests tests/test_optimization_options.c)
            target_link_libraries(xxwidgets_optimization_options_tests PRIVATE xxwidgets::xxwidgets)
            add_test(NAME xxwidgets_optimization_options COMMAND xxwidgets_optimization_options_tests)
            set_tests_properties(xxwidgets_optimization_options PROPERTIES TIMEOUT 30 RESOURCE_LOCK xxwidgets_macos_display)
        endif()
        if(WIN32)
            add_executable(xxwidgets_tui_tests tests/test_tui_win32.c)
            target_include_directories(xxwidgets_tui_tests PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
            target_compile_definitions(xxwidgets_tui_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
            target_link_libraries(xxwidgets_tui_tests PRIVATE xxwidgets::xxwidgets user32)
            add_test(NAME xxwidgets_tui COMMAND xxwidgets_tui_tests)
            set_tests_properties(xxwidgets_tui PROPERTIES TIMEOUT 15)
            add_test(NAME xxwidgets_tui_data_struct COMMAND xxwidgets_tui_tests --data-struct-table)
            set_tests_properties(xxwidgets_tui_data_struct PROPERTIES TIMEOUT 15)
        endif()
    endif()
endif()

if(XXWIDGETS_BUILD_TESTS AND BUILD_TESTING)
    if(UNIX AND TARGET xxwidgets_entropy_demo)
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            add_test(NAME xxwidgets_hex_tui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hex_tui.py" $<TARGET_FILE:xxwidgets_hexview_demo>)
            set_tests_properties(xxwidgets_hex_tui PROPERTIES TIMEOUT 20)
            add_test(NAME xxwidgets_entropy_tui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_entropy_tui.py" $<TARGET_FILE:xxwidgets_entropy_demo>)
            set_tests_properties(xxwidgets_entropy_tui PROPERTIES TIMEOUT 20)
        endif()
    endif()
    foreach(_xxwidgets_demo IN LISTS _xxwidgets_demo_targets)
        add_test(NAME ${_xxwidgets_demo}_help COMMAND ${_xxwidgets_demo} --help)
        if(_xxwidgets_native STREQUAL "win32" OR
           (_xxwidgets_native STREQUAL "cocoa" AND NOT _xxwidgets_demo STREQUAL "xxwidgets_context_options_demo") OR
           (_xxwidgets_demo STREQUAL "xxwidgets_entropy_demo" AND NOT _xxwidgets_native STREQUAL "none"))
            add_test(NAME ${_xxwidgets_demo}_smoke COMMAND ${_xxwidgets_demo} --native --smoke-test)
            set_tests_properties(${_xxwidgets_demo}_smoke PROPERTIES TIMEOUT 15)
            if(_xxwidgets_native STREQUAL "cocoa")
                set_tests_properties(${_xxwidgets_demo}_smoke PROPERTIES RESOURCE_LOCK xxwidgets_macos_display)
            endif()
        endif()
    endforeach()
endif()

install(TARGETS xxwidgets EXPORT xxwidgetsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(XXWIDGETS_BUILD_INSPECTORS)
    install(TARGETS xxwidgets_inspect EXPORT xxwidgetsTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if(XXWIDGETS_BUILD_SETTINGS)
    install(TARGETS xxwidgets_settings xxwidgets_xxsettings EXPORT xxwidgetsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
    install(FILES "${XXSETTINGS_INCLUDE_DIR}/xxfclib/xxfc_defs.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xxfclib)
    install(FILES "${XXSETTINGS_INCLUDE_DIR}/xxfclib/settings/xx_settings.h"
        "${XXSETTINGS_INCLUDE_DIR}/xxfclib/settings/xx_shortcuts.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xxfclib/settings)
    install(FILES "${XXSETTINGS_INCLUDE_DIR}/xxfclib/global/xx_settings_global.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xxfclib/global)
    install(FILES include/xxwidgets/xxwidgets_settings.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/xxwidgets)
endif()
install(DIRECTORY include/xxwidgets DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    PATTERN "xxwidgets_settings.h" EXCLUDE)
# The typed controls use xxfclib's public record definitions without linking its
# format engines. Install the small transitive header set for package consumers.
foreach(_xxwidgets_type_header
    xxfclib/formats/xx_format.h xxfclib/formats/xx_format_streams.h
    xxfclib/formats/xx_format_reader_only_abstract_extractor_decls.inc
    xxfclib/formats/xx_uniextract_enums.inc
    xxfclib/formats/xx_format_gap_enums.inc
    xxfclib/formats/xx_format_gap_aliases.inc
    xxfclib/formats/audio/die_music/xx_die_music_enums.inc
    xxfclib/formats/xx_memory_map.h
    xxfclib/data/xx_pd.h xxfclib/io/xx_io.h xxfclib/list/xx_list.h
    xxfclib/strings/xx_string.h xxfclib/var/xx_var.h xxfclib/xxfc_defs.h)
    get_filename_component(_xxwidgets_type_dir "${_xxwidgets_type_header}" DIRECTORY)
    install(FILES "${XXWIDGETS_XXFCLIB_DIR}/include/${_xxwidgets_type_header}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_xxwidgets_type_dir}")
endforeach()
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES examples/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
install(FILES docs/process-dialog-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/search-dialog-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/forms-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
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
