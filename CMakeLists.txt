cmake_minimum_required(VERSION 3.21)
project(synchro VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Svg Concurrent DBus)
find_package(KF6SyntaxHighlighting QUIET)
find_package(ZLIB QUIET)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(LIBWEBP libwebp)
    pkg_check_modules(SQLITE3 sqlite3)
endif()

# Link libwebp without its -I/usr/include/webp so <webp/decode.h> still resolves.
function(synchro_use_webp target)
    if(LIBWEBP_FOUND)
        target_link_libraries(${target} PRIVATE ${LIBWEBP_LIBRARIES})
        target_compile_definitions(${target} PRIVATE SYNCHRO_HAVE_WEBP=1)
    endif()
endfunction()

function(synchro_use_sqlite target)
    if(SQLITE3_FOUND)
        target_link_libraries(${target} PRIVATE ${SQLITE3_LIBRARIES})
        target_include_directories(${target} PRIVATE ${SQLITE3_INCLUDE_DIRS})
    else()
        target_link_libraries(${target} PRIVATE sqlite3)
    endif()
endfunction()

function(synchro_use_zlib target)
    if(ZLIB_FOUND)
        target_link_libraries(${target} PRIVATE ZLIB::ZLIB)
        target_compile_definitions(${target} PRIVATE SYNCHRO_HAVE_ZLIB=1)
    endif()
endfunction()

function(synchro_use_syntax target)
    if(TARGET KF6::SyntaxHighlighting)
        target_link_libraries(${target} PRIVATE KF6::SyntaxHighlighting)
        target_compile_definitions(${target} PRIVATE SYNCHRO_HAVE_KSYNTAX=1)
    endif()
endfunction()

qt_standard_project_setup(REQUIRES 6.8)

set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

set(SYNCHRO_FILEOP_SOURCES
    src/core/SelectionModel.cpp
    src/core/UndoStack.cpp
    src/core/FileOpEngine.cpp
)

set(SYNCHRO_THUMB_SOURCES
    src/core/ThumbnailService.cpp
    src/core/ThumbCache.cpp
    src/core/ParquetMeta.cpp
    src/core/DbPreview.cpp
    src/core/ArchiveMeta.cpp
    src/core/ThumbTheme.cpp
)

set(SYNCHRO_SYNTAX_SOURCES
    src/core/SyntaxHighlight.cpp
)

qt_add_executable(synchro
    src/main.cpp
    src/core/DirectoryModel.cpp
    src/core/DirectoryLister.cpp
    src/core/DirectoryWatcher.cpp
    src/core/NavStack.cpp
    src/core/FilterProxy.cpp
    src/core/KeyMachine.cpp
    src/core/CommandPalette.cpp
    src/core/PeekHost.cpp
    src/core/MimeMap.cpp
    src/core/RecentStore.cpp
    src/core/TrashStore.cpp
    src/core/Config.cpp
    src/core/LocationChips.cpp
    ${SYNCHRO_THUMB_SOURCES}
    src/core/ThumbImageProvider.cpp
    src/core/SearchService.cpp
    src/core/SearchModel.cpp
    src/core/SelectionModel.cpp
    src/core/UndoStack.cpp
    src/core/FileOpEngine.cpp
    src/core/PortalService.cpp
    src/handlers/HandlerExec.cpp
    src/handlers/XdgOpen.cpp
    src/handlers/Manifest.cpp
    src/handlers/HandlerRegistry.cpp
    src/handlers/HandlerLoader.cpp
    src/handlers/HostApi.cpp
    ${SYNCHRO_SYNTAX_SOURCES}
    src/handlers/HandlerInstall.cpp
    src/handlers/HandlerActions.cpp
    src/handlers/CoreVerbs.cpp
    src/cli.cpp
)

target_include_directories(synchro PRIVATE
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/handlers
)

# Debug / uninstalled builds resolve first-party handlers from the source tree.
target_compile_definitions(synchro PRIVATE
    SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    SYNCHRO_VERSION="${PROJECT_VERSION}"
)

qt_add_qml_module(synchro
    URI Synchro
    VERSION 1.0
    QML_FILES
        src/ui/qml/Main.qml
        src/ui/qml/FileList.qml
        src/ui/qml/FileGrid.qml
        src/ui/qml/PathBar.qml
        src/ui/qml/CommandField.qml
        src/ui/qml/PeekOverlay.qml
        src/ui/qml/DoOverlay.qml
        src/ui/qml/StatusLine.qml
        src/ui/qml/Confirm.qml
        src/ui/qml/ChooserWindow.qml
        src/ui/qml/ScrollChrome.qml
)

set_source_files_properties(src/ui/qml/Synchro/Theme/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(synchro_theme
    STATIC
    URI Synchro.Theme
    VERSION 1.0
    SOURCES
        src/theme/ThemeBridge.h
        src/theme/ThemeBridge.cpp
    QML_FILES
        src/ui/qml/Synchro/Theme/Theme.qml
        src/ui/qml/Synchro/Theme/FolderMark.qml
        src/ui/qml/Synchro/Theme/FileMark.qml
        src/ui/qml/Synchro/Theme/EmptyListing.qml
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Synchro/Theme"
)

qt_add_qml_module(synchro_handler
    STATIC
    URI Synchro.Handler
    VERSION 1.0
    QML_FILES
        src/ui/qml/Synchro/Handler/HandlerSurface.qml
        src/ui/qml/Synchro/Handler/DatabasePeek.qml
        src/ui/qml/Synchro/Handler/ArchivePeek.qml
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Synchro/Handler"
)

target_link_libraries(synchro PRIVATE
    Qt6::Quick
    Qt6::Svg
    Qt6::Concurrent
    Qt6::DBus
    synchro_theme
    synchro_themeplugin
    synchro_handler
    synchro_handlerplugin
)
synchro_use_webp(synchro)
synchro_use_sqlite(synchro)
synchro_use_zlib(synchro)
synchro_use_syntax(synchro)

target_link_libraries(synchro_theme PRIVATE Qt6::Quick)
target_include_directories(synchro_theme PUBLIC
    ${CMAKE_SOURCE_DIR}/src/theme
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(synchro PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(synchro_theme PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(directory_model_test
        tests/directory_model_test.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
    )
    target_include_directories(directory_model_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(directory_model_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
    )
    synchro_use_webp(directory_model_test)
    synchro_use_sqlite(directory_model_test)
    synchro_use_zlib(directory_model_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(directory_model_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME directory_model_test COMMAND directory_model_test)
    set_tests_properties(directory_model_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(command_field_test
        tests/command_field_test.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        ${SYNCHRO_FILEOP_SOURCES}
        src/core/CommandPalette.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/PeekHost.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
    )
    target_include_directories(command_field_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(command_field_test PRIVATE
        SYNCHRO_MAIN_QML="${CMAKE_SOURCE_DIR}/src/ui/qml/Main.qml"
    )
    target_link_libraries(command_field_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Concurrent
        synchro_theme
        synchro_themeplugin
    )
    synchro_use_webp(command_field_test)
    synchro_use_sqlite(command_field_test)
    synchro_use_zlib(command_field_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(command_field_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME command_field_test COMMAND command_field_test)
    set_tests_properties(command_field_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(handler_exec_test
        tests/handler_exec_test.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        ${SYNCHRO_FILEOP_SOURCES}
        src/core/CommandPalette.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/PeekHost.cpp
        src/core/NavStack.cpp
        src/core/MimeMap.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
        src/handlers/HandlerExec.cpp
        src/handlers/XdgOpen.cpp
    )
    target_include_directories(handler_exec_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(handler_exec_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(handler_exec_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Core
        Qt6::Concurrent
    )
    synchro_use_webp(handler_exec_test)
    synchro_use_sqlite(handler_exec_test)
    synchro_use_zlib(handler_exec_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(handler_exec_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME handler_exec_test COMMAND handler_exec_test)
    set_tests_properties(handler_exec_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(thumbnail_service_test
        tests/thumbnail_service_test.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/MimeMap.cpp
    )
    target_include_directories(thumbnail_service_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(thumbnail_service_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Core
    )
    synchro_use_webp(thumbnail_service_test)
    synchro_use_sqlite(thumbnail_service_test)
    synchro_use_zlib(thumbnail_service_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(thumbnail_service_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME thumbnail_service_test COMMAND thumbnail_service_test)
    set_tests_properties(thumbnail_service_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(manifest_validate_test
        tests/manifest_validate_test.cpp
        src/handlers/Manifest.cpp
    )
    target_include_directories(manifest_validate_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(manifest_validate_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(manifest_validate_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(manifest_validate_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME manifest_validate_test COMMAND manifest_validate_test)

    qt_add_executable(match_test
        tests/match_test.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
    )
    target_include_directories(match_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(match_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(match_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(match_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME match_test COMMAND match_test)

    qt_add_executable(peek_overlay_test
        tests/peek_overlay_test.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        ${SYNCHRO_FILEOP_SOURCES}
        src/core/CommandPalette.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/PeekHost.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/ThumbImageProvider.cpp
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/core/MimeMap.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
        src/handlers/HandlerLoader.cpp
        src/handlers/HostApi.cpp
        ${SYNCHRO_SYNTAX_SOURCES}
        src/handlers/HandlerExec.cpp
        src/handlers/XdgOpen.cpp
        src/handlers/HandlerActions.cpp
        src/handlers/CoreVerbs.cpp
    )
    target_include_directories(peek_overlay_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(peek_overlay_test PRIVATE
        SYNCHRO_MAIN_QML="${CMAKE_SOURCE_DIR}/src/ui/qml/Main.qml"
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(peek_overlay_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::Concurrent
        synchro_theme
        synchro_themeplugin
        synchro_handler
        synchro_handlerplugin
    )
    synchro_use_webp(peek_overlay_test)
    synchro_use_sqlite(peek_overlay_test)
    synchro_use_zlib(peek_overlay_test)
    synchro_use_syntax(peek_overlay_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(peek_overlay_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME peek_overlay_test COMMAND peek_overlay_test)
    set_tests_properties(peek_overlay_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(handler_cli_test
        tests/handler_cli_test.cpp
        src/handlers/HandlerInstall.cpp
        src/handlers/HandlerRegistry.cpp
        src/handlers/Manifest.cpp
    )
    target_include_directories(handler_cli_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(handler_cli_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(handler_cli_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(handler_cli_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME handler_cli_test COMMAND handler_cli_test)

    qt_add_executable(handler_actions_test
        tests/handler_actions_test.cpp
        src/handlers/HandlerActions.cpp
        src/handlers/CoreVerbs.cpp
        src/handlers/HandlerExec.cpp
        src/handlers/HandlerRegistry.cpp
        src/handlers/Manifest.cpp
    )
    target_include_directories(handler_actions_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(handler_actions_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(handler_actions_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(handler_actions_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME handler_actions_test COMMAND handler_actions_test)

    qt_add_executable(search_service_test
        tests/search_service_test.cpp
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        ${SYNCHRO_FILEOP_SOURCES}
        src/core/CommandPalette.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/PeekHost.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
    )
    target_include_directories(search_service_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_link_libraries(search_service_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Core
        Qt6::Concurrent
    )
    synchro_use_webp(search_service_test)
    synchro_use_sqlite(search_service_test)
    synchro_use_zlib(search_service_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(search_service_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME search_service_test COMMAND search_service_test)
    set_tests_properties(search_service_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(location_adapters_test
        tests/location_adapters_test.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        ${SYNCHRO_FILEOP_SOURCES}
        src/core/CommandPalette.cpp
        src/core/PeekHost.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
    )
    target_include_directories(location_adapters_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(location_adapters_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
    )
    target_link_libraries(location_adapters_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Core
        Qt6::Concurrent
    )
    synchro_use_webp(location_adapters_test)
    synchro_use_sqlite(location_adapters_test)
    synchro_use_zlib(location_adapters_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(location_adapters_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME location_adapters_test COMMAND location_adapters_test)
    set_tests_properties(location_adapters_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(file_ops_test
        tests/file_ops_test.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        src/core/CommandPalette.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/PeekHost.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
        ${SYNCHRO_FILEOP_SOURCES}
    )
    target_include_directories(file_ops_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_link_libraries(file_ops_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Core
        Qt6::Concurrent
    )
    synchro_use_webp(file_ops_test)
    synchro_use_sqlite(file_ops_test)
    synchro_use_zlib(file_ops_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(file_ops_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME file_ops_test COMMAND file_ops_test)
    set_tests_properties(file_ops_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(portal_dbus_test
        tests/portal_dbus_test.cpp
        src/core/DirectoryModel.cpp
        src/core/DirectoryLister.cpp
        src/core/DirectoryWatcher.cpp
        src/core/NavStack.cpp
        src/core/FilterProxy.cpp
        src/core/KeyMachine.cpp
        src/core/CommandPalette.cpp
        src/core/PeekHost.cpp
        src/core/RecentStore.cpp
        src/core/TrashStore.cpp
        ${SYNCHRO_THUMB_SOURCES}
        src/core/ThumbImageProvider.cpp
        src/core/SearchService.cpp
        src/core/SearchModel.cpp
        src/core/MimeMap.cpp
        src/core/Config.cpp
        src/core/LocationChips.cpp
        src/core/PortalService.cpp
        src/handlers/Manifest.cpp
        src/handlers/HandlerRegistry.cpp
        src/handlers/HandlerLoader.cpp
        src/handlers/HostApi.cpp
        ${SYNCHRO_SYNTAX_SOURCES}
        src/handlers/HandlerExec.cpp
        src/handlers/XdgOpen.cpp
        src/handlers/HandlerActions.cpp
        src/handlers/CoreVerbs.cpp
        ${SYNCHRO_FILEOP_SOURCES}
    )
    target_include_directories(portal_dbus_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/handlers
    )
    target_compile_definitions(portal_dbus_test PRIVATE
        SYNCHRO_FIRST_PARTY_HANDLER_DIR="${CMAKE_SOURCE_DIR}/handlers"
        SYNCHRO_CHOOSER_QML="${CMAKE_SOURCE_DIR}/src/ui/qml/ChooserWindow.qml"
    )
    target_link_libraries(portal_dbus_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::DBus
        Qt6::Concurrent
        synchro_theme
        synchro_themeplugin
        synchro_handler
        synchro_handlerplugin
    )
    synchro_use_webp(portal_dbus_test)
    synchro_use_sqlite(portal_dbus_test)
    synchro_use_zlib(portal_dbus_test)
    synchro_use_syntax(portal_dbus_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(portal_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME portal_dbus_test COMMAND portal_dbus_test)
    set_tests_properties(portal_dbus_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(syntax_highlight_test
        tests/syntax_highlight_test.cpp
        src/core/SyntaxHighlight.cpp
    )
    target_include_directories(syntax_highlight_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(syntax_highlight_test PRIVATE
        Qt6::Test
        Qt6::Gui
    )
    synchro_use_syntax(syntax_highlight_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(syntax_highlight_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME syntax_highlight_test COMMAND syntax_highlight_test)
    set_tests_properties(syntax_highlight_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    qt_add_executable(parquet_meta_test
        tests/parquet_meta_test.cpp
        src/core/ParquetMeta.cpp
    )
    target_include_directories(parquet_meta_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(parquet_meta_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(parquet_meta_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME parquet_meta_test COMMAND parquet_meta_test)

    qt_add_executable(db_preview_test
        tests/db_preview_test.cpp
        src/core/DbPreview.cpp
    )
    target_include_directories(db_preview_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(db_preview_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    synchro_use_sqlite(db_preview_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(db_preview_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME db_preview_test COMMAND db_preview_test)

    qt_add_executable(archive_meta_test
        tests/archive_meta_test.cpp
        src/core/ArchiveMeta.cpp
    )
    target_include_directories(archive_meta_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(archive_meta_test PRIVATE
        Qt6::Test
        Qt6::Core
    )
    synchro_use_zlib(archive_meta_test)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(archive_meta_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME archive_meta_test COMMAND archive_meta_test)
endif()

set_target_properties(synchro PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

include(GNUInstallDirs)

# Exec is baked at configure time; use -DCMAKE_INSTALL_PREFIX=... (not only
# cmake --install --prefix) so the D-Bus service points at the installed binary.
configure_file(
    packaging/dbus/org.freedesktop.impl.portal.desktop.synchro.service.in
    ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.synchro.service
    @ONLY)

install(TARGETS synchro RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/org.omarchy.synchro.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(DIRECTORY handlers/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/synchro/handlers)
install(FILES packaging/portals/synchro.portal
        DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal/portals)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/org.freedesktop.impl.portal.desktop.synchro.service
        DESTINATION ${CMAKE_INSTALL_DATADIR}/dbus-1/services)
install(FILES packaging/icons/org.omarchy.synchro.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# Launchers go to bindir so `{ omarchy = "synchro" }` works after a prefix
# install. Hypr overlays stay in-tree — never /usr/share/omarchy/ (K15).
install(PROGRAMS
        packaging/omarchy/omarchy-launch-synchro
        packaging/omarchy/omarchy-launch-synchro-cwd
        DESTINATION ${CMAKE_INSTALL_BINDIR})
