import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The image under the folders: a title that collapses it, the image, and a line about it.
Rectangle {
    id: panel

    required property BrowseContext context
    required property bool active
    property bool sharp: false
    property alias collapsed: settings.previewCollapsed
    readonly property alias view: surface.view
    readonly property int headerHeight: 26

    color: palette.window

    Settings {
        id: settings
        category: "Browser"
        property bool previewCollapsed: false
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        PaneHeader {
            objectName: "previewHeader"
            Layout.fillWidth: true
            Layout.preferredHeight: panel.headerHeight
            text: "Preview"
            collapsible: true
            collapsed: panel.collapsed
            onToggled: panel.collapsed = !panel.collapsed
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !panel.collapsed
            color: Theme.imageBackground

            ImageSurface {
                id: surface
                anchors.fill: parent
                // Reading the image would be wasted on a closed panel.
                source: panel.active && !panel.collapsed ? panel.context.currentPath : ""
                sharp: panel.sharp
            }

            Connections {
                target: panel.context
                function onFilesEdited(paths) { surface.reloadIfAmong(paths) }
            }

            Label {
                anchors.fill: parent
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
                color: Theme.onImage
                visible: surface.view.status === ImageView.Error
                text: surface.view.errorString
            }
        }

        RowLayout {
            objectName: "previewStatus"
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 2
            visible: !panel.collapsed
            spacing: 4

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Label {
                    objectName: "previewSummary"
                    Layout.fillWidth: true
                    text: surface.view.summary
                    wrapMode: Text.Wrap
                    font.pointSize: Theme.smallSize
                }
                Label {
                    objectName: "previewCamera"
                    Layout.fillWidth: true
                    visible: text !== ""
                    text: surface.view.cameraSummary
                    wrapMode: Text.Wrap
                    font.pointSize: Theme.smallSize
                }
            }
            ToolButton {
                objectName: "previewActualSize"
                Layout.alignment: Qt.AlignTop
                text: "1:1"
                focusPolicy: Qt.NoFocus
                enabled: surface.view.imageWidth > 0
                onClicked: surface.view.actualSize()
            }
            ToolButton {
                objectName: "previewFit"
                Layout.alignment: Qt.AlignTop
                text: "Fit"
                focusPolicy: Qt.NoFocus
                enabled: surface.view.imageWidth > 0
                onClicked: surface.view.fitToView()
            }
        }
    }
}
