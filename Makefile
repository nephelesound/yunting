# 云听：常用操作
# Qt6 工具在 /usr/lib/qt6/bin（PATH 里的 qml/qmllint 是 Qt5 的，别用）

APP       := build/app/yunting
QML       ?= /usr/lib/qt6/bin/qml
QMLLINT   ?= /usr/lib/qt6/bin/qmllint
QMLFORMAT ?= /usr/lib/qt6/bin/qmlformat

.PHONY: run lint fmt refs shots data app app-build contract test qmltest smoke-readonly

# 应用（C++ 后端 + QML 界面，QML 编进可执行文件的资源）
app-build:
	cmake -S src -B build/app -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
	ninja -C build/app

test: app-build qmltest ## 后端单元测试 + QML 交互测试
	ctest --test-dir build/app --output-on-failure

qmltest: prototype/Personal.js ## 界面交互测试（悬停、点击、滚轮；用快照数据）
	QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde /usr/lib/qt6/bin/qmltestrunner -input prototype/tests

app: app-build ## 运行应用（实时接口 + 真实播放；登录态见 README）
	$(APP)

# 契约检查：C++ Store 的数据形状必须与 snapshot.py 的快照一致（登录与匿名两种会话各查一次）
#（两次都用一次性的缓存目录：检查的是这次取到的形状，不受也不污染日常使用的缓存；
#  匿名那次还要断开会话总线，否则会从钥匙串读到登录凭据）
smoke-readonly: app-build ## 匿名只读 smoke：CLI + 歌手/专辑/评论/MV/用户/电台
	scripts/smoke-readonly.sh

contract: app-build ## 检查 C++ Store 与快照的数据形状一致
	rm -rf build/contract-cache build/contract-cache-anon build/contract-empty-home
	XDG_CACHE_HOME=$(CURDIR)/build/contract-cache $(APP) --dump-store build/store.json
	python3 -I scripts/snapshot.py --out build/contract >/dev/null
	python3 -I scripts/check_contract.py build/store.json build/contract/Data.js build/contract/Personal.js
	mkdir -p build/contract-empty-home
	DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent XDG_CACHE_HOME=$(CURDIR)/build/contract-cache-anon XDG_DATA_HOME=$(CURDIR)/build/contract-empty-home $(APP) --dump-store build/store-anon.json
	python3 -I scripts/snapshot.py --anonymous --out build/contract-anon >/dev/null
	python3 -I scripts/check_contract.py build/store-anon.json build/contract-anon/Data.js build/contract-anon/Personal.js

# 离屏截图走 GPU（OpenGL）路径；默认的 software 后端不画 MultiEffect/ShadowedTexture，看到的不是真实效果
SHOT_ENV = QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl \
           XDG_CACHE_HOME=$(CURDIR)/build/shot-cache
SHOT_SIZE ?= 1280 800

build/qmlshot/qmlshot: scripts/qmlshot/main.cpp scripts/qmlshot/CMakeLists.txt src/colortone.h src/colortone.cpp
	cmake -S scripts/qmlshot -B build/qmlshot -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
	ninja -C build/qmlshot >/dev/null

# 「别人的机器」：发行版默认的 Breeze 配色（stock 浅色、dark 深色）与默认字体 Noto Sans，
# 配置放在临时目录（XDG_CONFIG_HOME），不碰用户自己的 ~/.config。fontconfig 的用户偏好也在 XDG_CONFIG_HOME 下，
# 换掉目录就只剩发行版默认；再排除本机系统设置里用的字体，否则中日文字回退时仍会选到它
SYSTEM_FONT = $(shell grep -m1 '^font=' $(HOME)/.config/kdeglobals 2>/dev/null | sed 's/^font=//; s/,.*//')
# 按文件所在目录排除（字体家族名靠不住：HarmonyOS Sans 的中文是另一个家族 HarmonyOS Sans SC）；本机本来就用 Noto 时不排除
SYSTEM_FONT_DIR = $(if $(filter Noto%,$(SYSTEM_FONT)),,$(shell fc-list "$(SYSTEM_FONT)" file 2>/dev/null | head -1 | sed 's/: *$$//' | xargs -r dirname))
STOCK_FONT = font=Noto Sans,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1
build/stock/kdeglobals build/dark/kdeglobals:
	@mkdir -p $(dir $@)fontconfig
	cp /usr/share/color-schemes/$(if $(findstring dark,$@),BreezeDark,BreezeLight).colors $@
	printf '\n[Icons]\nTheme=$(if $(findstring dark,$@),breeze-dark,breeze)\n\n[General]\n$(STOCK_FONT)\n' >> $@
	printf '<?xml version="1.0"?>\n<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">\n<fontconfig>\n%b</fontconfig>\n' \
		"$(if $(SYSTEM_FONT_DIR),  <selectfont><rejectfont><glob>$(SYSTEM_FONT_DIR)/*</glob></rejectfont></selectfont>\n)" > $(dir $@)fontconfig/fonts.conf

# 截图清单：文件名:根对象属性（逗号分隔，按顺序设置）
SHOTS = home:page=home home-anon:account=anonymous,page=home charts:page=charts \
        daily:listKey=daily,page=list hot:listKey=hot,page=list nowplaying:page=nowplaying mine-anon:account=anonymous,page=mine \
        worst-home:fixture=worst,page=home worst-daily:fixture=worst,listKey=daily,page=list worst-charts:fixture=worst,page=charts \
        worst-nowplaying:fixture=worst,page=nowplaying worst-comments:fixture=worst,nowPlayingPane=comments,page=nowplaying empty-home:fixture=empty,page=home empty-daily:fixture=empty,listKey=daily,page=list \
        one-home:fixture=one,page=home one-daily:fixture=one,listKey=daily,page=list \
        queue:page=home,startOverlay=queue empty-queue:fixture=empty,page=home,startOverlay=queue \
        login:account=anonymous,page=home,startOverlay=login login-scanned:account=anonymous,page=home,loginState=scanned,startOverlay=login \
        login-expired:account=anonymous,page=home,loginState=expired,startOverlay=login \
        offline:simulateOffline=true,page=home session-expired:account=anonymous,simulateExpired=true,page=home \
        mine:page=mine worst-mine:fixture=worst,page=mine empty-mine:fixture=empty,page=mine \
        search:page=search search-results:searchQuery=林俊杰,page=search worst-search:fixture=worst,searchQuery=x,page=search \
        empty-search:fixture=empty,searchQuery=x,page=search \
        worst-search-albums:fixture=worst,searchQuery=x,searchCollection=albums,page=search \
        worst-artist:fixture=worst,listKey=artist:1,page=artist worst-artist-albums:fixture=worst,listKey=artist:1,artistPane=albums,page=artist \
        worst-album:fixture=worst,listKey=album:1,page=list worst-trackmenu:fixture=worst,listKey=daily,page=list,startOverlay=trackmenu \
        one-wiki:fixture=one,nowPlayingPane=wiki,page=nowplaying worst-wiki:fixture=worst,nowPlayingPane=wiki,page=nowplaying \
        worst-describe:fixture=worst,listKey=playlist:11,page=list,startOverlay=describe \
        worst-saved:fixture=worst,listKey=artists,page=saved worst-suggest:fixture=worst,searchQuery=x,page=search,startOverlay=suggest \
        worst-events:fixture=worst,userId=2,page=user,startPane=events \
        worst-inbox-comments:fixture=worst,page=inbox,startPane=comments one-inbox-forwards:fixture=one,page=inbox,startPane=forwards \
        worst-discover-highquality:fixture=worst,page=charts,startPane=highquality one-discover-programs:fixture=one,page=charts,startPane=programs \
        worst-radio:fixture=worst,listKey=radio:7,page=list \
        worst-search-users:fixture=worst,searchQuery=x,searchCollection=users,page=search worst-reorder:fixture=worst,page=playlists,startOverlay=reorder \
        listen-waiting:fixture=one,listenState=waiting,page=home,startOverlay=queue listen-together:fixture=one,listenState=together,page=home,startOverlay=queue \
        listen-end:fixture=one,listenState=together,page=home,startOverlay=endlisten \
        worst-playlist-comments:fixture=worst,listKey=playlist:11,page=list,startPane=comments \
        worst-downloads:fixture=worst,listKey=downloads,page=list,downloadsFilled=true empty-downloads:fixture=empty,listKey=downloads,page=list \
        worst-mine-downloads:fixture=worst,page=mine,downloadsFilled=true worst-mv:fixture=worst,page=mv,mvId=1

data: ## 重新抓取数据快照（有 gtk4 客户端登录 cookie 时同时生成个人快照）
	scripts/snapshot.py

# 没有个人快照（贡献者、CI）：用入库的匿名替身，不联网、不改 Data.js；要真的个人快照用 make data
prototype/Personal.js:
	cp prototype/Anonymous.js $@

shots: build/qmlshot/qmlshot build/stock/kdeglobals build/dark/kdeglobals prototype/Personal.js ## 各屏截图到 build/shots/（本机配色、发行版默认的浅色与深色、窄窗）
	@mkdir -p build/shots
	@# 每屏一个进程、各核并行：两百多张逐张渲染要好几分钟。任何一张失败，xargs 非零退出
	@printf '%s\n' $(SHOTS) | xargs -P $$(nproc) -I{} sh -c '\
		n=$${1%%:*}; props=$$(echo $${1#*:} | tr , " "); \
		$(SHOT_ENV) build/qmlshot/qmlshot prototype/main.qml build/shots/$$n.png $(SHOT_SIZE) -- $$props && \
		$(SHOT_ENV) XDG_CONFIG_HOME=$(CURDIR)/build/stock build/qmlshot/qmlshot prototype/main.qml build/shots/$$n-stock.png $(SHOT_SIZE) -- $$props && \
		$(SHOT_ENV) XDG_CONFIG_HOME=$(CURDIR)/build/dark build/qmlshot/qmlshot prototype/main.qml build/shots/$$n-dark.png $(SHOT_SIZE) -- $$props && \
		$(SHOT_ENV) build/qmlshot/qmlshot prototype/main.qml build/shots/$$n-narrow.png 960 700 -- $$props' _ {}

run: ## 运行界面原型
	$(QML) prototype/main.qml

# qmllint 默认有警告也返回 0：--max-warnings 0 让任何警告都算失败。原型的 Snapshot.qml 引用快照文件，所以先要有快照。
# 相对路径下 qmllint 会误报 qmldir 里的单例未声明，所以用绝对路径
lint: prototype/Personal.js ## 静态检查（QML）
	$(QMLLINT) --max-warnings 0 -I $(CURDIR)/prototype $(CURDIR)/prototype/*.qml

fmt: ## 格式化（QML）
	$(QMLFORMAT) -i prototype/*.qml

refs: ## 按 scripts/references.txt 记录的 SHA 复原参考库
	scripts/fetch-references.sh
