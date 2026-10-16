# NCM Player — 常用操作
# Qt6 工具在 /usr/lib/qt6/bin（PATH 里的 qml/qmllint 是 Qt5 的，别用）

QML       ?= /usr/lib/qt6/bin/qml
QMLLINT   ?= /usr/lib/qt6/bin/qmllint
QMLFORMAT ?= /usr/lib/qt6/bin/qmlformat

.PHONY: run lint fmt refs

run: ## 运行界面原型
	$(QML) prototype/main.qml

lint: ## 静态检查（QML）
	$(QMLLINT) prototype/*.qml

fmt: ## 格式化（QML）
	$(QMLFORMAT) -i prototype/*.qml

refs: ## 按 scripts/references.txt 记录的 SHA 复原参考库
	scripts/fetch-references.sh
