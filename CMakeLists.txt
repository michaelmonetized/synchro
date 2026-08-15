cmake_minimum_required(VERSION 3.21)
project(synchro VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick Svg Concurrent DBus)

qt_standard_project_setup(REQUIRES 6.8)

set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

qt_add_executable(synchro
    src/main.cpp
    src/core/DirectoryModel.cpp
    src/core/DirectoryLister.cpp
    src/core/DirectoryWatcher.cpp
    src/core/NavStack.cpp
    src/core/FilterProxy.cpp
    src/core/KeyMachine.cpp
    src/core/PeekHost.cpp
    src/core/MimeMap.cpp
    src/core/RecentStore.cpp
    src/core/ThumbnailService.cpp
    src/handlers/HandlerExec.cpp
    src/handlers/XdgOpen.cpp
    src/handlers/Manifest.cpp
    src/handlers/HandlerRegistry.cpp
    src/handlers/HandlerLoader.cpp
    src/handlers/HostApi.cpp
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
    OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml/Synchro/Theme"
)

qt_add_qml_module(synchro_handler
    STATIC
    URI Synchro.Handler
    VERSION 1.0
    QML_FILES
        src/ui/qml/Synchro/Handler/HandlerSurface.qml
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
        src/core/ThumbnailService.cpp
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
        src/core/PeekHost.cpp
        src/core/ThumbnailService.cpp
    )
    target_include_directories(command_field_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_compile_definitions(command_field_test PRIVATE
        SYNCHRO_MAIN_QML="${CMAKE_SOURCE_DIR}/src/ui/qml/Main.qml"
    )
    target_link_libraries(command_field_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        synchro_theme
        synchro_themeplugin
    )
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
        src/core/PeekHost.cpp
        src/core/NavStack.cpp
        src/core/MimeMap.cpp
        src/core/RecentStore.cpp
        src/core/ThumbnailService.cpp
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
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(handler_exec_test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME handler_exec_test COMMAND handler_exec_test)
    set_tests_properties(handler_exec_test PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

    qt_add_executable(thumbnail_service_test
        tests/thumbnail_service_test.cpp
        src/core/ThumbnailService.cpp
    )
    target_include_directories(thumbnail_service_test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(thumbnail_service_test PRIVATE
        Qt6::Test
        Qt6::Gui
        Qt6::Core
    )
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
endif()

set_target_properties(synchro PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

include(GNUInstallDirs)
install(TARGETS synchro RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/org.omarchy.synchro.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
