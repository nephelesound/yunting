cmake_minimum_required(VERSION 3.21)
project(qmlshot LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickControls2)
find_package(KF6ColorScheme REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6I18n REQUIRED)   # 与应用一样给 QML 装上 i18n()：kirigami-addons 的对话框按钮用它
qt_standard_project_setup()

# 应用的配色色调（src/colortone.cpp）同样作用在截图上
qt_add_executable(qmlshot main.cpp ../../src/colortone.h ../../src/colortone.cpp)
target_include_directories(qmlshot PRIVATE ../../src)
target_link_libraries(qmlshot PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::ColorScheme KF6::ConfigCore KF6::I18nQml)
