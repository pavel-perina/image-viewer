import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// The program, its version and what it is built with, each with its licence and a link. Ctrl+C
// copies it all as text, for a bug report.
AppDialog {
    id: dialog

    title: "About Image Viewer"
    width: Math.min(520, parent ? parent.width - 32 : 520)

    function plainText() {
        const lines = ["Image Viewer " + AppInfo.version, ""]
        for (const part of AppInfo.components)
            lines.push(part.name + " " + part.version + " (" + part.licence + ")")
        return lines.join("\n")
    }

    onOpened: okButton.forceActiveFocus()

    Shortcut {
        sequences: [StandardKey.Copy]
        enabled: dialog.opened
        onActivated: {
            copier.text = dialog.plainText()
            copier.selectAll()
            copier.copy()
        }
    }

    footer: DialogButtonBox {
        Button {
            objectName: "licenceTextsButton"
            text: "Licence Texts"
            visible: AppInfo.licencesUrl.toString() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: Qt.openUrlExternally(AppInfo.licencesUrl)
        }
        Button {
            id: okButton
            text: "OK"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            Keys.onReturnPressed: dialog.accept()
            Keys.onEnterPressed: dialog.accept()
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 6

        // QML has no clipboard of its own.
        TextEdit {
            id: copier
            visible: false
            textFormat: TextEdit.PlainText
        }

        Label {
            text: "Image Viewer"
            font.pixelSize: dialog.font.pixelSize * 1.5
            font.weight: Font.DemiBold
        }
        Label {
            objectName: "aboutVersion"
            text: "Version " + AppInfo.version
            color: Theme.textMuted
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: "A fast, multiplatform image viewer and basic editor.\nMIT licence."
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.topMargin: 10
            text: "Built with"
            font.weight: Font.DemiBold
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 16
            rowSpacing: 3

            Repeater {
                objectName: "componentRows"
                model: AppInfo.components.length * 3
                Label {
                    required property int index
                    readonly property var part: AppInfo.components[Math.floor(index / 3)]
                    readonly property int column: index % 3
                    readonly property bool isLink: column === 0

                    Layout.fillWidth: column === 2
                    text: column === 0 ? part.name : column === 1 ? part.version : part.licence
                    color: isLink ? Theme.link : column === 1 ? Theme.textMuted : Theme.text
                    font.underline: isLink && linkArea.containsMouse
                    elide: Text.ElideRight

                    MouseArea {
                        id: linkArea
                        anchors.fill: parent
                        enabled: parent.isLink
                        hoverEnabled: true
                        cursorShape: parent.isLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                        onClicked: Qt.openUrlExternally(parent.part.url)
                    }
                    ToolTip.visible: linkArea.containsMouse
                    ToolTip.delay: 600
                    ToolTip.text: part.url
                }
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 8
            visible: AppInfo.licencesUrl.toString() !== ""
            text: "The full licence texts, including those of the libraries these depend on, are installed with the program."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
    }
}
