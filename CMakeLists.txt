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
)

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

set_source_files_properties(src/ui/qml/Synchro/Theme/Theme.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

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

set_target_properties(synchro PROPERTIES
    WIN32_EXECUTABLE FALSE
    MACOSX_BUNDLE FALSE
)

include(GNUInstallDirs)
install(TARGETS synchro RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/org.omarchy.synchro.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
