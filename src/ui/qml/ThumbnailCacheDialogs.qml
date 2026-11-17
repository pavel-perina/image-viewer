import QtQuick
import QtQuick.Controls
import ImageViewer

// One clear workflow, usable from either window. Owner-tagged replies prevent a cache
// measurement or a request in another window from opening this window's confirmation.
Item {
    id: controller
    property bool ownsClear: false
    function requestClear() { ThumbnailStore.requestClear(controller) }

    Connections {
        target: ThumbnailStore
        function onClearRequested(owner, available, count, bytes) {
            if (owner !== controller) return
            if (!available) {
                result.kind = "error"
                result.text = "The thumbnail cache cannot be opened, perhaps because another copy of the program is using it."
                result.open()
            } else if (count === 0) {
                result.kind = "info"
                result.text = "There are no stored thumbnails."
                result.open()
            } else {
                confirm.text = "Clear the thumbnail cache (" + ThumbnailStore.usageText + ")? Thumbnails are rebuilt when their folders are shown. Cancel in Settings cannot restore a cleared cache."
                confirm.open()
            }
        }
        function onCleared(done) {
            if (!controller.ownsClear) return
            controller.ownsClear = false
            if (!done) {
                result.kind = "error"
                result.text = "The thumbnails could not be removed."
                result.open()
            }
        }
    }
    MessageBox {
        id: confirm
        objectName: "clearCacheDialog"
        kind: "question"
        title: "Clear Thumbnail Cache"
        standardButtons: Dialog.Yes | Dialog.No
        focusedButton: Dialog.No
        onAccepted: controller.ownsClear = ThumbnailStore.clear()
    }
    MessageBox {
        id: result
        objectName: "clearCacheResult"
        title: "Clear Thumbnail Cache"
        standardButtons: Dialog.Ok
    }
}
