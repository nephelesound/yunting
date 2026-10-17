# NCM Player — 常用操作
# Qt6 工具在 /usr/lib/qt6/bin（PATH 里的 qml/qmllint 是 Qt5 的，别用）

QML       ?= /usr/lib/qt6/bin/qml
QMLLINT   ?= /usr/lib/qt6/bin/qmllint
QMLFORMAT ?= /usr/lib/qt6/bin/qmlformat

.PHONY: run lint fmt refs shots data app app-build contract

# ── 应用（C++ 后端 + QML 界面）──
app-build: prototype/Personal.js
	cmake -S src -B build/app -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >/dev/null
	ninja -C build/app

app: app-build ## 运行应用（实时接口 + 真实播放；登录态见 README）
	build/app/ncm-player

# 契约检查：C++ Store 的数据形状必须与 snapshot.py 的快照一致（登录与匿名两种会话各查一次）
contract: app-build ## 检查 C++ Store 与快照的数据形状一致
	build/app/ncm-player --dump-store build/store.json
	python3 -I scripts/snapshot.py --out build/contract >/dev/null
	python3 -I scripts/check_contract.py build/store.json build/contract/Data.js build/contract/Personal.js
	mkdir -p build/contract-empty-home
	XDG_DATA_HOME=$(CURDIR)/build/contract-empty-home build/app/ncm-player --dump-store build/store-anon.json
	python3 -I scripts/snapshot.py --anonymous --out build/contract-anon >/dev/null
	python3 -I scripts/check_contract.py build/store-anon.json build/contract-anon/Data.js build/contract-anon/Personal.js

# 离屏截图走 GPU（OpenGL）路径；默认的 software 后端不画 MultiEffect/ShadowedTexture，看到的不是真实效果
SHOT_ENV = QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl
SHOT_SIZE ?= 1280 800

build/qmlshot/qmlshot: scripts/qmlshot/main.cpp scripts/qmlshot/CMakeLists.txt
	cmake -S scripts/qmlshot -B build/qmlshot -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
	ninja -C build/qmlshot >/dev/null

# 暗色截图用临时配置目录里的 Breeze Dark 配色，不碰用户自己的 ~/.config/kdeglobals
build/dark/kdeglobals:
	@mkdir -p build/dark
	cp /usr/share/color-schemes/BreezeDark.colors $@
	printf '\n[Icons]\nTheme=breeze-dark\n' >> $@
	grep -m1 '^font=' $(HOME)/.config/kdeglobals | sed '1i [General]' >> $@ || true

# 截图清单：文件名:根对象属性（逗号分隔，按顺序设置）
SHOTS = home:page=home home-anon:account=anonymous,page=home charts:page=charts \
        daily:listKey=daily,page=list hot:listKey=hot,page=list nowplaying:page=nowplaying mine-anon:account=anonymous,page=mine \
        worst-home:fixture=worst,page=home worst-daily:fixture=worst,listKey=daily,page=list worst-charts:fixture=worst,page=charts \
        worst-nowplaying:fixture=worst,page=nowplaying empty-home:fixture=empty,page=home empty-daily:fixture=empty,listKey=daily,page=list \
        one-home:fixture=one,page=home one-daily:fixture=one,listKey=daily,page=list \
        queue:page=home,startOverlay=queue empty-queue:fixture=empty,page=home,startOverlay=queue \
        login:account=anonymous,page=home,startOverlay=login login-scanned:account=anonymous,page=home,loginState=scanned,startOverlay=login \
        login-expired:account=anonymous,page=home,loginState=expired,startOverlay=login \
        mine:page=mine worst-mine:fixture=worst,page=mine empty-mine:fixture=empty,page=mine \
        search:page=search search-results:searchQuery=林俊杰,page=search worst-search:fixture=worst,searchQuery=x,page=search \
        empty-search:fixture=empty,searchQuery=x,page=search

data: ## 重新抓取数据快照（有 gtk4 客户端登录 cookie 时同时生成个人快照）
	scripts/snapshot.py

prototype/Personal.js:
	scripts/snapshot.py

shots: build/qmlshot/qmlshot build/dark/kdeglobals prototype/Personal.js ## 各屏截图到 build/shots/（系统配色、Breeze Dark、窄窗）
	@mkdir -p build/shots
	@for s in $(SHOTS); do \
		n=$${s%%:*}; props=$$(echo $${s#*:} | tr , ' '); \
		$(SHOT_ENV) build/qmlshot/qmlshot prototype/main.qml build/shots/$$n.png $(SHOT_SIZE) -- $$props || exit 1; \
		$(SHOT_ENV) XDG_CONFIG_HOME=$(CURDIR)/build/dark build/qmlshot/qmlshot prototype/main.qml build/shots/$$n-dark.png $(SHOT_SIZE) -- $$props || exit 1; \
		$(SHOT_ENV) build/qmlshot/qmlshot prototype/main.qml build/shots/$$n-narrow.png 960 700 -- $$props || exit 1; \
	done

run: ## 运行界面原型
	$(QML) prototype/main.qml

lint: ## 静态检查（QML）
	$(QMLLINT) -I prototype prototype/*.qml

fmt: ## 格式化（QML）
	$(QMLFORMAT) -i prototype/*.qml

refs: ## 按 scripts/references.txt 记录的 SHA 复原参考库
	scripts/fetch-references.sh
