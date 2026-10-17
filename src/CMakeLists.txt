cmake_minimum_required(VERSION 3.21)
project(ncm-player VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Network Multimedia DBus)

qt_add_executable(ncm-player
    main.cpp
    ncmclient.h ncmclient.cpp
    store.h store.cpp store_on_demand.cpp
    json_util.h
    normalize.h normalize.cpp
    audioengine.h audioengine.cpp
    session.h session.cpp
    mpris.h mpris.cpp
)

# 原型阶段：QML 直接从源码树加载（界面与后端并行演进）；装运时再改资源化
target_compile_definitions(ncm-player PRIVATE
    PROTOTYPE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../prototype"
)

target_compile_options(ncm-player PRIVATE -Wall -Wextra)

target_link_libraries(ncm-player PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Multimedia Qt6::DBus
)
