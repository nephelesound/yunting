# NCM Player — 常用操作
# Qt6 工具在 /usr/lib/qt6/bin（PATH 里的 qml/qmllint 是 Qt5 的，别用）

QML       ?= /usr/lib/qt6/bin/qml
QMLLINT   ?= /usr/lib/qt6/bin/qmllint
QMLFORMAT ?= /usr/lib/qt6/bin/qmlformat

.PHONY: run lint fmt refs shots data

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
        daily:listKey=daily,page=list hot:listKey=hot,page=list nowplaying:page=nowplaying mine-anon:account=anonymous,page=mine

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
