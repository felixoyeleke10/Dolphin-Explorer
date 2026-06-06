cmake_minimum_required(VERSION 3.20)
project(DolphinExplorer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# CMake 4.x generates moc_predefs.h by running the compiler with -dM -E on an
# internal test file.  This step fails silently with MinGW 13 + CMake 4.x.
# Disabling it is safe: MOC still parses Q_OBJECT headers correctly; it just
# won't receive a list of compiler predefined macros (not needed here).
set(CMAKE_AUTOMOC_COMPILER_PREDEFINES OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets Concurrent Svg Network)
qt_standard_project_setup()

add_subdirectory(src/util)
add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/geo)
add_subdirectory(src/pipeline)
add_subdirectory(src/render)
add_subdirectory(src/app)
add_subdirectory(src/ui)

enable_testing()
add_subdirectory(tests)

qt_add_executable(DolphinExplorer
    main.cpp
    resources/resources.qrc
)

target_link_libraries(DolphinExplorer PRIVATE
    dolphin-ui
    Qt6::Widgets
)
if(WIN32)
    target_link_libraries(DolphinExplorer PRIVATE dbghelp)
endif()

set_target_properties(DolphinExplorer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
