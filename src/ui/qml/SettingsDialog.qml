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

    function start() { if (settings.begin()) open() }
    function resetSection() {
        layout.forceActiveFocus()
        if (sections.currentIndex === 0) settings.appearance.resetDefaults()
        else if (sections.currentIndex === 1) settings.resetViewing()
        else if (sections.currentIndex === 2) settings.resetThumbnails()
        else settings.resetBehaviour()
    }
    SettingsDraft {
        id: settings
        objectName: "settingsDraft"
        onActivationRequested: { dialog.raise(); dialog.requestActivate() }
        onApplied: closeAfter => {
            if (thumbnailsPage.visible) ThumbnailStore.measure()
            if (closeAfter) dialog.close()
        }
    }
    Connections {
        target: dialog
        function onVisibleChanged() { if (!dialog.visible) settings.cancel() }
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
                    objectName: "settingsSections"
                    anchors.fill: parent
                    anchors.margins: Theme.paneInset
                    model: ["Appearance", "Viewing", "Thumbnails", "Behaviour"]
                    currentIndex: 0
                    focus: true
                    clip: true
                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index
                        width: ListView.view.width
                        height: Theme.controlHeight
                        text: modelData
                        icon.source: ["qrc:/icons/preferences-desktop-theme.svg", "qrc:/icons/view-image.svg", "qrc:/icons/view-browser.svg", "qrc:/icons/adjust-colours.svg"][index]
                        icon.color: Theme.text
                        onClicked: { sections.currentIndex = index; sections.forceActiveFocus() }
                        background: SelectionBackground { selected: sections.currentIndex === index; focused: sections.activeFocus }
                    }
                }
            }
            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: sections.currentIndex
                enabled: !settings.busy
                SettingsAppearancePage { id: appearancePage; draft: settings.appearance }
                SettingsViewingPage { draft: settings }
                SettingsThumbnailsPage { id: thumbnailsPage; draft: settings }
                SettingsBehaviourPage { draft: settings }
            }
        }
        Label {
            objectName: "settingsProblem"
            Layout.fillWidth: true
            visible: settings.problem !== "" || settings.busy
            text: settings.busy ? "Checking display profile…" : settings.problem
            color: settings.busy ? Theme.textMuted : Theme.danger
            wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.paneFrame }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Button {
                objectName: ["resetAppearanceButton", "resetViewingButton", "resetThumbnailsButton", "resetBehaviourButton"][sections.currentIndex]
                text: ["Reset appearance", "Reset viewing", "Reset thumbnails", "Reset behaviour"][sections.currentIndex]
                enabled: !settings.busy
                onClicked: dialog.resetSection()
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
                    enabled: !settings.busy && (settings.dirty || appearancePage.pendingEdits || thumbnailsPage.pendingEdits)
                    DialogButtonBox.buttonRole: DialogButtonBox.ApplyRole
                }
                Button {
                    objectName: "settingsOkButton"
                    text: "OK"
                    enabled: !settings.busy
                    highlighted: true
                    DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
                }
                onApplied: { layout.forceActiveFocus(); settings.apply() }
                onAccepted: { layout.forceActiveFocus(); settings.apply(true) }
                onRejected: dialog.close()
            }
        }
    }
}
