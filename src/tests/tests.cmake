# 测试目标。由 src/CMakeLists.txt 在 BUILD_TESTING 打开时 include：路径仍相对 src/。
# 测试：只测有策略的深模块（QueryCache 的缓存与去重规则、AudioEngine 的音量刻度与钳制、
# NcmClient 的重试、CookieVault 的后备文件规则）
find_package(Qt6 REQUIRED COMPONENTS Test)

# Every SDK parser/service test crosses the same transport seam. Keep the source
# list and link flags here so adding a module is one target call, not a copy of
# CMake boilerplate.
set(YUNTING_SDK_TEST_TRANSPORT
    sdk/sdk_json.h sdk/sdk_json.cpp sdk/service.h sdk/service.cpp
    ncmclient.h ncmclient.cpp traffic.h traffic.cpp cookievault.h cookievault.cpp
    crypto.h crypto.cpp perf.h perf.cpp)
function(yunting_add_sdk_test name)
    qt_add_executable(${name} tests/${name}_test.cpp ${ARGN} ${YUNTING_SDK_TEST_TRANSPORT})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_options(${name} PRIVATE -Wall -Wextra)
    target_link_libraries(${name} PRIVATE Qt6::Core Qt6::Network Qt6::Test
        Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

qt_add_executable(querycache_test tests/querycache_test.cpp querycache.h querycache.cpp perf.h perf.cpp)
target_link_libraries(querycache_test PRIVATE Qt6::Core Qt6::Sql Qt6::Test Qt6::Network)
add_test(NAME querycache COMMAND querycache_test)

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
qt_add_executable(traffic_test tests/traffic_test.cpp traffic.h traffic.cpp)
target_link_libraries(traffic_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME traffic COMMAND traffic_test)

# 纯解析：不联网、不需要 GUI，与 Store/传输层无关
qt_add_executable(link_parser_test tests/link_parser_test.cpp link_parser.h link_parser.cpp)
target_include_directories(link_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(link_parser_test PRIVATE -Wall -Wextra)
target_link_libraries(link_parser_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME link_parser COMMAND link_parser_test)

qt_add_executable(normalize_test tests/normalize_test.cpp normalize.h normalize.cpp json_util.h
    sdk/track.h sdk/track.cpp sdk/sdk_json.h sdk/sdk_json.cpp sdk/sdk_result.h
    sdk/user_vip.h sdk/user_vip.cpp)
target_include_directories(normalize_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(normalize_test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME normalize COMMAND normalize_test)

# 音量/静音与播放走真实 libmpv（ao=null），测试要能构造真实引擎（无窗口、无网络）
qt_add_executable(audioengine_test tests/audioengine_test.cpp audioengine.h audioengine.cpp
    ncmclient.h ncmclient.cpp traffic.h traffic.cpp cookievault.h cookievault.cpp json_util.h crypto.h crypto.cpp
    normalize.h normalize.cpp perf.h perf.cpp
    sdk/track.h sdk/track.cpp sdk/sdk_json.h sdk/sdk_json.cpp sdk/sdk_result.h
    sdk/user_vip.h sdk/user_vip.cpp)
target_include_directories(audioengine_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(audioengine_test PRIVATE -Wall -Wextra)
target_link_libraries(audioengine_test PRIVATE Qt6::Core Qt6::Test Qt6::Network MpvQt::MpvQt Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME audioengine COMMAND audioengine_test)

# 重试策略：seam 是本地 QTcpServer（I/O 边界），不 mock NcmClient 内部
qt_add_executable(retry_test tests/retry_test.cpp ncmclient.h ncmclient.cpp traffic.h traffic.cpp cookievault.h cookievault.cpp crypto.h crypto.cpp perf.h perf.cpp)
target_compile_options(retry_test PRIVATE -Wall -Wextra)
target_link_libraries(retry_test PRIVATE Qt6::Core Qt6::Network Qt6::Test Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME retry COMMAND retry_test)

# CookieVault 只测后备文件路径（钥匙串要守护进程与用户确认，不进单测）
qt_add_executable(cookievault_test tests/cookievault_test.cpp cookievault.h cookievault.cpp)
target_link_libraries(cookievault_test PRIVATE Qt6::Core Qt6::Test Qt6Keychain::Qt6Keychain)
add_test(NAME cookievault COMMAND cookievault_test)

# Store 的缓存/写操作行为：本地假接口，不碰外网
qt_add_executable(store_test tests/store_test.cpp
    store.h store.cpp store_like.cpp store_playlist.cpp store_account.cpp store_follow.cpp store_comment.cpp store_user.cpp store_inbox.cpp store_stats.cpp store_notify.cpp store_social.cpp store_user_events.cpp store_library.cpp store_resource.cpp store_comment_extra.cpp store_lyrics_new.cpp store_on_demand.cpp store_discovery.cpp store_wiki.cpp
    sdk/sdk_json.h sdk/sdk_json.cpp sdk/service.h sdk/service.cpp
    sdk/library.h sdk/library.cpp sdk/notification.h sdk/notification.cpp
    sdk/social.h sdk/social.cpp sdk/dynamic_cover.h sdk/dynamic_cover.cpp
    sdk/song_wiki.h sdk/song_wiki.cpp sdk/song_wiki_service.h sdk/song_wiki_service.cpp
    sdk/discovery.h sdk/discovery.cpp
    sdk/radio.h sdk/radio.cpp
    sdk/feed.h sdk/feed.cpp
    sdk/search_suggest.h sdk/search_suggest.cpp
    sdk/notify.h sdk/notify.cpp
    sdk/search_default.h sdk/search_default.cpp
    sdk/pendant.h sdk/pendant.cpp
    sdk/lyrics.h sdk/lyrics.cpp
    sdk/user_social.h sdk/user_social.cpp
    sdk/user_vip.h sdk/user_vip.cpp
    sdk/comment.h sdk/comment.cpp
    sdk/search_people.h sdk/search_people.cpp
    sdk/mv_page.h sdk/mv_page.cpp
    sdk/account_feed.h sdk/account_feed.cpp
    sdk/listen_data.h sdk/listen_data.cpp
    session.h session.cpp
    querycache.h querycache.cpp
    ncmclient.h ncmclient.cpp traffic.h traffic.cpp cookievault.h cookievault.cpp
    sdk/track.h sdk/track.cpp sdk/track_service.h sdk/track_service.cpp
    normalize.h normalize.cpp json_util.h crypto.h crypto.cpp perf.h perf.cpp)
target_include_directories(store_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(store_test PRIVATE -Wall -Wextra)
target_link_libraries(store_test PRIVATE Qt6::Core Qt6::Sql Qt6::Network Qt6::Test Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME store COMMAND store_test)

# 切歌时挂起的 seek 不能落到下一首：本地假接口 + 本地 WAV
qt_add_executable(audioengine_seek_test tests/audioengine_seek_test.cpp
    audioengine.h audioengine.cpp ncmclient.h ncmclient.cpp traffic.h traffic.cpp
    cookievault.h cookievault.cpp json_util.h crypto.h crypto.cpp normalize.h normalize.cpp perf.h perf.cpp
    sdk/track.h sdk/track.cpp sdk/sdk_json.h sdk/sdk_json.cpp sdk/sdk_result.h
    sdk/user_vip.h sdk/user_vip.cpp)
target_include_directories(audioengine_seek_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(audioengine_seek_test PRIVATE -Wall -Wextra)
target_link_libraries(audioengine_seek_test PRIVATE Qt6::Core Qt6::Test Qt6::Network MpvQt::MpvQt Qt6Keychain::Qt6Keychain OpenSSL::Crypto)
add_test(NAME audioengine_seek COMMAND audioengine_seek_test)

# SDK typed parser/service tests; the function owns transport and link flags.
yunting_add_sdk_test(sdk_song_wiki sdk/song_wiki.h sdk/song_wiki.cpp)
yunting_add_sdk_test(sdk_track sdk/track.h sdk/track.cpp)
yunting_add_sdk_test(sdk_track_service sdk/track_service.h sdk/track_service.cpp
    sdk/track.h sdk/track.cpp)
yunting_add_sdk_test(sdk_discovery sdk/discovery.h sdk/discovery.cpp
    sdk/track.h sdk/track.cpp)
yunting_add_sdk_test(sdk_radio sdk/radio.h sdk/radio.cpp
    sdk/track.h sdk/track.cpp)
yunting_add_sdk_test(sdk_social sdk/social.h sdk/social.cpp)
yunting_add_sdk_test(sdk_feed sdk/feed.h sdk/feed.cpp)
yunting_add_sdk_test(sdk_video sdk/video.h sdk/video.cpp)
yunting_add_sdk_test(sdk_library sdk/library.h sdk/library.cpp)
yunting_add_sdk_test(sdk_notification sdk/notification.h sdk/notification.cpp)
yunting_add_sdk_test(sdk_dynamic_cover sdk/dynamic_cover.h sdk/dynamic_cover.cpp)
yunting_add_sdk_test(sdk_search_suggest sdk/search_suggest.h sdk/search_suggest.cpp)
yunting_add_sdk_test(sdk_service)
yunting_add_sdk_test(notify sdk/notify.h sdk/notify.cpp)
yunting_add_sdk_test(lyrics sdk/lyrics.h sdk/lyrics.cpp)
yunting_add_sdk_test(sdk_listen_data sdk/listen_data.h sdk/listen_data.cpp)
yunting_add_sdk_test(user_social sdk/user_social.h sdk/user_social.cpp
    sdk/user_vip.h sdk/user_vip.cpp)
yunting_add_sdk_test(user_vip sdk/user_vip.h sdk/user_vip.cpp)
yunting_add_sdk_test(comment sdk/comment.h sdk/comment.cpp
    sdk/user_vip.h sdk/user_vip.cpp)
yunting_add_sdk_test(search_people sdk/search_people.h sdk/search_people.cpp)
yunting_add_sdk_test(mv_page sdk/mv_page.h sdk/mv_page.cpp)
yunting_add_sdk_test(account_feed sdk/account_feed.h sdk/account_feed.cpp)
yunting_add_sdk_test(listen_together sdk/listen_together.h sdk/listen_together.cpp
    sdk/user_vip.h sdk/user_vip.cpp)
yunting_add_sdk_test(download sdk/download.h sdk/download.cpp
    sdk/track.h sdk/track.cpp sdk/track_service.h sdk/track_service.cpp
    downloadmanager.h downloadmanager.cpp downloadpaths.h)
yunting_add_sdk_test(search_default sdk/search_default.h sdk/search_default.cpp)
yunting_add_sdk_test(pendant sdk/pendant.h sdk/pendant.cpp)

# 任何测试都碰不到用户的真实凭据与缓存：会话总线不可达（钥匙串因此走后备文件），数据与缓存目录在构建目录里
set_tests_properties(querycache normalize form clientidentity audioengine audioengine_seek retry cookievault store link_parser sdk_song_wiki sdk_track sdk_track_service sdk_discovery sdk_radio sdk_social sdk_feed sdk_video sdk_library sdk_notification sdk_dynamic_cover sdk_search_suggest sdk_service notify lyrics sdk_listen_data user_social user_vip comment search_people mv_page account_feed listen_together download search_default pendant PROPERTIES ENVIRONMENT
    "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent;XDG_DATA_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/data;XDG_CACHE_HOME=${CMAKE_CURRENT_BINARY_DIR}/test-home/cache;YUNTING_KEYCHAIN_SERVICE=yunting-test")

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
