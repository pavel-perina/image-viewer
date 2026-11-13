import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// A label with a whole number to set beside it, as the panels of tools have many of. With
// `automatic`, one below the least stands for "let the program choose" and reads "Auto".
RowLayout {
    id: row

    required property string label
    property int value: 0
    property int from: 0
    property int to: 999
    property bool automatic: false
    // The name the number box is found by.
    property string boxName

    // The user set a value; -1 for Auto.
    signal modified(int value)
    // Enter in the box took its value.
    signal accepted()

    Label {
        Layout.fillWidth: true
        text: row.label
    }
    SpinBox {
        id: box
        objectName: row.boxName
        editable: true
        from: row.automatic ? row.from - 1 : row.from
        to: row.to
        value: row.automatic && row.value < row.from ? from : row.value
        textFromValue: (value) => row.automatic && value < row.from ? "Auto" : String(value)
        valueFromText: (text) => {
            const number = parseInt(text)
            return isNaN(number) ? from : number
        }
        onValueModified: row.modified(row.automatic && value < row.from ? -1 : value)
        Connections {
            target: box.contentItem
            ignoreUnknownSignals: true
            function onAccepted() { row.accepted() }
        }
    }
}
