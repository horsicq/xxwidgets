cmake_minimum_required(VERSION 3.20)

project(xxwidgets
    VERSION 0.1.0
    DESCRIPTION "C widgets with native desktop controls and a terminal backend"
    LANGUAGES C
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
if(WIN32)
    set(_xxwidgets_scan_directory_source src/scan/xxwidgets_scan_directory_win32.c)
    set(_xxwidgets_process_dump_source src/platform/xxwidget_systemprocessesviewer_dump_win32.c)
    set(_xxwidgets_process_memory_source src/platform/xxwidget_systemprocessesviewer_memory_win32.c)
else()
    set(_xxwidgets_scan_directory_source src/scan/xxwidgets_scan_directory_posix.c)
    set(_xxwidgets_process_dump_source src/platform/xxwidget_systemprocessesviewer_dump_posix.c)
    set(_xxwidgets_process_memory_source src/platform/xxwidget_systemprocessesviewer_memory_posix.c)
endif()
set(_xxwidgets_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_xxwidgets_top_level ON)
endif()

option(BUILD_SHARED_LIBS "Build xxwidgets as a shared library" OFF)
option(XXWIDGETS_BUILD_EXAMPLES "Build the native/TUI widget demos" ${_xxwidgets_top_level})
option(XXWIDGETS_BUILD_TESTS "Build xxwidgets tests" ${_xxwidgets_top_level})
option(XXWIDGETS_BUILD_SETTINGS "Build the xxfclib settings adapter for options dialogs" OFF)
option(XXWIDGETS_BUILD_INSPECTORS "Build native binary inspection using xxfclib" ${_xxwidgets_top_level})
option(XXWIDGETS_BUILD_DEBUGGER "Build the optional xxdebug session adapter" OFF)
option(XXWIDGETS_BUILD_EMULATOR "Build the optional direct xxemul session adapter" OFF)
option(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER "Build the optional DIE signature debugger" OFF)
option(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER_DIE_ENGINE "Build the actual DIE engine adapter for the signature debugger" ON)
set(XXWIDGETS_DEBUGGER_MACHO_FIXTURE "" CACHE FILEPATH "Unoptimized ARM64 C++ hello Mach-O for optional debugger runtime tests")
set(XXWIDGETS_XXDEBUG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xxdebug" CACHE PATH "xxdebug source tree for the optional debugger adapter")
set(XXWIDGETS_XXEMUL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xxemul" CACHE PATH "xxemul source tree for the optional emulator adapter")
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
include("${XXWIDGETS_XXFCLIB_DIR}/xxio.cmake")
# File-backed widgets need the complete standalone I/O dependency closure:
# wide-path conversion uses strings, whose binary readers use data/progress.
# These runtime sources keep editor builds independent of formats and JS.
list(APPEND XXIO_SOURCES
    "${XXWIDGETS_XXFCLIB_DIR}/src/strings/xx_string.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/data/xx_data_io.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/data/xx_data_raw.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/data/xx_pd.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/data/platforms/xx_data_sse2.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/data/platforms/xx_data_avx2.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/data/platforms/xx_data_neon.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/rt/xx_rt.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/rt/xx_rt_fp.c"
    "${XXWIDGETS_XXFCLIB_DIR}/src/rt/xx_rt_math.c")
if(WIN32)
    list(APPEND XXIO_SOURCES "${XXWIDGETS_XXFCLIB_DIR}/src/rt/platforms/xx_rt_windows.c")
endif()
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
    set_property(SOURCE "${XXWIDGETS_XXFCLIB_DIR}/src/data/platforms/xx_data_avx2.c"
        APPEND PROPERTY COMPILE_OPTIONS "/arch:AVX2")
endif()
add_library(xxwidgets_io OBJECT ${XXIO_SOURCES})
target_compile_features(xxwidgets_io PRIVATE c_std_11)
target_compile_definitions(xxwidgets_io PRIVATE XXFC_STATIC ${XXIO_COMPILE_DEFINITIONS})
target_include_directories(xxwidgets_io PRIVATE "${XXIO_INCLUDE_DIR}")
set_target_properties(xxwidgets_io PROPERTIES POSITION_INDEPENDENT_CODE ON)

include("${XXWIDGETS_XXFCLIB_DIR}/xxtranslation.cmake")
add_library(xxwidgets_translation OBJECT ${XXTRANSLATION_SOURCES} ${XXTRANSLATION_IO_SOURCES})
target_compile_features(xxwidgets_translation PRIVATE c_std_11)
target_compile_definitions(xxwidgets_translation PRIVATE XXFC_STATIC)
target_include_directories(xxwidgets_translation PRIVATE "${XXTRANSLATION_INCLUDE_DIR}")
set_target_properties(xxwidgets_translation PROPERTIES POSITION_INDEPENDENT_CODE ON)

include("${XXWIDGETS_XXFCLIB_DIR}/xxhistory.cmake")
add_library(xxwidgets_history OBJECT ${XXHISTORY_SOURCES})
target_compile_features(xxwidgets_history PRIVATE c_std_11)
target_compile_definitions(xxwidgets_history PRIVATE XXFC_STATIC)
target_include_directories(xxwidgets_history PRIVATE "${XXHISTORY_INCLUDE_DIR}")
set_target_properties(xxwidgets_history PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Device edits use the same small thread runtime as xxfclib consumers. Keep
# standalone xxwidgets builds independent of a full xxfclib library build.
set(_xxwidgets_threads_sources "${XXWIDGETS_XXFCLIB_DIR}/src/threads/xx_threads.c")
if(WIN32)
    list(APPEND _xxwidgets_threads_sources "${XXWIDGETS_XXFCLIB_DIR}/src/threads/platforms/xx_threads_windows.c")
else()
    list(APPEND _xxwidgets_threads_sources "${XXWIDGETS_XXFCLIB_DIR}/src/threads/platforms/xx_threads_posix.c")
endif()
find_package(Threads REQUIRED)
add_library(xxwidgets_threads OBJECT ${_xxwidgets_threads_sources})
target_compile_features(xxwidgets_threads PRIVATE c_std_11)
target_compile_definitions(xxwidgets_threads PRIVATE XXFC_STATIC)
target_include_directories(xxwidgets_threads PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
set_target_properties(xxwidgets_threads PROPERTIES POSITION_INDEPENDENT_CODE ON)

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
    $<TARGET_OBJECTS:xxwidgets_io>
    $<TARGET_OBJECTS:xxwidgets_history>
    $<TARGET_OBJECTS:xxwidgets_translation>
    $<TARGET_OBJECTS:xxwidgets_threads>
    $<TARGET_OBJECTS:xxwidgets_scan_colors>
    $<TARGET_OBJECTS:xxwidgets_codepages>
    include/xxwidgets/xxwidgets.h
    include/xxwidgets/xxwidgets_translation.h
    include/xxwidgets/die_engine/xxwidgets_die_signature_edit.h
    include/xxwidgets/die_engine/xxwidgets_die_signature_editor.h
    include/xxwidgets/die_engine/xxwidgets_die_signature_debug_session.h
    include/xxwidgets/die_engine/xxwidgets_die_signature_debugger.h
    include/xxwidgets/die_engine/xxwidgets_die_signature_debug_die_engine.h
    src/die_engine/xxwidgets_die_signature_edit_internal.h
    src/die_engine/xxwidgets_die_signature_edit.c
    src/die_engine/xxwidgets_die_signature_editor.c
    include/xxwidgets/xxwidgets_combobox.h
    include/xxwidgets/xxwidgets_data_struct_table.h
    include/xxwidgets/xxwidgets_data_struct_view.h
    include/xxwidgets/xxwidgets_file_information.h
    include/xxwidgets/xxwidgets_debugger.h
    include/xxwidgets/xxwidgets_debugger_arch.h
    include/xxwidgets/xxwidgets_debugger_session.h
    include/xxwidgets/xxwidgets_debugger_tables.h
    include/xxwidgets/debugger/xxwidgets_debugger_project_dialog.h
    include/xxwidgets/debugger/xxwidgets_debugger_trace_dialog.h
    include/xxwidgets/debugger/xxwidgets_debugger_arguments.h
    include/xxwidgets/xxwidgets_canvas.h
    include/xxwidgets/xxwidgets_hash.h
    include/xxwidgets/xxwidgets_entropy.h
    include/xxwidgets/xxwidgets_resources.h
    include/xxwidgets/xxwidgets_symbols.h
    include/xxwidgets/xxwidgets_exports.h
    include/xxwidgets/xxwidgets_imports.h
    include/xxwidgets/xxwidgets_memory_map.h
    include/xxwidgets/xxwidgets_disasm.h
    include/xxwidgets/xxwidgets_disasm_options.h
    include/xxwidgets/xxwidgets_hexview_browser.h
    include/xxwidgets/xxwidgets_hex_options.h
    include/xxwidgets/xxwidgets_process.h
    include/xxwidgets/xxwidget_systemprocessesviewer.h
    include/xxwidgets/xxwidget_binaryviewer.h
    include/xxwidgets/xxwidget_resourcesviewer.h
    include/xxwidgets/xxwidget_importsviewer.h
    include/xxwidgets/xxwidget_exportsviewer.h
    include/xxwidgets/xxwidget_symbolsviewer.h
    include/xxwidgets/xxwidgets_scan_directory.h
    include/xxwidgets/xxwidgets_scan_panel.h
    include/xxwidgets/xxwidgets_scan_options.h
    include/xxwidgets/xxwidgets_search.h
    include/xxwidgets/xxwidgets_strings.h
    include/xxwidgets/xxwidgets_extractor.h
    include/xxwidgets/xxwidgets_disk_write.h
    include/xxwidgets/xxwidgets_forms.h
    include/xxwidgets/xxwidgets_font_options.h
    include/xxwidgets/xxwidgets_database_paths.h
    include/xxwidgets/xxwidgets_context_options.h
    include/xxwidgets/xxwidgets_optimization_options.h
    include/xxwidgets/xxwidgets_options_pages.h
    src/xxwidgets_internal.h
    src/xxwidgets.c
    src/xxwidgets_translation.c
    src/xxwidgets_edit.c
    src/xxwidgets_window.c
    src/xxwidgets_modality.c
    src/xxwidgets_menu.c
    src/debugger/xxwidgets_debugger_project_dialog.c
    src/debugger/xxwidgets_debugger_trace_dialog.c
    src/debugger/xxwidgets_debugger_arguments.c
    src/xxwidgets_listbox.c
    src/xxwidgets_archiveview.c
    src/xxwidgets_resources.c
    src/xxwidgets_resources_dialog.c
    src/xxwidgets_symbols.c
    src/xxwidgets_symbols_dialog.c
    src/xxwidgets_exports.c
    src/xxwidgets_exports_dialog.c
    src/xxwidgets_imports.c
    src/xxwidgets_imports_dialog.c
    src/xxwidgets_memory_map.c
    src/xxwidgets_memory_map_dialog.c
    src/xxwidgets_memory_map_export.c
    src/xxwidgets_disasm.c
    src/xxwidgets_disasm_options.c
    src/xxwidgets_disasm_dialog.c
    src/xxwidgets_hexview_browser.c
    src/xxwidgets_hexview_dialog.c
    src/xxwidgets_hex_options.c
    src/xxwidgets_hexview.c
    src/xxwidgets_hex_edit.c
    src/xxwidgets_hex_paste.c
    src/xxwidgets_hex_cut.c
    src/xxwidgets_hex_columns.c
    src/xxwidgets_shortcuts.c
    src/xxwidgets_archivebrowser.c
    src/xxwidgets_dialog.c
    src/xxwidgets_about.c
    src/xxwidgets_open_url.c
    src/xxwidgets_table_filter.c
    src/xxwidgets_table.c
    src/xxwidgets_process.c
    src/xxwidgets_disk_write.c
    src/xxwidgets_file_options.c
    src/xxwidget_systemprocessesviewer.c
    src/xxwidget_systemprocessesviewer_hexview.c
    ${_xxwidgets_process_memory_source}
    src/platform/xxwidget_systemprocessesviewer_platform.c
    ${_xxwidgets_process_dump_source}
    src/xxwidget_binaryviewer.c
    src/xxwidget_resourcesviewer.c
    src/xxwidget_importsviewer.c
    src/xxwidget_exportsviewer.c
    src/xxwidget_symbolsviewer.c
    src/xxwidgets_combobox.c
    src/xxwidgets_data_struct_table.c
    src/xxwidgets_data_struct_view.c
    src/xxwidgets_file_information.c
    src/xxwidgets_extractor.c
    src/xxwidgets_debugger.c
    src/xxwidgets_debugger_tables.c
    src/xxwidgets_debugger_session.c
    src/xxwidgets_debugger/xxwidgets_debugger_arch_internal.h
    src/xxwidgets_debugger/xxwidgets_debugger_arch.c
    src/xxwidgets_debugger/xxwidgets_debugger_x86_16.c
    src/xxwidgets_debugger/xxwidgets_debugger_x86_32.c
    src/xxwidgets_debugger/xxwidgets_debugger_x86_64.c
    src/xxwidgets_debugger/xxwidgets_debugger_arm32.c
    src/xxwidgets_debugger/xxwidgets_debugger_arm64.c
    src/xxwidgets_canvas.c
    src/xxwidgets_hash.c
    src/xxwidgets_hash_dialog.c
    src/xxwidgets_entropy_dialog.c
    src/xxwidgets_entropy.c
    src/scan/xxwidgets_scanresults.c
    src/xxwidgets_treeview.c
    src/scan/xxwidgets_scan_panel.c
    src/scan/xxwidgets_scan_directory.c
    ${_xxwidgets_scan_directory_source}
    src/scan/xxwidgets_scan_options.c
    src/xxwidgets_search.c
    src/xxwidgets_strings_dialog.c
    src/xxwidgets_strings.c
    src/xxwidgets_forms.c
    src/xxwidgets_font_options.c
    src/xxwidgets_database_paths.c
    src/xxwidgets_context_options.c
    src/xxwidgets_optimization_options.c
    src/xxwidgets_options_pages.c
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
    target_compile_definitions(xxwidgets PUBLIC XXWIDGETS_STATIC XXFC_STATIC)
endif()
target_link_libraries(xxwidgets PRIVATE ${_xxwidgets_backend_libs} ${XXIO_LIBRARIES} Threads::Threads)
if(WIN32)
    target_compile_definitions(xxwidgets PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(xxwidgets PRIVATE comdlg32 advapi32 shell32 ole32 user32)
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

# Configure the full engine before the inspectors can select a formats-only
# xxfclib profile. EXCLUDE_FROM_ALL builds only its focused DIE dependency
# closure when the signature debugger is requested.
if(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER)
    if(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER_DIE_ENGINE)
        if(NOT TARGET xxfclib::die_engine)
            if(TARGET xxfclib::xxfclib)
                message(FATAL_ERROR "Enable XXFC_BUILD_DIE_ENGINE in the parent xxfclib build before adding the signature debugger, or disable XXWIDGETS_BUILD_SIGNATURE_DEBUGGER_DIE_ENGINE")
            endif()
            function(xxwidgets_add_signature_engine_dependency)
                set(BUILD_SHARED_LIBS OFF)
                set(BUILD_TESTING OFF)
                set(XXFC_BUILD_PROFILE full)
                set(XXFC_BUILD_DIE_ENGINE ON)
                set(XXFC_BUILD_BOTH OFF)
                set(XXFC_BUILD_TESTS OFF)
                set(XXFC_BUILD_SAMPLES OFF)
                # Signature APIs inspect formats directly; they do not use
                # the optional structure-description catalog.
                set(XXFC_ENABLE_STREAM_DATASTRUCTS OFF)
                add_subdirectory("${XXWIDGETS_XXFCLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xxfclib-signatures" EXCLUDE_FROM_ALL)
            endfunction()
            xxwidgets_add_signature_engine_dependency()
        endif()
        set(_xxwidgets_signature_js_target xxfclib::die_engine)
    else()
        # The generic session needs the interpreter, buffers, lists and cache
        # checksums; its remaining runtime comes from base xxwidgets.
        add_library(xxwidgets_signature_js STATIC
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_builtins.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_bytecode.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_deps.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_engine.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_interp.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_lex.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_parse.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/regex/xx_regex.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/regex/xx_regex_search.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/regex/xx_regex_js.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/regex/xx_regex_js_lex.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/regex/xx_regex_js_strings.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/js/xx_js_value.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/buf/xx_buf.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/list/xx_list.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/xx_crc.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/xx_crc8.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/xx_crc16.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/xx_crc32.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/xx_crc64.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/platforms/xx_crc32_arm.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/platforms/xx_crc32_arm_pmull.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/platforms/xx_crc32c_sse42.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/platforms/xx_crc64_sse2.c"
            "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/platforms/xx_crc64_avx2.c")
        include("${XXWIDGETS_XXFCLIB_DIR}/cmake/xxfclib_arm_accel.cmake")
        xxfclib_configure_arm_accel_sources("${XXWIDGETS_XXFCLIB_DIR}")
        if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID MATCHES "^(X86|x86|x64|AMD64)$")
            set_property(SOURCE "${XXWIDGETS_XXFCLIB_DIR}/src/algo/crc/platforms/xx_crc64_avx2.c"
                APPEND PROPERTY COMPILE_OPTIONS "/arch:AVX2")
        endif()
        target_compile_features(xxwidgets_signature_js PUBLIC c_std_11)
        target_compile_definitions(xxwidgets_signature_js PUBLIC XXFC_STATIC)
        target_include_directories(xxwidgets_signature_js PUBLIC "${XXWIDGETS_XXFCLIB_DIR}/include")
        set_target_properties(xxwidgets_signature_js PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_link_libraries(xxwidgets_signature_js PUBLIC xxwidgets::xxwidgets)
        set(_xxwidgets_signature_js_target xxwidgets_signature_js)
    endif()
    add_library(xxwidgets_signature_debugger STATIC
        src/die_engine/xxwidgets_die_signature_debug_session.c
        src/die_engine/xxwidgets_die_signature_debugger.c)
    add_library(xxwidgets::signature_debugger ALIAS xxwidgets_signature_debugger)
    set_target_properties(xxwidgets_signature_debugger PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(xxwidgets_signature_debugger PUBLIC c_std_11)
    target_compile_definitions(xxwidgets_signature_debugger PUBLIC XXWIDGETS_STATIC PRIVATE XXFC_STATIC)
    target_link_libraries(xxwidgets_signature_debugger PUBLIC xxwidgets::xxwidgets PRIVATE ${_xxwidgets_signature_js_target} Threads::Threads)
    if(MSVC)
        target_compile_options(xxwidgets_signature_debugger PRIVATE /W4 /utf-8)
    else()
        target_compile_options(xxwidgets_signature_debugger PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    if(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER_DIE_ENGINE)
        add_library(xxwidgets_signature_debugger_die_engine STATIC src/die_engine/xxwidgets_die_signature_debug_die_engine.c)
        add_library(xxwidgets::signature_debugger_die_engine ALIAS xxwidgets_signature_debugger_die_engine)
        set_target_properties(xxwidgets_signature_debugger_die_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
        target_compile_features(xxwidgets_signature_debugger_die_engine PUBLIC c_std_11)
        target_compile_definitions(xxwidgets_signature_debugger_die_engine PUBLIC XXWIDGETS_STATIC PRIVATE XXFC_STATIC)
        target_link_libraries(xxwidgets_signature_debugger_die_engine PUBLIC xxwidgets::signature_debugger xxfclib::die_engine)
        if(MSVC)
            target_compile_options(xxwidgets_signature_debugger_die_engine PRIVATE /W4 /utf-8)
        else()
            target_compile_options(xxwidgets_signature_debugger_die_engine PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
    # Like the native debugger adapters, these are source-tree integration
    # targets; no installed xxfclib CMake package is required by base xxwidgets.
endif()

if(XXWIDGETS_BUILD_INSPECTORS)
    if(NOT TARGET xxfclib::xxfclib)
        set(XXFC_ENABLE_STREAM_DATASTRUCTS OFF)
        set(XXFC_ENABLE_STREAM_IMPORT ON)
        set(XXFC_ENABLE_STREAM_EXPORT ON)
        set(XXFC_ENABLE_STREAM_RESOURCES ON)
        set(XXFC_ENABLE_STREAM_METADATA OFF)
        set(XXFC_ENABLE_STREAM_SYMBOLS ON)
        set(XXFC_BUILD_PROFILE "formats" CACHE STRING "xxfclib source profile")
        add_subdirectory("${XXWIDGETS_XXFCLIB_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xxfclib-inspect")
    endif()
    if(COMMAND xxfclib_require_streams)
        xxfclib_require_streams(xxfclib::xxfclib IMPORT EXPORT RESOURCES SYMBOLS)
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

if(XXWIDGETS_BUILD_DEBUGGER)
    if(NOT TARGET xxdebug::xxdebug)
        # Keep the adapter's dependency build local to this subdirectory.
        function(xxwidgets_add_debugger_dependency)
            set(BUILD_TESTING OFF)
            set(XXDEBUG_BUILD_TOOLS OFF)
            set(XXDEBUG_BUILD_SCRIPT OFF)
            set(XXDEBUG_BUILD_TESTS OFF)
            add_subdirectory("${XXWIDGETS_XXDEBUG_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xxdebug-adapter" EXCLUDE_FROM_ALL)
        endfunction()
        xxwidgets_add_debugger_dependency()
    endif()
    add_library(xxwidgets_debugger STATIC src/xxwidgets_debugger_xxdebug.c
        src/debugger/xxwidgets_debugger_project_xxdebug.c)
    add_library(xxwidgets::debugger ALIAS xxwidgets_debugger)
    set_target_properties(xxwidgets_debugger PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(xxwidgets_debugger PUBLIC c_std_11)
    target_link_libraries(xxwidgets_debugger PUBLIC xxwidgets::xxwidgets xxdebug::xxdebug)
    if(MSVC)
        target_compile_options(xxwidgets_debugger PRIVATE /W4 /utf-8)
    else()
        target_compile_options(xxwidgets_debugger PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    # xxdebug currently has no installed CMake package. This adapter target
    # is available to source-tree consumers; the base widget exports normally.
endif()

if(XXWIDGETS_BUILD_EMULATOR)
    if(NOT TARGET xxemul::xxemul)
        function(xxwidgets_add_emulator_dependency)
            set(BUILD_SHARED_LIBS OFF)
            set(BUILD_TESTING OFF)
            set(XXEMUL_BUILD_TESTS OFF)
            set(XXEMUL_BUILD_CLI OFF)
            set(XXEMUL_BUILD_VIEWER OFF)
            add_subdirectory("${XXWIDGETS_XXEMUL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/xxemul-adapter" EXCLUDE_FROM_ALL)
        endfunction()
        xxwidgets_add_emulator_dependency()
    endif()
    if(NOT TARGET cdisasm::cdisasm)
        find_package(cdisasm CONFIG REQUIRED)
    endif()
    add_library(xxwidgets_emulator STATIC src/xxwidgets_debugger_xxemul.c)
    add_library(xxwidgets::emulator ALIAS xxwidgets_emulator)
    set_target_properties(xxwidgets_emulator PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(xxwidgets_emulator PUBLIC c_std_11)
    target_link_libraries(xxwidgets_emulator PUBLIC xxwidgets::xxwidgets xxemul::xxemul PRIVATE cdisasm::cdisasm)
    if(MSVC)
        target_compile_options(xxwidgets_emulator PRIVATE /W4 /utf-8)
    else()
        target_compile_options(xxwidgets_emulator PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    # Like xxwidgets::debugger, this is a source-tree integration target.
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
    xxwidgets_add_demo(xxwidgets_disk_write_demo examples/disk_write_demo.c)
    # This example writes only an explicit destination supplied by the user.
    list(REMOVE_ITEM _xxwidgets_demo_targets xxwidgets_disk_write_demo)
    if(WIN32 AND MSVC)
        # A GUI sibling is useful for visual QA without a covering console.
        add_executable(xxwidgets_disk_write_demo_gui WIN32 examples/disk_write_demo.c)
        target_link_libraries(xxwidgets_disk_write_demo_gui PRIVATE xxwidgets::xxwidgets)
        target_link_options(xxwidgets_disk_write_demo_gui PRIVATE /ENTRY:mainCRTStartup)
        target_compile_options(xxwidgets_disk_write_demo_gui PRIVATE /W4 /utf-8)
        target_compile_definitions(xxwidgets_disk_write_demo_gui PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    xxwidgets_add_demo(xxwidgets_translation_demo examples/translation_demo.c)
    xxwidgets_add_demo(xxwidgets_die_signature_editor_demo examples/die_engine/signature_editor_demo.c)
    list(REMOVE_ITEM _xxwidgets_demo_targets xxwidgets_die_signature_editor_demo)
    if(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER)
        xxwidgets_add_demo(xxwidgets_die_signature_debugger_demo examples/die_engine/signature_debugger_demo.c)
        if(XXWIDGETS_BUILD_SIGNATURE_DEBUGGER_DIE_ENGINE)
            target_link_libraries(xxwidgets_die_signature_debugger_demo PRIVATE xxwidgets::signature_debugger_die_engine)
            target_compile_definitions(xxwidgets_die_signature_debugger_demo PRIVATE XXWIDGETS_SIGNATURE_DEBUGGER_DIE_ENGINE)
        else()
            target_link_libraries(xxwidgets_die_signature_debugger_demo PRIVATE xxwidgets::signature_debugger)
        endif()
        list(REMOVE_ITEM _xxwidgets_demo_targets xxwidgets_die_signature_debugger_demo)
    endif()
    xxwidgets_add_demo(xxwidgets_debugger_demo examples/debugger_demo.c)
    # Interactive architecture/pane demonstration; no generic smoke entry.
    list(REMOVE_ITEM _xxwidgets_demo_targets xxwidgets_debugger_demo)
    if(XXWIDGETS_BUILD_DEBUGGER AND XXWIDGETS_BUILD_EMULATOR)
        xxwidgets_add_demo(xxwidgets_debugger_session_demo examples/debugger_session_demo.c)
        target_link_libraries(xxwidgets_debugger_session_demo PRIVATE xxwidgets::debugger xxwidgets::emulator)
        list(REMOVE_ITEM _xxwidgets_demo_targets xxwidgets_debugger_session_demo)
    endif()
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
    xxwidgets_add_demo(xxwidgets_scan_directory_demo examples/scan_directory_demo.c)
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
        add_executable(xxwidgets_debugger_arguments_tests
            tests/test_debugger_arguments.c src/debugger/xxwidgets_debugger_arguments.c)
        target_include_directories(xxwidgets_debugger_arguments_tests PRIVATE include)
        target_compile_features(xxwidgets_debugger_arguments_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_debugger_arguments_tests PRIVATE XXWIDGETS_STATIC)
        add_test(NAME xxwidgets_debugger_arguments COMMAND xxwidgets_debugger_arguments_tests)
        if(TARGET xxwidgets_debugger AND UNIX AND NOT CMAKE_CROSSCOMPILING)
            add_executable(xxwidgets_debugger_project_probe_tests tests/test_debugger_project_probe.c)
            target_compile_features(xxwidgets_debugger_project_probe_tests PRIVATE c_std_11)
            target_link_libraries(xxwidgets_debugger_project_probe_tests PRIVATE xxwidgets::debugger)
            add_test(NAME xxwidgets_debugger_project_probe
                COMMAND xxwidgets_debugger_project_probe_tests
                    "${XXWIDGETS_XXDEBUG_DIR}/tests/fixtures/bin/test.com"
                    "${XXWIDGETS_XXDEBUG_DIR}/tests/fixtures/bin/test_elf64")
            set_tests_properties(xxwidgets_debugger_project_probe PROPERTIES TIMEOUT 15)
        endif()
        if(_xxwidgets_native STREQUAL "gtk3" AND NOT CMAKE_CROSSCOMPILING)
            find_program(XXWIDGETS_XVFB_RUN xvfb-run)
            if(XXWIDGETS_XVFB_RUN)
                add_executable(xxwidgets_menu_gtk_tests tests/test_menu_gtk.c)
                target_link_libraries(xxwidgets_menu_gtk_tests PRIVATE xxwidgets::xxwidgets PkgConfig::XXWIDGETS_GTK3)
                target_compile_features(xxwidgets_menu_gtk_tests PRIVATE c_std_11)
                add_test(NAME xxwidgets_menu_gtk
                    COMMAND "${XXWIDGETS_XVFB_RUN}" -a -s "-screen 0 1600x1200x24" $<TARGET_FILE:xxwidgets_menu_gtk_tests>)
                set_tests_properties(xxwidgets_menu_gtk PROPERTIES TIMEOUT 30)
                find_package(Python3 QUIET COMPONENTS Interpreter)
                find_program(XXWIDGETS_XDOTOOL xdotool)
                if(Python3_Interpreter_FOUND AND XXWIDGETS_XDOTOOL)
                    add_executable(xxwidgets_debugger_dialog_tests tests/test_debugger_dialog.c)
                    target_link_libraries(xxwidgets_debugger_dialog_tests PRIVATE xxwidgets::xxwidgets)
                    target_compile_features(xxwidgets_debugger_dialog_tests PRIVATE c_std_11)
                    add_test(NAME xxwidgets_debugger_dialog
                        COMMAND "${XXWIDGETS_XVFB_RUN}" -a "${Python3_EXECUTABLE}"
                            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_debugger_dialog.py" $<TARGET_FILE:xxwidgets_debugger_dialog_tests>)
                    set_tests_properties(xxwidgets_debugger_dialog PROPERTIES TIMEOUT 45)
                endif()
            endif()
        endif()
        # Deterministic API/lifecycle tests don't require a display or terminal.
        add_executable(xxwidgets_core_tests
            $<TARGET_OBJECTS:xxwidgets_io>
            $<TARGET_OBJECTS:xxwidgets_history>
            $<TARGET_OBJECTS:xxwidgets_translation>
            $<TARGET_OBJECTS:xxwidgets_threads>
            $<TARGET_OBJECTS:xxwidgets_scan_colors>
    $<TARGET_OBJECTS:xxwidgets_codepages>
            tests/test_core.c
            tests/test_translation.c
            tests/test_hex_resize.c
            src/xxwidgets.c
            src/xxwidgets_modality.c
            src/xxwidgets_translation.c
            src/xxwidgets_edit.c
            src/xxwidgets_window.c
            src/xxwidgets_menu.c
            src/xxwidgets_listbox.c
            src/xxwidgets_archiveview.c
            src/xxwidgets_hex_options.c
            src/xxwidgets_hexview.c
            src/xxwidgets_hex_edit.c
            src/xxwidgets_hex_paste.c
            src/xxwidgets_hex_cut.c
            src/xxwidgets_hex_columns.c
            src/xxwidgets_archivebrowser.c
            src/xxwidgets_dialog.c
            src/xxwidgets_about.c
            src/xxwidgets_combobox.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_process.c tests/test_process.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_window_modality.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidget_systemprocessesviewer.c src/xxwidget_systemprocessesviewer_hexview.c ${_xxwidgets_process_memory_source} src/platform/xxwidget_systemprocessesviewer_platform.c ${_xxwidgets_process_dump_source} tests/test_systemprocessesviewer.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidget_binaryviewer.c src/xxwidget_resourcesviewer.c src/xxwidget_importsviewer.c src/xxwidget_exportsviewer.c src/xxwidget_symbolsviewer.c tests/test_binaryviewers.c)
        if(APPLE)
            target_compile_definitions(xxwidgets_core_tests PRIVATE _DARWIN_C_SOURCE)
        endif()
        if(WIN32)
            target_link_libraries(xxwidgets_core_tests PRIVATE shell32 ole32)
        endif()
        target_link_libraries(xxwidgets_core_tests PRIVATE ${XXIO_LIBRARIES} Threads::Threads)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_font_options.c
            src/xxwidgets_disasm_options.c src/xxwidgets_file_options.c tests/test_listing_options.c)
        set(_xxwidgets_listing_fixture "${CMAKE_CURRENT_BINARY_DIR}/listing-options-fixture")
        file(MAKE_DIRECTORY "${_xxwidgets_listing_fixture}/blocked.bin.bak")
        target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_LISTING_TEST_DIR="${_xxwidgets_listing_fixture}")
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_forms.c)
        target_include_directories(xxwidgets_core_tests PRIVATE include src "${XXWIDGETS_XXFCLIB_DIR}/include")
        target_compile_features(xxwidgets_core_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_STATIC XXWIDGETS_TEST_CLOCK XXFC_STATIC)
        if(UNIX)
            target_compile_definitions(xxwidgets_core_tests PRIVATE _POSIX_C_SOURCE=200809L)
        endif()
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_shortcuts.c
            src/die_engine/xxwidgets_die_signature_edit.c src/die_engine/xxwidgets_die_signature_editor.c)
        target_sources(xxwidgets_core_tests PRIVATE src/scan/xxwidgets_scanresults.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_data_struct_table.c src/xxwidgets_data_struct_view.c src/xxwidgets_table_filter.c src/xxwidgets_table.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_debugger_tables.c src/xxwidgets_debugger.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_debugger_session.c src/xxwidgets_debugger_session.c
            src/xxwidgets_debugger/xxwidgets_debugger_arch.c
            src/xxwidgets_debugger/xxwidgets_debugger_x86_16.c
            src/xxwidgets_debugger/xxwidgets_debugger_x86_32.c
            src/xxwidgets_debugger/xxwidgets_debugger_x86_64.c
            src/xxwidgets_debugger/xxwidgets_debugger_arm32.c
            src/xxwidgets_debugger/xxwidgets_debugger_arm64.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_treeview.c src/scan/xxwidgets_scan_panel.c tests/test_scan_panel.c
            src/scan/xxwidgets_scan_options.c src/scan/xxwidgets_scan_directory.c ${_xxwidgets_scan_directory_source}
            tests/test_scan_directory.c)
        set(_xxwidgets_scan_fixture "${CMAKE_CURRENT_BINARY_DIR}/scan-directory-fixture")
        file(MAKE_DIRECTORY "${_xxwidgets_scan_fixture}/sub/nested" "${_xxwidgets_scan_fixture}/empty" "${CMAKE_CURRENT_BINARY_DIR}/scan-directory-bulk")
        foreach(_scan_file root.bin .hidden fail.bin unicode-λ.bin sub/child.bin sub/nested/deep.bin)
            file(WRITE "${_xxwidgets_scan_fixture}/${_scan_file}" "directory scan fixture\n")
        endforeach()
        foreach(_scan_index RANGE 0 127)
            file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/scan-directory-bulk/file${_scan_index}.bin" "bulk fixture\n")
        endforeach()
        target_compile_definitions(xxwidgets_core_tests PRIVATE
            XXWIDGETS_SCAN_FIXTURE="${_xxwidgets_scan_fixture}"
            XXWIDGETS_SCAN_BULK="${CMAKE_CURRENT_BINARY_DIR}/scan-directory-bulk")
        target_sources(xxwidgets_core_tests PRIVATE tests/test_shortcuts.c tests/test_hex_columns.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_file_drops.c tests/test_canvas.c src/xxwidgets_canvas.c src/xxwidgets_entropy.c tests/test_entropy.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_strings.c src/xxwidgets_strings_dialog.c
            tests/test_strings.c tests/test_strings_source.c tests/test_table.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_extractor.c tests/test_extractor.c)
        target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_disk_write.c tests/test_disk_write.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_disk_sources.c)
        target_sources(xxwidgets_core_tests PRIVATE tests/test_signature_disk_write.c)
        target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_TEST_DISK_CLOCK)
        if(UNIX)
            target_link_libraries(xxwidgets_core_tests PRIVATE m)
        endif()
        if(XXWIDGETS_BUILD_SETTINGS)
            target_sources(xxwidgets_core_tests PRIVATE src/xxwidgets_settings.c)
            target_link_libraries(xxwidgets_core_tests PRIVATE xxwidgets_xxsettings)
            target_compile_definitions(xxwidgets_core_tests PRIVATE XXWIDGETS_WITH_SETTINGS)
        endif()
        if(MSVC)
            target_compile_options(xxwidgets_core_tests PRIVATE /utf-8)
        endif()
        target_compile_definitions(xxwidgets_core_tests PRIVATE
            XXWIDGETS_TEST_TRANSLATION_PO="${CMAKE_CURRENT_SOURCE_DIR}/po/de.po")
        add_test(NAME xxwidgets_scan_directory COMMAND xxwidgets_core_tests --scan-directory-only)
        add_test(NAME xxwidgets_extractor COMMAND xxwidgets_core_tests --extractor-only)
        add_test(NAME xxwidgets_disk_write COMMAND xxwidgets_core_tests --disk-write-only)
        add_test(NAME xxwidgets_disk_sources COMMAND xxwidgets_core_tests --disk-sources-only)
        set_tests_properties(xxwidgets_disk_sources PROPERTIES TIMEOUT 20 LABELS "disk;strings;extractor")
        set_tests_properties(xxwidgets_disk_write PROPERTIES TIMEOUT 20 LABELS "disk;worker;io")
        set_tests_properties(xxwidgets_extractor PROPERTIES TIMEOUT 15 LABELS "extractor;worker;io")
        add_test(NAME xxwidgets_window_modality COMMAND xxwidgets_core_tests --window-modality-only)
        set_tests_properties(xxwidgets_window_modality PROPERTIES TIMEOUT 15 LABELS "window;modality")
        add_test(NAME xxwidgets_core COMMAND xxwidgets_core_tests)
        set_tests_properties(xxwidgets_core xxwidgets_scan_directory PROPERTIES
            RESOURCE_LOCK xxwidgets_scan_directory_fixture)
        if(UNIX AND TARGET xxwidgets_emulator)
            add_executable(xxwidgets_debugger_emul_runtime_tests tests/test_debugger_emul_runtime.c)
            target_link_libraries(xxwidgets_debugger_emul_runtime_tests PRIVATE xxwidgets::emulator)
            if(XXWIDGETS_DEBUGGER_MACHO_FIXTURE)
                if(NOT EXISTS "${XXWIDGETS_DEBUGGER_MACHO_FIXTURE}")
                    message(FATAL_ERROR "XXWIDGETS_DEBUGGER_MACHO_FIXTURE does not exist")
                endif()
                find_package(Python3 COMPONENTS Interpreter REQUIRED)
                add_test(NAME xxwidgets_debugger_emul_runtime
                    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_debugger_runtime.py"
                    --transcript "${CMAKE_CURRENT_BINARY_DIR}/debugger-emul-runtime.ansi" --timeout 30 --
                    $<TARGET_FILE:xxwidgets_debugger_emul_runtime_tests> "${XXWIDGETS_DEBUGGER_MACHO_FIXTURE}"
                    "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/debugger-emul-runtime.log")
                set_tests_properties(xxwidgets_debugger_emul_runtime PROPERTIES TIMEOUT 35 LABELS "debugger;emulator;tui")
                if(TARGET xxwidgets_debugger_session_demo)
                    add_test(NAME xxwidgets_debugger_session_tui
                        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_debugger_session_tui.py"
                        $<TARGET_FILE:xxwidgets_debugger_session_demo> "${XXWIDGETS_DEBUGGER_MACHO_FIXTURE}"
                        --artifacts "${CMAKE_CURRENT_BINARY_DIR}/debugger-tui-frames")
                    set_tests_properties(xxwidgets_debugger_session_tui PROPERTIES TIMEOUT 35 LABELS "debugger;emulator;tui")
                endif()
            endif()
        endif()
        # Reuse the deterministic core's production sources with an independent
        # fake backend so search modal transactions run without a desktop.
        get_target_property(_xxwidgets_search_sources xxwidgets_core_tests SOURCES)
        list(FILTER _xxwidgets_search_sources EXCLUDE REGEX "^tests/")
        add_executable(xxwidgets_search_tests tests/test_search_dialog.c
            src/xxwidgets_search.c ${_xxwidgets_search_sources})
        if(WIN32)
            target_link_libraries(xxwidgets_search_tests PRIVATE shell32 ole32)
        endif()
        target_include_directories(xxwidgets_search_tests PRIVATE include src "${XXWIDGETS_XXFCLIB_DIR}/include")
        target_compile_features(xxwidgets_search_tests PRIVATE c_std_11)
        target_link_libraries(xxwidgets_search_tests PRIVATE ${XXIO_LIBRARIES} Threads::Threads)
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
        add_executable(xxwidgets_options_nested_tests tests/test_options_nested.c
            src/xxwidgets_options_pages.c src/xxwidgets_database_paths.c
            src/xxwidgets_context_options.c src/xxwidgets_optimization_options.c
            ${_xxwidgets_search_sources})
        target_include_directories(xxwidgets_options_nested_tests PRIVATE include src "${XXWIDGETS_XXFCLIB_DIR}/include")
        target_compile_features(xxwidgets_options_nested_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_options_nested_tests PRIVATE XXWIDGETS_STATIC XXWIDGETS_TEST_CLOCK XXFC_STATIC)
        target_link_libraries(xxwidgets_options_nested_tests PRIVATE ${XXIO_LIBRARIES} Threads::Threads)
        if(WIN32)
            target_link_libraries(xxwidgets_options_nested_tests PRIVATE shell32 ole32 advapi32 user32)
        endif()
        if(UNIX)
            target_compile_definitions(xxwidgets_options_nested_tests PRIVATE _POSIX_C_SOURCE=200809L)
            target_link_libraries(xxwidgets_options_nested_tests PRIVATE m)
        endif()
        if(XXWIDGETS_BUILD_SETTINGS)
            target_link_libraries(xxwidgets_options_nested_tests PRIVATE xxwidgets_xxsettings)
            target_compile_definitions(xxwidgets_options_nested_tests PRIVATE XXWIDGETS_WITH_SETTINGS)
        endif()
        add_test(NAME xxwidgets_options_nested COMMAND xxwidgets_options_nested_tests)
        set_tests_properties(xxwidgets_options_nested PROPERTIES TIMEOUT 15 LABELS "smoke;options;modality")
        set(_xxwidgets_memory_map_export_sources ${_xxwidgets_search_sources}
            src/xxwidgets_memory_map.c src/xxwidgets_memory_map_export.c)
        list(REMOVE_DUPLICATES _xxwidgets_memory_map_export_sources)
        add_executable(xxwidgets_memory_map_exports_tests tests/test_memory_map_exports.c
            ${_xxwidgets_memory_map_export_sources})
        target_include_directories(xxwidgets_memory_map_exports_tests PRIVATE include src "${XXWIDGETS_XXFCLIB_DIR}/include")
        target_compile_features(xxwidgets_memory_map_exports_tests PRIVATE c_std_11)
        target_compile_definitions(xxwidgets_memory_map_exports_tests PRIVATE XXWIDGETS_STATIC XXWIDGETS_TEST_CLOCK XXFC_STATIC)
        target_link_libraries(xxwidgets_memory_map_exports_tests PRIVATE ${XXIO_LIBRARIES} Threads::Threads)
        if(WIN32)
            target_link_libraries(xxwidgets_memory_map_exports_tests PRIVATE shell32 ole32 user32)
        endif()
        if(UNIX)
            target_compile_definitions(xxwidgets_memory_map_exports_tests PRIVATE _POSIX_C_SOURCE=200809L)
            target_link_libraries(xxwidgets_memory_map_exports_tests PRIVATE m)
        endif()
        if(XXWIDGETS_BUILD_SETTINGS)
            target_link_libraries(xxwidgets_memory_map_exports_tests PRIVATE xxwidgets_xxsettings)
            target_compile_definitions(xxwidgets_memory_map_exports_tests PRIVATE XXWIDGETS_WITH_SETTINGS)
        endif()
        add_test(NAME xxwidgets_memory_map_exports COMMAND xxwidgets_memory_map_exports_tests "${CMAKE_CURRENT_BINARY_DIR}")
        set_tests_properties(xxwidgets_memory_map_exports PROPERTIES TIMEOUT 15 LABELS "memory-map;io")
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
            add_executable(xxwidgets_cocoa_strings_tests tests/test_strings_cocoa.m)
            target_compile_options(xxwidgets_cocoa_strings_tests PRIVATE -fno-objc-arc)
            target_link_libraries(xxwidgets_cocoa_strings_tests PRIVATE xxwidgets::xxwidgets "-framework AppKit")
            add_test(NAME xxwidgets_cocoa_strings COMMAND xxwidgets_cocoa_strings_tests)
            set_tests_properties(xxwidgets_cocoa_strings PROPERTIES TIMEOUT 30)
            set_tests_properties(xxwidgets_cocoa_widgets xxwidgets_cocoa_hex xxwidgets_cocoa_drop xxwidgets_cocoa_strings
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
            add_test(NAME xxwidgets_win32_tree_colors COMMAND xxwidgets_win32_tests --tree-colors)
            set_tests_properties(xxwidgets_win32_tree_colors PROPERTIES TIMEOUT 15)
            add_test(NAME xxwidgets_memory_map_controls COMMAND xxwidgets_win32_tests --memory-map-controls)
            add_executable(xxwidgets_memory_map_browser_tests tests/test_memory_map_browser.c)
            target_include_directories(xxwidgets_memory_map_browser_tests PRIVATE "${XXWIDGETS_XXFCLIB_DIR}/include")
            target_link_libraries(xxwidgets_memory_map_browser_tests PRIVATE xxwidgets::xxwidgets)
            add_test(NAME xxwidgets_memory_map_browser COMMAND xxwidgets_memory_map_browser_tests)
            set_tests_properties(xxwidgets_memory_map_controls xxwidgets_memory_map_browser PROPERTIES TIMEOUT 15)
        endif()
        if(_xxwidgets_native STREQUAL "win32")
            foreach(_xxwidgets_options IN ITEMS scan_options font_options context_options optimization_options options_pages)
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
            add_test(NAME xxwidgets_hex_resize_tui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hex_resize_tui.py" $<TARGET_FILE:xxwidgets_hexview_demo>)
            set_tests_properties(xxwidgets_hex_resize_tui PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE LABELS "smoke;hex;io")
            add_test(NAME xxwidgets_hex_paste_tui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hex_paste_tui.py" $<TARGET_FILE:xxwidgets_hexview_demo>)
            set_tests_properties(xxwidgets_hex_paste_tui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "smoke;hex;clipboard")
            add_test(NAME xxwidgets_hex_cut_tui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hex_cut_tui.py" $<TARGET_FILE:xxwidgets_hexview_demo>)
            set_tests_properties(xxwidgets_hex_cut_tui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "smoke;hex;clipboard;io")
            add_test(NAME xxwidgets_hex_paste_insert_tui COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hex_paste_insert_tui.py" $<TARGET_FILE:xxwidgets_hexview_demo>)
            set_tests_properties(xxwidgets_hex_paste_insert_tui PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE LABELS "smoke;hex;clipboard;io")
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
    xxfclib/formats/xx_format_unpack_handle_rows.inc
    xxfclib/formats/xx_uniextract_enums.inc
    xxfclib/formats/xx_format_gap_enums.inc
    xxfclib/formats/xx_format_gap_aliases.inc
    xxfclib/formats/audio/die_music/xx_die_music_enums.inc
    xxfclib/formats/xx_memory_map.h
    xxfclib/data/xx_pd.h xxfclib/io/xx_io.h xxfclib/list/xx_list.h
    xxfclib/history/xx_history.h
    xxfclib/strings/xx_string.h xxfclib/var/xx_var.h xxfclib/xxfc_defs.h)
    get_filename_component(_xxwidgets_type_dir "${_xxwidgets_type_header}" DIRECTORY)
    install(FILES "${XXWIDGETS_XXFCLIB_DIR}/include/${_xxwidgets_type_header}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/${_xxwidgets_type_dir}")
endforeach()
install(FILES LICENSE README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
install(FILES examples/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/examples)
install(FILES docs/process-dialog-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/search-dialog-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/strings-viewer-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/extractor-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/disk-write-dialog.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/forms-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/die-signature-editor-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/die-signature-debugger-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
install(FILES docs/debugger-widget-reference.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
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

# Optional maintenance tool; catalog loading itself needs no gettext installation.
find_program(XXWIDGETS_XGETTEXT_EXECUTABLE xgettext)
if(XXWIDGETS_XGETTEXT_EXECUTABLE)
    file(GLOB_RECURSE _xxwidgets_translation_inputs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.m")
    add_custom_target(xxwidgets_update_translations
        COMMAND "${XXWIDGETS_XGETTEXT_EXECUTABLE}" --language=C --from-code=UTF-8
            --keyword=XXW_TR:2 --keyword=XXW_N_ --keyword=xxwidgets_app_gettext:2 --keyword=xxwidgets_app_ngettext:2,3
            --sort-output --add-comments=TRANSLATORS --package-name=xxwidgets
            --package-version=${PROJECT_VERSION} --msgid-bugs-address=horsicq@gmail.com
            --output=po/xxwidgets.pot ${_xxwidgets_translation_inputs} examples/translation_demo.c examples/scan_directory_demo.c
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Extracting xxwidgets PO message template")
endif()
install(DIRECTORY po/ DESTINATION ${CMAKE_INSTALL_DATADIR}/xxwidgets/po
    FILES_MATCHING PATTERN "*.po" PATTERN "*.pot")
install(FILES docs/translation.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)

install(FILES docs/scan-directory.md DESTINATION ${CMAKE_INSTALL_DOCDIR}/docs)
