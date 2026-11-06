pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The histogram and the details of the image in a view.
Rectangle {
    id: panel

    required property ImageView view
    property bool logScale: false
    property bool fitRange: false

    width: 320
    color: "#e6181818"

    // Keeps the mouse from reaching the image underneath.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => wheel.accepted = true
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 12
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        ScrollBar.vertical: AppScrollBar {}

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 4

            HistogramView {
                id: histogramView
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                histogram: panel.view.histogram
                logScale: panel.logScale
                fitRange: panel.fitRange
            }

            Label {
                Layout.fillWidth: true
                text: histogramView.description
                color: "#a0a0a0"
                font.pointSize: Qt.application.font.pointSize - 1
                elide: Text.ElideRight
            }

            RowLayout {
                CheckBox {
                    text: "Log scale"
                    checked: panel.logScale
                    onToggled: panel.logScale = checked
                }
                CheckBox {
                    text: "Fit range"
                    checked: panel.fitRange
                    onToggled: panel.fitRange = checked
                }
            }

            Repeater {
                model: panel.view.details

                delegate: ColumnLayout {
                    id: row
                    required property var modelData
                    required property int index
                    readonly property bool startsSection: index === 0 || panel.view.details[index - 1].section !== modelData.section

                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        visible: row.startsSection
                        Layout.topMargin: 8
                        text: row.modelData.section
                        font.bold: true
                        color: "#e0e0e0"
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.preferredWidth: 90
                            Layout.alignment: Qt.AlignTop
                            text: row.modelData.label
                            color: "#a0a0a0"
                        }
                        Label {
                            Layout.fillWidth: true
                            text: row.modelData.value
                            color: "#e0e0e0"
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
