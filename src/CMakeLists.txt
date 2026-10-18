cmake_minimum_required(VERSION 3.21)
project(yunting VERSION 0.2.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Network Multimedia DBus Sql)
find_package(Qt6Keychain REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)
find_package(KF6CoreAddons REQUIRED)   # KAboutData（「关于」页）、KSignalHandler
find_package(KF6ColorScheme REQUIRED)  # 中性配色下的应用色调（colortone.cpp）
find_package(KF6Config REQUIRED)
find_package(KF6I18n REQUIRED)         # kirigami-addons 的页面（「关于」）在 QML 里用 i18nd()
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(LayerShellQt QUIET)
find_package(PkgConfig QUIET)
if (PkgConfig_FOUND)
    pkg_check_modules(QRENCODE QUIET libqrencode)
    pkg_check_modules(ZSTD QUIET libzstd)
endif()
option(YUNTING_EXPERIMENTAL_EQ "Build QAudioBufferOutput + biquad EQ path" ON)

qt_add_executable(yunting
    main.cpp cli.h cli.cpp
    ncmclient.h ncmclient.cpp
    cookievault.h cookievault.cpp
    store.h store.cpp store_like.cpp store_playlist.cpp store_account.cpp store_follow.cpp store_comment.cpp store_user.cpp store_inbox.cpp store_stats.cpp store_notify.cpp store_social.cpp store_comment_extra.cpp store_lyrics_new.cpp store_on_demand.cpp
    querycache.h querycache.cpp
    imagecache.h
    json_util.h
    normalize.h normalize.cpp
    audioengine.h audioengine.cpp
    session.h session.cpp
    mpris.h mpris.cpp
    tray.h tray.cpp
    clipboardhelper.h
    diagnostics.h diagnostics.cpp
    colortone.h colortone.cpp
    powerinhibitor.h powerinhibitor.cpp
    crypto.h crypto.cpp
    perf.h perf.cpp
    layershellhelper.h layershellhelper.cpp
    apicli.h apicli.cpp
    eqprocessor.h eqprocessor.cpp
    eqaudiosink.h eqaudiosink.cpp
)

# 界面 QML/JS 编进可执行文件：整个 prototype/ 目录挂到 qrc:/qt/qml/org/nephelesound/yunting/ 下，
# qmldir 与组件同目录，singleton 声明照常解析。Snapshot.qml 与它读的 Data.js / Personal.js /
# Anonymous.js / Fixtures.js 是原型与截图专用，不编入；其中 Personal.js 是账号数据，绝不能进二进制。
file(GLOB _app_qml RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../prototype"
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/icons/*.svg"
    "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/qmldir")
list(FILTER _app_qml EXCLUDE REGEX "^(Snapshot\\.qml|Data\\.js|Personal\\.js|Anonymous\\.js|Fixtures\\.js)$")
# qt_add_resources 的 FILES 接受绝对路径；RELATIVE 摘出的裸文件名会被qt_add_executable 误当源码解析
list(TRANSFORM _app_qml REPLACE "^" "${CMAKE_CURRENT_SOURCE_DIR}/../prototype/")

qt_add_resources(yunting "app_qml"
    PREFIX "/qt/qml/org/nephelesound/yunting"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../prototype"
    FILES ${_app_qml})

# 窗口/任务栏图标：运行时优先取主题里的已安装图标；源码运行时回退到内嵌 SVG
set_source_files_properties("../data/io.github.nephelesound.Yunting.svg"
    PROPERTIES QT_RESOURCE_ALIAS "io.github.nephelesound.Yunting.svg")
qt_add_resources(yunting "app_icon"
    PREFIX "/"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../data/io.github.nephelesound.Yunting.svg")

target_compile_options(yunting PRIVATE -Wall -Wextra)

# 测试：只测有策略的深模块（QueryCache 的缓存与去重规则、AudioEngine 的音量刻度与钳制、
# NcmClient 的重试、CookieVault 的后备文件规则）
find_package(Qt6 REQUIRED COMPONENTS Test)
enable_testing()
qt_add_executable(querycache_test tests/querycache_test.cpp querycache.h querycache.cpp perf.h perf.cpp)
target_link_libraries(querycache_test PRIVATE Qt6::Core Qt6::Sql Qt6::Test Qt6::Network)
add_test(NAME querycache COMMAND querycache_test)

# 归一化形状：曲目导航字段、MV、云盘后备、LRC 译文
qt_add_executable(eqprocessor_test tests/eqprocessor_test.cpp eqprocessor.h eqprocessor.cpp)
target_link_libraries(eqprocessor_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME eqprocessor COMMAND eqprocessor_test)

qt_add_executable(form_test tests/form_test.cpp json_util.h)
target_link_libraries(form_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME form COMMAND form_test)
qt_add_executable(clientidentity_test tests/clientidentity_test.cpp clientidentity.h)
target_link_libraries(clientidentity_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME clientidentity COMMAND clientidentity_test)
qt_add_executable(clipboardhelper_test tests/clipboardhelper_test.cpp clipboardhelper.h)
target_link_libraries(clipboardhelper_test PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME clipboardhelper COMMAND clipboardhelper_test)
set_tests_properties(clipboardhelper PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
qt_add_executable(colortone_test tests/colortone_test.cpp colortone.h colortone.cpp)
target_link_libraries(colortone_test PRIVATE Qt6::Widgets Qt6::Test KF6::ColorScheme KF6::ConfigCore)
add_test(NAME colortone COMMAND colortone_test)
qt_add_executable(diagnostics_test tests/diagnostics_test.cpp diagnostics.h diagnostics.cpp)
target_link_libraries(diagnostics_test PRIVATE Qt6::Gui Qt6::Test)
add_test(NAME diagnostics COMMAND diagnostics_test)



qt_add_executable(normalize_test tests/normalize_test.cpp normalize.h normalize.cpp json_util.h)
target_link_libraries(normalize_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME normalize COMMAND normalize_test)

# 音量/静音走 QAudioOutput，测试要能构造真实引擎（无窗口、无网络）
qt_add_executable(audioengine_test tests/audioengine_test.cpp audioengine.h audioengine.cpp
    ncmclient.h ncmclient.cpp cookievault.h cookievault.cpp json_util.h crypto.h crypto.cpp
    normalize.h normalize.cpp perf.h perf.cpp)
target_compile_options(audioengine_test PRIVATE -Wall -Wextra)
target_link_libraries(audioengine_test PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Multimedia Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME audioengine COMMAND audioengine_test)

# 重试策略：seam 是本地 QTcpServer（I/O 边界），不 mock NcmClient 内部
qt_add_executable(retry_test tests/retry_test.cpp ncmclient.h ncmclient.cpp cookievault.h cookievault.cpp crypto.h crypto.cpp perf.h perf.cpp)
target_compile_options(retry_test PRIVATE -Wall -Wextra)
target_link_libraries(retry_test PRIVATE Qt6::Core Qt6::Network Qt6::Test Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME retry COMMAND retry_test)

# CookieVault 只测后备文件路径（钥匙串要守护进程与用户确认，不进单测）
qt_add_executable(cookievault_test tests/cookievault_test.cpp cookievault.h cookievault.cpp)
target_link_libraries(cookievault_test PRIVATE Qt6::Core Qt6::Test Qt6Keychain::Qt6Keychain)
add_test(NAME cookievault COMMAND cookievault_test)

# Store 的缓存/写操作行为：本地假接口，不碰外网
qt_add_executable(store_test tests/store_test.cpp
    store.h store.cpp store_like.cpp store_playlist.cpp store_account.cpp store_follow.cpp store_comment.cpp store_user.cpp store_inbox.cpp store_stats.cpp store_notify.cpp store_social.cpp store_comment_extra.cpp store_lyrics_new.cpp store_on_demand.cpp
    querycache.h querycache.cpp
    ncmclient.h ncmclient.cpp cookievault.h cookievault.cpp
    normalize.h normalize.cpp json_util.h crypto.h crypto.cpp perf.h perf.cpp)
target_compile_options(store_test PRIVATE -Wall -Wextra)
target_link_libraries(store_test PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::Test Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME store COMMAND store_test)

# 切歌时挂起的 seek 不能落到下一首：本地假接口 + 本地 WAV
qt_add_executable(audioengine_seek_test tests/audioengine_seek_test.cpp
    audioengine.h audioengine.cpp ncmclient.h ncmclient.cpp
    cookievault.h cookievault.cpp json_util.h crypto.h crypto.cpp normalize.h normalize.cpp perf.h perf.cpp)
target_compile_options(audioengine_seek_test PRIVATE -Wall -Wextra)
target_link_libraries(audioengine_seek_test PRIVATE Qt6::Core Qt6::Test Qt6::Network Qt6::Multimedia Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME audioengine_seek COMMAND audioengine_seek_test)

# 任何测试都碰不到用户的真实凭据与缓存：会话总线不可达（钥匙串因此走后备文件），数据与缓存目录在构建目录里
set_tests_properties(querycache normalize form eqprocessor clientidentity audioengine audioengine_seek retry cookievault store PROPERTIES ENVIRONMENT
    "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache;YUNTING_KEYCHAIN_SERVICE=yunting-test")

target_link_libraries(yunting PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::Multimedia Qt6::DBus Qt6::Sql
    Qt6Keychain::Qt6Keychain
    KF6::StatusNotifierItem
    OpenSSL::Crypto
)
if (LayerShellQt_FOUND)
    target_compile_definitions(yunting PRIVATE YUNTING_LAYERSHELL=1)
    target_link_libraries(yunting PRIVATE LayerShellQt::Interface)
    # 拖动 overlay 用相对指针协议（协议 XML 原样取自 wayland-protocols，MIT）
    find_package(Qt6 QUIET COMPONENTS WaylandClient)
    if (Qt6WaylandClient_FOUND)
        enable_language(C)
        qt_generate_wayland_protocol_client_sources(yunting PRIVATE_CODE
            FILES ${CMAKE_CURRENT_SOURCE_DIR}/wayland/relative-pointer-unstable-v1.xml)
        target_compile_definitions(yunting PRIVATE YUNTING_RELATIVE_POINTER=1)
        target_link_libraries(yunting PRIVATE Qt6::WaylandClient)
    endif()
endif()
if (YUNTING_EXPERIMENTAL_EQ)
    target_compile_definitions(yunting PRIVATE YUNTING_EXPERIMENTAL_EQ=1)
endif()
target_compile_definitions(yunting PRIVATE YUNTING_KSIGNALHANDLER=1 YUNTING_VERSION="${PROJECT_VERSION}")
target_link_libraries(yunting PRIVATE KF6::CoreAddons KF6::I18nQml KF6::ColorScheme KF6::ConfigCore)
if (QRENCODE_FOUND)
    target_sources(yunting PRIVATE qrcodeimage.h qrcodeimage.cpp)
    target_compile_definitions(yunting PRIVATE YUNTING_QRCODE=1)
    target_include_directories(yunting PRIVATE ${QRENCODE_INCLUDE_DIRS})
    target_link_libraries(yunting PRIVATE ${QRENCODE_LIBRARIES})
endif()
if (ZSTD_FOUND)
    target_sources(yunting PRIVATE ncbl.h ncbl.cpp)
    target_compile_definitions(yunting PRIVATE YUNTING_NCBL=1)
    target_include_directories(yunting PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_libraries(yunting PRIVATE ${ZSTD_LIBRARIES})
endif()

# 电源抑制：对着真的会话总线测（没有 Plasma 电源管理时自行跳过），所以不像其他测试那样断开总线
qt_add_executable(powerinhibitor_test tests/powerinhibitor_test.cpp powerinhibitor.h powerinhibitor.cpp)
target_link_libraries(powerinhibitor_test PRIVATE Qt6::Core Qt6::Gui Qt6::DBus Qt6::Test)
add_test(NAME powerinhibitor COMMAND powerinhibitor_test)

if (ZSTD_FOUND)
    qt_add_executable(ncbl_test tests/ncbl_test.cpp ncbl.h ncbl.cpp)
    target_compile_definitions(ncbl_test PRIVATE YUNTING_NCBL=1)
    target_include_directories(ncbl_test PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_libraries(ncbl_test PRIVATE Qt6::Core Qt6::Test OpenSSL::Crypto ${ZSTD_LIBRARIES})
    add_test(NAME ncbl COMMAND ncbl_test)
    set_tests_properties(ncbl PROPERTIES ENVIRONMENT
        "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache")
endif()

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
install(FILES ../data/io.github.nephelesound.Yunting.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ../data/io.github.nephelesound.Yunting.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ../data/io.github.nephelesound.Yunting.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES ../data/ICON-LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/yunting)
install(FILES ../prototype/icons/LICENSES.txt
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/yunting
        RENAME UI-ICONS-LICENSES)
