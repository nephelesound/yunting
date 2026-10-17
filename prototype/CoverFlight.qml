// 共享封面转场：打开 / 关闭正在播放页时，封面从播放条飞到正在播放页（返回时飞回）。
// 用途：空间一致（这一页从哪来、回哪去）。每天几十次的动作，所以快而轻：motionBase、强 ease-out、不回弹。
// 只移动一张封面副本，用 translate + scale（不改宽高、不触发重排）；落地后交还给页面里的真封面。
// 时长取自 Kirigami.Units：系统关掉动画时为 0，直接到位。
import QtQuick

Item {
    id: flight

    // 坐标都是窗口（场景）坐标：起点、终点矩形 { x, y, width, height }
    function fly(source, from, to, landed) {
        if (!source || from.width <= 0 || to.width <= 0) {
            if (landed)
                landed();
            return;
        }
        flight.landed = landed || null;
        // 副本按终点尺寸排版，起点用缩放表达：只动 transform
        cover.x = to.x;
        cover.y = to.y;
        cover.width = to.width;
        cover.height = to.height;
        cover.source = source;
        shift.x = from.x - to.x + (from.width - to.width) / 2;
        shift.y = from.y - to.y + (from.height - to.height) / 2;
        zoom.xScale = from.width / to.width;
        zoom.yScale = from.height / to.height;
        cover.visible = true;
        run.restart();
    }
    property var landed: null

    Cover {
        id: cover
        visible: false
        pixelSize: 560
        transform: [
            Scale { id: zoom; origin.x: cover.width / 2; origin.y: cover.height / 2 },
            Translate { id: shift }
        ]
    }

    ParallelAnimation {
        id: run
        NumberAnimation { target: shift; properties: "x,y"; to: 0; duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
        NumberAnimation { target: zoom; properties: "xScale,yScale"; to: 1; duration: Tokens.motionBase; easing.type: Easing.BezierSpline; easing.bezierCurve: Tokens.easeOut }
        onFinished: {
            cover.visible = false;
            if (flight.landed)
                flight.landed();
            flight.landed = null;
        }
    }
}
