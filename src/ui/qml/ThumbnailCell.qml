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

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        color: !cell.selected ? "transparent" : cell.keysHere ? palette.highlight : Theme.withAlpha(palette.highlight, 0.5)
        border.width: cell.current ? 2 : 0
        border.color: palette.highlightedText
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
