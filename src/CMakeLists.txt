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
    querycache.h querycache.cpp
    imagecache.h
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

# 测试：只测有策略的深模块（QueryCache 的缓存与去重规则）
find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()
qt_add_executable(querycache_test tests/querycache_test.cpp querycache.h querycache.cpp)
target_link_libraries(querycache_test PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME querycache COMMAND querycache_test)

# 重试策略：seam 是本地 QTcpServer（I/O 边界），不 mock NcmClient 内部
qt_add_executable(retry_test tests/retry_test.cpp ncmclient.h ncmclient.cpp)
target_compile_options(retry_test PRIVATE -Wall -Wextra)
target_link_libraries(retry_test PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME retry COMMAND retry_test)

target_link_libraries(ncm-player PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Multimedia Qt6::DBus
)
