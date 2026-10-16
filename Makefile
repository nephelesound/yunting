# NCM Player — 常用操作
# Qt6 工具在 /usr/lib/qt6/bin（PATH 里的 qml/qmllint 是 Qt5 的，别用）

QML       ?= /usr/lib/qt6/bin/qml
QMLLINT   ?= /usr/lib/qt6/bin/qmllint
QMLFORMAT ?= /usr/lib/qt6/bin/qmlformat

.PHONY: run lint fmt refs shots

# 离屏截图走 GPU（OpenGL）路径；默认的 software 后端不画 MultiEffect/ShadowedTexture，看到的不是真实效果
SHOT_ENV = QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME=kde QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl
SHOT_SIZE ?= 1280 800

build/qmlshot/qmlshot: scripts/qmlshot/main.cpp scripts/qmlshot/CMakeLists.txt
	cmake -S scripts/qmlshot -B build/qmlshot -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
	ninja -C build/qmlshot >/dev/null

shots: build/qmlshot/qmlshot ## 把原型各页截图到 build/shots/
	@mkdir -p build/shots
	@for p in discover playlist nowplaying mine; do \
		$(SHOT_ENV) build/qmlshot/qmlshot prototype/main.qml build/shots/$$p.png $(SHOT_SIZE) -- page=$$p || exit 1; \
	done

run: ## 运行界面原型
	$(QML) prototype/main.qml

lint: ## 静态检查（QML）
	$(QMLLINT) -I prototype prototype/*.qml

fmt: ## 格式化（QML）
	$(QMLFORMAT) -i prototype/*.qml

refs: ## 按 scripts/references.txt 记录的 SHA 复原参考库
	scripts/fetch-references.sh
