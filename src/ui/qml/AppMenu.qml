import QtQuick
import QtQuick.Controls

// A menu as wide as its widest item, so that names and shortcuts never run into each other. The
// style would keep one width for every menu.
Menu {
    id: menu

    width: {
        let widest = implicitWidth
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item)
                widest = Math.max(widest, item.implicitWidth + leftPadding + rightPadding)
        }
        return widest
    }
}
