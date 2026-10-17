cmake_minimum_required(VERSION 3.21)
project(yunting VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Network Multimedia DBus)
find_package(Qt6Keychain REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

qt_add_executable(yunting
    main.cpp
    ncmclient.h ncmclient.cpp
    cookievault.h cookievault.cpp
    store.h store.cpp store_on_demand.cpp
    querycache.h querycache.cpp
    imagecache.h
    json_util.h
    normalize.h normalize.cpp
    audioengine.h audioengine.cpp
    session.h session.cpp
    mpris.h mpris.cpp
    tray.h tray.cpp
    crypto.h crypto.cpp
    apicli.h apicli.cpp
)

# 界面 QML/JS 编进可执行文件：整个 prototype/ 目录挂到 qrc:/qt/qml/org/sudoskys/yunting/ 下，
# qmldir 与组件同目录，singleton 声明照常解析。Snapshot.qml 与它读的 Data.js / Personal.js /
# Fixtures.js 是原型与截图专用，不编入；其中 Personal.js 是账号数据，绝不能进二进制。
file(GLOB _app_qml RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../prototype"
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/qmldir")
list(FILTER _app_qml EXCLUDE REGEX "^(Snapshot\\.qml|Data\\.js|Personal\\.js|Fixtures\\.js)$")
# qt_add_resources 的 FILES 接受绝对路径；RELATIVE 摘出的裸文件名会被qt_add_executable 误当源码解析
list(TRANSFORM _app_qml REPLACE "^" "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/")

qt_add_resources(yunting "app_qml"
    PREFIX "/qt/qml/org/sudoskys/yunting"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../prototype"
    FILES ${_app_qml})

target_compile_options(yunting PRIVATE -Wall -Wextra)

# 测试：只测有策略的深模块（QueryCache 的缓存与去重规则、AudioEngine 的音量刻度与钳制、
# NcmClient 的重试、CookieVault 的后备文件规则）
find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()
qt_add_executable(querycache_test tests/querycache_test.cpp querycache.h querycache.cpp)
target_link_libraries(querycache_test PRIVATE Qt6::Core Qt6::Test Qt6::Network)
add_test(NAME querycache COMMAND querycache_test)

# 归一化形状：曲目导航字段、MV、云盘后备、LRC 译文
qt_add_executable(form_test tests/form_test.cpp json_util.h)
target_link_libraries(form_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME form COMMAND form_test)

qt_add_executable(normalize_test tests/normalize_test.cpp normalize.h normalize.cpp json_util.h)
target_link_libraries(normalize_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME normalize COMMAND normalize_test)

# 音量/静音走 QAudioOutput，测试要能构造真实引擎（无窗口、无网络）
qt_add_executable(audioengine_test tests/audioengine_test.cpp audioengine.h audioengine.cpp
    ncmclient.h ncmclient.cpp cookievault.h cookievault.cpp json_util.h crypto.h crypto.cpp)
target_compile_options(audioengine_test PRIVATE -Wall -Wextra)
target_link_libraries(audioengine_test PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Multimedia Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME audioengine COMMAND audioengine_test)

# 重试策略：seam 是本地 QTcpServer（I/O 边界），不 mock NcmClient 内部
qt_add_executable(retry_test tests/retry_test.cpp ncmclient.h ncmclient.cpp cookievault.h cookievault.cpp crypto.h crypto.cpp)
target_compile_options(retry_test PRIVATE -Wall -Wextra)
target_link_libraries(retry_test PRIVATE Qt6::Core Qt6::Network Qt6::Test Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME retry COMMAND retry_test)

# CookieVault 只测后备文件路径（钥匙串要守护进程与用户确认，不进单测）
qt_add_executable(cookievault_test tests/cookievault_test.cpp cookievault.h cookievault.cpp)
target_link_libraries(cookievault_test PRIVATE Qt6::Core Qt6::Test Qt6Keychain::Qt6Keychain)
add_test(NAME cookievault COMMAND cookievault_test)

# 任何测试都碰不到用户的真实凭据与缓存：会话总线不可达（钥匙串因此走后备文件），数据与缓存目录在构建目录里
set_tests_properties(querycache normalize form audioengine retry cookievault PROPERTIES ENVIRONMENT
    "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache;YUNTING_KEYCHAIN_SERVICE=yunting-test")

target_link_libraries(yunting PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Multimedia Qt6::DBus
    Qt6Keychain::Qt6Keychain
    KF6::StatusNotifierItem
    OpenSSL::Crypto
)

qt_add_executable(crypto_test tests/crypto_test.cpp crypto.h crypto.cpp)
target_link_libraries(crypto_test PRIVATE Qt6::Core Qt6::Test OpenSSL::Crypto)
add_test(NAME crypto COMMAND crypto_test)
set_tests_properties(crypto PROPERTIES ENVIRONMENT
    "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache")

# 共享 Qt 下插件在运行时按 QML import 从 QML2_IMPORT_PATH/插件目录解析，无需链进可执行文件。
# 关掉 finalize 阶段的 qmlimportscanner 扫描：它要把每个 QML 模块的插件目标链进来，
# 而 kirigami/kirigami-addons/prison 的插件目标不在本工程的 CMake 作用域里，只会逐个打 warning。
set_target_properties(yunting PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN ON)

# 安装：可执行文件 + 桌面项 + AppStream 元数据 + 图标；测试可执行文件不装
include(GNUInstallDirs)
install(TARGETS yunting RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ../data/io.github.sudoskys.Yunting.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../data/io.github.sudoskys.Yunting.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ../data/io.github.sudoskys.Yunting.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
