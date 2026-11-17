import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

DialogWindow {
    id: dialog
    objectName: "settingsDialog"
    title: "Settings"
    sizeKey: "settings"
    initialWidth: 760
    initialHeight: 560
    minimumWidth: 680
    minimumHeight: 400

    function start() {
        if (draft.begin()) open()
    }

    function pendingValue(control) {
        return control.contentItem.activeFocus && control.contentItem.acceptableInput
                && control.valueFromText(control.contentItem.text, control.locale) !== control.value
    }

    AppearanceDraft {
        id: draft
        objectName: "appearanceDraft"
        onActivationRequested: { dialog.raise(); dialog.requestActivate() }
    }

    // Keep DialogWindow's geometry handling; hiding by any route ends the preview.
    Connections {
        target: dialog
        function onVisibleChanged() { if (!dialog.visible) draft.cancel() }
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: dialog.padding

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: dialog.padding

            ListFrame {
                Layout.preferredWidth: 160
                Layout.fillHeight: true
                focused: sections.activeFocus
                ListView {
                    id: sections
                    anchors.fill: parent
                    anchors.margins: Theme.paneInset
                    model: ["Appearance"]
                    currentIndex: 0
                    focus: true
                    clip: true
                    delegate: ItemDelegate {
                        required property string modelData
                        width: ListView.view.width
                        height: Theme.controlHeight
                        text: modelData
                        onClicked: sections.forceActiveFocus()
                        background: SelectionBackground { selected: true; focused: sections.activeFocus }
                    }
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    width: parent.width
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
                            value: draft.hue
                            onMoved: draft.hue = value
                            Accessible.name: "Hue"
                        }
                        SpinBox {
                            id: hueInput
                            objectName: "hueInput"
                            Layout.preferredWidth: 112
                            from: 0; to: 360
                            editable: true
                            live: false
                            value: draft.hue
                            valueFromText: (text, locale) => Number.fromLocaleString(locale, text)
                            onValueModified: draft.hue = value
                            Accessible.name: "Hue in degrees"
                        }

                        Label { text: "Accent strength" }
                        Slider {
                            objectName: "accentSlider"
                            Layout.fillWidth: true
                            from: 0; to: Math.max(0.15, draft.accentChroma); stepSize: 0.001
                            value: draft.accentChroma
                            onMoved: draft.accentChroma = value
                            Accessible.name: "Accent strength"
                        }
                        DecimalSpinBox {
                            id: accentInput
                            objectName: "accentInput"
                            Layout.preferredWidth: 112
                            decimals: 3
                            live: false
                            realTo: Math.max(0.15, draft.accentChroma)
                            realStepSize: 0.001
                            onRealValueModified: value => draft.accentChroma = value
                            Accessible.name: "Accent strength"
                            Binding on realValue { value: draft.accentChroma }
                        }

                        Label { text: "Surface tint" }
                        Slider {
                            objectName: "surfaceSlider"
                            Layout.fillWidth: true
                            from: 0; to: Math.max(0.04, draft.surfaceChroma); stepSize: 0.001
                            value: draft.surfaceChroma
                            onMoved: draft.surfaceChroma = value
                            Accessible.name: "Surface tint"
                        }
                        DecimalSpinBox {
                            id: surfaceInput
                            objectName: "surfaceInput"
                            Layout.preferredWidth: 112
                            decimals: 3
                            live: false
                            realTo: Math.max(0.04, draft.surfaceChroma)
                            realStepSize: 0.001
                            onRealValueModified: value => draft.surfaceChroma = value
                            Accessible.name: "Surface tint"
                            Binding on realValue { value: draft.surfaceChroma }
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
                        implicitHeight: preview.implicitHeight + dialog.padding * 2
                        RowLayout {
                            id: preview
                            anchors.fill: parent
                            anchors.margins: dialog.padding
                            spacing: dialog.padding
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
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.paneFrame }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Button {
                objectName: "resetAppearanceButton"
                text: "Reset appearance"
                onClicked: { layout.forceActiveFocus(); draft.resetDefaults() }
            }
            Item { Layout.fillWidth: true }
            DialogButtonBox {
                Button {
                    objectName: "settingsCancelButton"
                    text: "Cancel"
                    DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                }
                Button {
                    objectName: "settingsApplyButton"
                    text: "Apply"
                    // SpinBox commits on Enter/focus loss. Keep Apply available while
                    // valid text is pending; clicking it commits that editor first.
                    enabled: draft.dirty
                             || dialog.pendingValue(hueInput)
                             || dialog.pendingValue(accentInput)
                             || dialog.pendingValue(surfaceInput)
                    DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
                }
                Button {
                    objectName: "settingsOkButton"
                    text: "OK"
                    highlighted: true
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                }
                onApplied: { layout.forceActiveFocus(); draft.apply() }
                onAccepted: { layout.forceActiveFocus(); draft.apply(); dialog.close() }
                onRejected: dialog.close()
            }
        }
    }
}
