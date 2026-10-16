cmake_minimum_required(VERSION 3.21)
project(qmlshot LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)
qt_standard_project_setup()

qt_add_executable(qmlshot main.cpp)
target_link_libraries(qmlshot PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
