cmake_minimum_required(VERSION 3.21)
project(yunting VERSION 0.6.24 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2 Network DBus Sql)
find_package(MpvQt REQUIRED)
find_package(Qt6Keychain REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)
find_package(KF6CoreAddons REQUIRED)   # KAboutData（「关于」页）、KSignalHandler
find_package(KF6DBusAddons REQUIRED)   # 单实例：KDBusService（cli.cpp 的 runGui）
find_package(KF6WindowSystem REQUIRED) # 链接打开时用激活令牌把窗口提到前面（links.cpp）
find_package(KF6ColorScheme REQUIRED)  # 中性配色下的应用色调（colortone.cpp）
find_package(KF6Config REQUIRED)
find_package(KF6I18n REQUIRED)         # kirigami-addons 的页面（「关于」）在 QML 里用 i18nd()
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(Fontconfig REQUIRED)        # 中日文回退落在同一款字（uifont.cpp）
find_package(LayerShellQt QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD QUIET libzstd)
include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/material-color-utilities/mcu.cmake)   # 中性配色下的 Material You 配色（materialyou.cpp）

set(YUNTING_SDK_SOURCES
    sdk/sdk_json.h sdk/sdk_json.cpp sdk/service.h sdk/service.cpp
    sdk/track.h sdk/track.cpp sdk/track_service.h sdk/track_service.cpp
    sdk/song_wiki.h sdk/song_wiki.cpp sdk/song_wiki_service.h sdk/song_wiki_service.cpp
    sdk/discovery.h sdk/discovery.cpp
    sdk/radio.h sdk/radio.cpp
    sdk/social.h sdk/social.cpp
    sdk/feed.h sdk/feed.cpp
    sdk/video.h sdk/video.cpp
    sdk/library.h sdk/library.cpp
    sdk/notification.h sdk/notification.cpp
    sdk/dynamic_cover.h sdk/dynamic_cover.cpp
    sdk/search_suggest.h sdk/search_suggest.cpp
    sdk/notify.h sdk/notify.cpp
    sdk/search_default.h sdk/search_default.cpp
    sdk/pendant.h sdk/pendant.cpp
    sdk/friend_listening.h sdk/friend_listening.cpp
    sdk/lyrics.h sdk/lyrics.cpp
    sdk/user_social.h sdk/user_social.cpp
    sdk/user_vip.h sdk/user_vip.cpp
    sdk/comment.h sdk/comment.cpp
    sdk/search_people.h sdk/search_people.cpp
    sdk/mv_page.h sdk/mv_page.cpp
    sdk/account_feed.h sdk/account_feed.cpp
    sdk/listen_data.h sdk/listen_data.cpp
    sdk/listen_together.h sdk/listen_together.cpp
    sdk/download.h sdk/download.cpp
)

add_library(yunting_color STATIC
    materialyou.h materialyou.cpp
    colortone.h colortone.cpp)
target_include_directories(yunting_color PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(yunting_color PRIVATE -Wall -Wextra)
target_link_libraries(yunting_color PUBLIC Qt6::Gui KF6::ColorScheme KF6::ConfigCore material_color_utilities)

qt_add_executable(yunting
    main.cpp cli.h cli.cpp
    link_parser.h link_parser.cpp links.h links.cpp
    listentogether.h listentogether.cpp
    downloadmanager.h downloadmanager.cpp
    clouduploader.h clouduploader.cpp
    downloadpaths.h
    ncmclient.h ncmclient.cpp traffic.h traffic.cpp
    cookievault.h cookievault.cpp
    store.h store.cpp store_like.cpp store_playlist.cpp store_account.cpp store_follow.cpp store_comment.cpp store_user.cpp store_inbox.cpp store_stats.cpp store_notify.cpp store_social.cpp store_user_events.cpp store_library.cpp store_resource.cpp store_comment_extra.cpp store_lyrics_new.cpp store_on_demand.cpp store_discovery.cpp store_wiki.cpp store_cloud.cpp
    ${YUNTING_SDK_SOURCES}
    session.h session.cpp
    querycache.h querycache.cpp
    imagecache.h
    json_util.h
    normalize.h normalize.cpp
    audioengine.h audioengine.cpp
    voiceplayer.h voiceplayer.cpp
    mpvoptions.h
    systemsleep.h systemsleep.cpp
    mvvideo.h mvvideo.cpp
    session.h session.cpp
    mpris.h mpris.cpp
    tray.h tray.cpp
    clipboardhelper.h
    diagnostics.h diagnostics.cpp
    uifont.h uifont.cpp
    powerinhibitor.h powerinhibitor.cpp
    crypto.h crypto.cpp
    perf.h perf.cpp
    layershellhelper.h layershellhelper.cpp
    apicli.h apicli.cpp
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

target_include_directories(yunting PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(yunting PRIVATE -Wall -Wextra)

target_link_libraries(yunting PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Network Qt6::DBus Qt6::Sql
    MpvQt::MpvQt
    Qt6Keychain::Qt6Keychain
    KF6::StatusNotifierItem
    KF6::DBusAddons
    KF6::WindowSystem
    yunting_color
    OpenSSL::Crypto
    Fontconfig::Fontconfig
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
target_compile_definitions(yunting PRIVATE YUNTING_KSIGNALHANDLER=1 YUNTING_VERSION="${PROJECT_VERSION}")
target_link_libraries(yunting PRIVATE KF6::CoreAddons KF6::I18nQml KF6::ColorScheme KF6::ConfigCore)
if (ZSTD_FOUND)
    target_sources(yunting PRIVATE ncbl.h ncbl.cpp)
    target_compile_definitions(yunting PRIVATE YUNTING_NCBL=1)
    target_include_directories(yunting PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_libraries(yunting PRIVATE ${ZSTD_LIBRARIES})
endif()

# 打包（Flatpak、AUR 的 package）只要应用本体：-DBUILD_TESTING=OFF 时一个测试目标都不配置
option(BUILD_TESTING "Build the tests" ON)
if (BUILD_TESTING)
    enable_testing()
    include(tests/tests.cmake)
endif()

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
# 编进程序的 Material Color Utilities（Apache-2.0）：许可证随包发
install(FILES third_party/material-color-utilities/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/yunting
        RENAME MATERIAL-COLOR-UTILITIES-LICENSE)
