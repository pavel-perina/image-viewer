import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// How the program acts, as a matter of habit rather than of looks.
SettingsPage {
    id: page
    required property SettingsDraft draft

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.panelSpacing * 2
        Label { text: "Behaviour"; font.pointSize: Theme.titleSize }
        Label {
            Layout.fillWidth: true
            text: "How the program responds, to suit your habits."
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.panelSpacing
            Label { text: "Esc in the viewer"; Layout.preferredWidth: 144 }
            ComboBox {
                objectName: "escapeChoice"
                Layout.fillWidth: true
                model: [ {label: "Depends on how it was started", choice: "auto"},
                         {label: "Goes back to the browser", choice: "browser"},
                         {label: "Closes the window", choice: "close"} ]
                textRole: "label"
                currentIndex: model.findIndex(item => item.choice === page.draft.escapeAction)
                onActivated: index => page.draft.escapeAction = model[index].choice
                Accessible.name: "Esc in the viewer"
            }
        }
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 144 + Theme.panelSpacing
            text: "Started on a picture, as from a file manager, Esc closes the window until you go to the browser; "
                  + "otherwise it goes back to the browser. Esc first ends a tool or full screen, and closing asks about unsaved edits."
            visible: page.draft.escapeAction === "auto"
            color: Theme.textMuted
            wrapMode: Text.WordWrap
        }
    }
}
