import QtQuick
import QtQuick.Controls
import ImageViewer

// A fuzzy filter for a list of files, typed in a small field that floats over the list's lower
// right corner, away from the matches, which are listed from the top. It takes no room until
// asked for: by a button, Ctrl+F or /, or, where letters are not shortcuts, by typing in the
// list. The list narrows as it is typed (fzf style, with its marks: 'exact, ^start, end$,
// !not). Up, Down and the page keys move in the list meanwhile, Enter opens the current entry,
// and Esc clears the filter and gives the keys back. A filter left in place keeps the field
// shown, so that it is plain why the list is short.
Rectangle {
    id: box

    property alias text: field.text
    // "12 of 311", or empty.
    property string countText
    readonly property bool open: shown || field.text !== ""
    readonly property alias field: field

    // A key that moves in the list: Up, Down, Page Up or Page Down.
    signal moveRequested(int key)
    signal activateRequested()
    // The keys go back to the list.
    signal finished()

    // Shows the field with the keys, adding what was typed in the list to start it.
    function start(typed) {
        shown = true
        if (typed)
            field.insert(field.length, typed)
        field.forceActiveFocus()
    }

    function stop() {
        field.clear()
        shown = false
        finished()
    }

    property bool shown: false

    visible: open
    width: Math.min(Theme.panelWidth, parent ? parent.width - 2 * Theme.panelMargin : Theme.panelWidth)
    height: Theme.controlHeight + 2 * Theme.paneInset
    radius: Theme.controlRadius + Theme.paneInset
    color: Theme.panel
    border.color: Theme.frame

    TextField {
        id: field
        objectName: "filterField"
        anchors.fill: parent
        anchors.margins: Theme.paneInset
        placeholderText: "Filter"
        rightPadding: count.width + 12
        selectByMouse: true

        Keys.onPressed: (event) => {
            switch (event.key) {
            case Qt.Key_Escape:
                box.stop()
                break
            case Qt.Key_Up:
            case Qt.Key_Down:
            case Qt.Key_PageUp:
            case Qt.Key_PageDown:
                box.moveRequested(event.key)
                break
            case Qt.Key_Return:
            case Qt.Key_Enter:
                box.activateRequested()
                break
            default:
                return
            }
            event.accepted = true
        }
        // Left empty, it goes away with the keys.
        onActiveFocusChanged: if (!activeFocus && text === "") box.shown = false
    }

    Label {
        id: count
        objectName: "filterCount"
        anchors.right: field.right
        anchors.rightMargin: 8
        anchors.verticalCenter: field.verticalCenter
        text: field.text !== "" ? box.countText : ""
        color: Theme.textMuted
    }
}
