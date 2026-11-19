import QtCore
import QtQuick
import QtQuick.Controls
import ImageViewer

// A dialog in a window of its own, which the system decorates, moves and resizes, and which may
// leave the main window: for the large ones, such as the file chooser. Short questions stay in
// the main window, as an AppDialog. It is modal to the window it belongs to, which is the one
// it is declared in, has no taskbar entry of its own, and keeps its size from one time to the
// next. Escape closes it, unless what has the keys takes Escape itself (a path being edited).
//
// It keeps the calls an AppDialog has, open() and close(), and `opened` while shown; its
// content goes inside, with `padding` round it.
Window {
    id: dialog

    // Where its size is kept, a name of its own per dialog; empty, it is not kept.
    property string sizeKey
    // The size the first time, made smaller to fit the screen.
    property int initialWidth: 900
    property int initialHeight: 640
    property int padding: 16
    readonly property bool opened: visible

    default property alias content: body.data

    flags: Qt.Dialog | Qt.WindowTitleHint | Qt.WindowCloseButtonHint
    modality: Qt.WindowModal
    color: Theme.window
    minimumWidth: 480
    minimumHeight: 320

    Settings {
        id: kept
        category: "DialogSizes"
        property var sizes: ({})
    }

    function open() {
        const size = dialog.sizeKey ? kept.sizes[dialog.sizeKey] : undefined
        const room = Screen.desktopAvailableWidth > 0 ? Qt.size(Screen.desktopAvailableWidth, Screen.desktopAvailableHeight)
                                                      : Qt.size(initialWidth, initialHeight)
        width = Math.max(minimumWidth, Math.min(size ? size.width : initialWidth, room.width))
        height = Math.max(minimumHeight, Math.min(size ? size.height : initialHeight, room.height))
        // Over the middle of its window, where the system lets a program place one; on Wayland
        // the compositor does so itself.
        if (transientParent) {
            x = transientParent.x + Math.round((transientParent.width - width) / 2)
            y = transientParent.y + Math.round((transientParent.height - height) / 2)
        }
        body.forceActiveFocus()
        show()
        requestActivate()
    }

    onVisibleChanged: {
        if (!visible && sizeKey) {
            const sizes = Object.assign({}, kept.sizes)
            sizes[sizeKey] = { width: width, height: height }
            kept.sizes = sizes
        }
    }

    // Qt counts the window it belongs to as active while this one has the keys, so that window's
    // shortcuts and menu keys would act from here too: Escape would leave the image under the
    // save dialog. A modal popup over it, invisible, blocks them as long as this is shown.
    Popup {
        objectName: "keyBlocker"
        // Never over this window itself, which a popup declared here falls back to.
        readonly property Item owner: dialog.transientParent ? dialog.transientParent.contentItem : null
        parent: owner
        visible: dialog.visible && owner !== null && parent === owner
        modal: true
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        width: 0
        height: 0
        padding: 0
        background: null
    }

    FocusScope {
        id: body
        anchors.fill: parent
        anchors.margins: dialog.padding
        focus: true
        Keys.onEscapePressed: dialog.close()
    }
}
