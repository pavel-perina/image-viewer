pragma ComponentBehavior: Bound

import QtQuick
import ImageViewer

// The colours offered at a click, as squares; the current one ringed in the accent.
Flow {
    id: swatches

    property color current
    // The name each square is found by.
    property string swatchName: "colourSwatch"
    signal picked(color colour)

    spacing: 4

    Repeater {
        model: Theme.annotationColours
        Rectangle {
            id: swatch
            required property color modelData
            readonly property bool current: Qt.colorEqual(swatches.current, modelData)
            objectName: swatches.swatchName
            width: 26
            height: 26
            color: modelData
            border.width: current ? 2 : 1
            border.color: current ? Theme.accent : Theme.swatchFrame
            MouseArea {
                anchors.fill: parent
                onClicked: swatches.picked(swatch.modelData)
            }
        }
    }
}
