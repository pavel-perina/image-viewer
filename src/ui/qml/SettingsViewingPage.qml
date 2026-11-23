import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

SettingsPage {
    id: page
    required property SettingsDraft draft

    FileChooser {
        id: profileChooser
        objectName: "settingsProfileChooser"
        title: "Choose Display Profile"
        types: [{label: "ICC profiles", extensions: ["icc", "icm"]}, {label: "All files", extensions: []}]
        onChosen: path => page.draft.chooseProfile(profileChooser.urlOf(path))
    }

    FileChooser {
        id: tablesChooser
        objectName: "settingsTablesChooser"
        title: "Choose the Folder of Colour Tables"
        mode: "folder"
        onChosen: path => page.draft.chooseColorTablesFolder(tablesChooser.urlOf(path))
    }

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.panelSpacing * 2
        Label { text: "Viewing"; font.pointSize: Theme.titleSize }
        Label {
            Layout.fillWidth: true
            text: "Colour and transparency when displaying images."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Label { text: "Transparency"; Layout.preferredWidth: 144 }
            ComboBox {
                id: transparency
                objectName: "transparencyChoice"
                Layout.fillWidth: true
                model: [ {label: "Dark squares", choice: "dark"}, {label: "Light squares", choice: "light"}, {label: "Gray", choice: "plain"} ]
                textRole: "label"
                currentIndex: model.findIndex(item => item.choice === page.draft.transparency)
                onActivated: index => page.draft.transparency = model[index].choice
                Accessible.name: "Transparency background"
            }
        }
        CheckBox {
            objectName: "colorManagedChoice"
            text: "Enable colour management"
            checked: page.draft.colorManaged
            onToggled: page.draft.colorManaged = checked
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.panelSpacing
            rowSpacing: Theme.panelSpacing
            enabled: page.draft.colorManaged
            Label { text: "Display profile"; Layout.preferredWidth: 144; Layout.alignment: Qt.AlignTop }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.panelSpacing
                RadioButton {
                    objectName: "srgbProfileChoice"
                    text: "sRGB display"
                    autoExclusive: false
                    checked: page.draft.displayProfile === ""
                    onClicked: { page.draft.useSrgb(); checked = Qt.binding(() => page.draft.displayProfile === "") }
                }
                RadioButton {
                    objectName: "fileProfileChoice"
                    text: "ICC profile from file"
                    autoExclusive: false
                    checked: page.draft.displayProfile !== ""
                    onClicked: {
                        checked = Qt.binding(() => page.draft.displayProfile !== "")
                        profileChooser.start("", page.draft.displayProfile)
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.panelSpacing
                    TextField {
                        objectName: "displayProfilePath"
                        Layout.fillWidth: true
                        readOnly: true
                        text: page.draft.displayProfile
                        placeholderText: "No custom profile selected"
                        Accessible.name: "Display profile path"
                        ToolTip.visible: hovered && text !== ""
                        ToolTip.text: text
                    }
                    Button { text: "Choose…"; onClicked: profileChooser.start("", page.draft.displayProfile) }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            visible: page.draft.displayProfile !== "" && page.draft.displayProfile === Preferences.displayProfile && !ColorSettings.hasDisplayProfile
            text: "The saved profile is unavailable. Images currently use sRGB."
            color: Theme.warning
            wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.paneFrame }
        // The tables Adjust offers by name, from this folder and its subfolders.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Label { text: "Colour tables"; Layout.preferredWidth: 144 }
            TextField {
                objectName: "colorTablesFolderPath"
                Layout.fillWidth: true
                readOnly: true
                text: page.draft.colorTablesFolder
                placeholderText: "No folder chosen"
                Accessible.name: "Folder of colour tables"
                ToolTip.visible: hovered && text !== ""
                ToolTip.text: text
            }
            Button {
                objectName: "colorTablesFolderChoose"
                text: "Choose…"
                onClicked: tablesChooser.start(page.draft.colorTablesFolder, "")
            }
            Button {
                objectName: "colorTablesFolderClear"
                text: "Clear"
                enabled: page.draft.colorTablesFolder !== ""
                onClicked: page.draft.chooseColorTablesFolder("")
            }
        }
        Label {
            Layout.fillWidth: true
            text: "Files ending in .cube, in the folder and its subfolders, are listed in Adjust."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.paneFrame }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Label { text: "Output depth"; Layout.preferredWidth: 144 }
            ComboBox {
                objectName: "outputBitsChoice"
                model: ["8 bits / channel", "10 bits / channel"]
                currentIndex: page.draft.outputBits === 10 ? 1 : 0
                onActivated: index => page.draft.outputBits = index === 1 ? 10 : 8
                Accessible.name: "Output depth after restart"
            }
            Label { text: "After restart"; color: Theme.textMuted; Layout.fillWidth: true }
        }
        Label {
            objectName: "actualOutputLabel"
            text: "Current output: " + DisplayOutput.bits + " bits per channel"
            color: Theme.textMuted
        }
        Label {
            Layout.fillWidth: true
            text: "10-bit output also depends on the display and graphics driver."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "outputOverrideLabel"
            Layout.fillWidth: true
            visible: DisplayOutput.environmentOverride
            text: "IV_OUTPUT_BITS currently requests " + DisplayOutput.requestedBits + " bits per channel and overrides this preference."
            color: Theme.warning
            wrapMode: Text.WordWrap
        }
    }
}
