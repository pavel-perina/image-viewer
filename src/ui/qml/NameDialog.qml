pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ImageViewer

// Asks for a name: of a file or folder to create or rename, or of a copy to save. The check comes
// from whoever asks, and a name it turns down keeps the dialog open with the reason.
AppDialog {
    id: nameDialog
    objectName: "nameDialog"
    property string initialText
    // Takes the name and returns what is wrong with it, or "" when it was used.
    property var accept: null
    property var cancel: null
    property bool named: false
    width: Math.min(480, parent.width - 32)

    // Cancel, if given, is called when the dialog closes without a name being used.
    function ask(title, text, accept, cancel) {
        nameDialog.title = title
        nameDialog.initialText = text
        nameDialog.accept = accept
        nameDialog.cancel = cancel ?? null
        nameDialog.open()
    }

    function submit() {
        const problem = nameDialog.accept(nameField.text)
        nameProblem.text = problem
        if (!problem) {
            named = true
            nameDialog.close()
        }
    }
    onAboutToShow: named = false
    onClosed: if (!named && cancel) cancel()

    // Closed by submit, because a name that cannot be used keeps the dialog open.
    closePolicy: Popup.CloseOnEscape
    onOpened: {
        nameProblem.text = ""
        nameField.text = initialText
        nameField.forceActiveFocus()
        // Selects the name without its extension, as that is what usually changes.
        const dot = initialText.lastIndexOf(".")
        nameField.select(0, dot > 0 ? dot : initialText.length)
    }

    footer: DialogButtonBox {
        Button {
            text: "OK"
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: nameDialog.submit()
        }
        Button {
            text: "Cancel"
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: nameDialog.close()
        }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        TextField {
            id: nameField
            objectName: "nameField"
            Layout.fillWidth: true
            onAccepted: nameDialog.submit()
        }
        Label {
            id: nameProblem
            objectName: "nameProblem"
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.Wrap
        }
    }
}
