cmake_minimum_required(VERSION 3.24)

project(QtBugPropertyVarMapGC VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(QtBugPropertyVarMapGC
    main.cpp
)

qt_add_qml_module(QtBugPropertyVarMapGC
    URI QtBugApp
    RESOURCE_PREFIX /
    VERSION 1.0
    SOURCES
        InputPoint.h
    QML_FILES
        Main.qml
)

target_link_libraries(QtBugPropertyVarMapGC
    PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)
