import QtQuick
import QtQuick.Controls

// What one picture looks like in a grid or list of thumbnails: the thumbnail, its name under
// it, and the highlight when it is chosen. Whoever uses it adds taps and drags as children.
Item {
    id: cell

    property string name
    property string thumbnail   // an image://thumbnails/ address, from the model
    property bool selected
    property bool current       // framed, as the image shown large, chosen or not
    property bool keysHere: true  // the list has the keys; paler highlight while not
    // The thumbnail, for whoever wants a picture of it, such as a drag.
    readonly property alias thumbnailImage: thumbnailImage

    SelectionBackground {
        anchors.fill: parent
        anchors.margins: 2
        selected: cell.selected
        focused: cell.keysHere
        subtle: true
    }

    Rectangle {
        anchors.fill: parent
        // Both states stay visible: selection outside, the current image just inside.
        anchors.margins: cell.selected ? 5 : 2
        visible: cell.current
        radius: Theme.controlRadius - 1
        color: "transparent"
        border.width: 2
        border.color: Theme.onImageBright
    }

    Item {
        id: thumbnailArea
        anchors.fill: parent
        anchors.margins: 8
        anchors.bottomMargin: 28

        // Made at the size it is shown, one pixel for one, so nothing filters it again.
        // Centring rounds to whole pixels, which keeps that exact.
        Image {
            id: thumbnailImage
            objectName: "thumbnailImage"
            anchors.centerIn: parent
            asynchronous: true
            // From the cell, as the area has no size yet when this starts loading.
            sourceSize: Qt.size(cell.width - 16, cell.width - 16)
            source: cell.thumbnail
        }
    }

    Label {
        anchors.centerIn: thumbnailArea
        visible: thumbnailImage.status === Image.Error
        text: "Cannot read"
    }

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideMiddle
        text: cell.name
    }
}
