import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

SettingsPage {
    id: page
    required property AppearanceDraft draft
    readonly property bool pendingEdits: hueInput.pendingEdit || accentInput.pendingEdit || surfaceInput.pendingEdit

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.panelSpacing * 2
        Label { text: "Appearance"; font.pointSize: Theme.titleSize }
        Label {
            Layout.fillWidth: true
            text: "Preview colours throughout the application."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.panelSpacing
            rowSpacing: Theme.panelSpacing * 2
            Label { text: "Hue"; Layout.preferredWidth: 112 }
            Slider {
                objectName: "hueSlider"
                Layout.fillWidth: true
                from: 0; to: 360; stepSize: 1
                value: page.draft.hue
                onMoved: page.draft.hue = value
                Accessible.name: "Hue"
            }
            DecimalSpinBox {
                id: hueInput
                objectName: "hueInput"
                Layout.preferredWidth: 112
                decimals: 0
                realTo: 360
                realStepSize: 1
                live: false
                Binding on realValue { value: page.draft.hue }
                onRealValueModified: value => page.draft.hue = value
                Accessible.name: "Hue in degrees"
            }
            Label { text: "Accent strength" }
            Slider {
                objectName: "accentSlider"
                Layout.fillWidth: true
                from: 0; to: Math.max(0.15, page.draft.accentChroma); stepSize: 0.001
                value: page.draft.accentChroma
                onMoved: page.draft.accentChroma = value
                Accessible.name: "Accent strength"
            }
            DecimalSpinBox {
                id: accentInput
                objectName: "accentInput"
                Layout.preferredWidth: 112
                decimals: 3
                live: false
                realTo: Math.max(0.15, page.draft.accentChroma)
                realStepSize: 0.001
                Binding on realValue { value: page.draft.accentChroma }
                onRealValueModified: value => page.draft.accentChroma = value
                Accessible.name: "Accent strength"
            }
            Label { text: "Surface tint" }
            Slider {
                objectName: "surfaceSlider"
                Layout.fillWidth: true
                from: 0; to: Math.max(0.04, page.draft.surfaceChroma); stepSize: 0.001
                value: page.draft.surfaceChroma
                onMoved: page.draft.surfaceChroma = value
                Accessible.name: "Surface tint"
            }
            DecimalSpinBox {
                id: surfaceInput
                objectName: "surfaceInput"
                Layout.preferredWidth: 112
                decimals: 3
                live: false
                realTo: Math.max(0.04, page.draft.surfaceChroma)
                realStepSize: 0.001
                Binding on realValue { value: page.draft.surfaceChroma }
                onRealValueModified: value => page.draft.surfaceChroma = value
                Accessible.name: "Surface tint"
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Set surface tint to zero for neutral gray surfaces."
            color: Theme.textMuted
            font.pointSize: Theme.smallSize
            wrapMode: Text.WordWrap
        }
        Label { text: "Controls preview"; font.pointSize: Theme.titleSize }
        ListFrame {
            Layout.fillWidth: true
            implicitHeight: preview.implicitHeight + Theme.panelMargin * 2
            RowLayout {
                id: preview
                anchors.fill: parent
                anchors.margins: Theme.panelMargin
                spacing: Theme.panelSpacing * 2
                ListFrame {
                    Layout.preferredWidth: 180
                    Layout.fillHeight: true
                    implicitHeight: Theme.controlHeight * 4 + Theme.paneInset * 2
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: Theme.paneInset
                        spacing: 0
                        Label {
                            text: "Example list"
                            Layout.leftMargin: Theme.controlPadding
                            Layout.preferredHeight: Theme.controlHeight
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.textMuted
                        }
                        ItemDelegate {
                            text: "Selected item"
                            Layout.fillWidth: true
                            background: SelectionBackground { selected: true; focused: false }
                        }
                        ItemDelegate { text: "Another item"; Layout.fillWidth: true }
                        ItemDelegate { text: "Unavailable"; enabled: false; Layout.fillWidth: true }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.panelSpacing
                    CheckBox { text: "Example option"; checked: true }
                    TextField {
                        Layout.fillWidth: true
                        text: "Example text"
                        Accessible.name: "Preview text field"
                    }
                    RowLayout {
                        spacing: Theme.panelSpacing
                        Button { text: "Button" }
                        Button { text: "Default"; highlighted: true }
                    }
                }
            }
        }
    }
}
