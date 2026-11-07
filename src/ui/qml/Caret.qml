import QtQuick

// A small triangle, pointing the way it leads.
Canvas {
    id: caret

    property int rotationAngle: 0  // 0 points right
    property color fill: Qt.rgba(palette.windowText.r, palette.windowText.g, palette.windowText.b, 0.55)

    implicitWidth: 10
    implicitHeight: 10
    rotation: rotationAngle
    onFillChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = fill
        ctx.beginPath()
        ctx.moveTo(3, 1.5)
        ctx.lineTo(7.5, 5)
        ctx.lineTo(3, 8.5)
        ctx.closePath()
        ctx.fill()
    }
}
