import QtQuick

// An app or theme icon: a file path, a URL, or a theme icon name (drawn by the theme).
// An Item around the Image: the picture's own size never feeds back into the size it is asked for.
Item {
    id: box
    property string icon
    property string fallback: "application-x-executable"
    implicitWidth: 40
    implicitHeight: 40
    Image {
        id: image
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        // The provider gets the logical size with the scale, so the theme picks the drawing made for that size, as widgets do
        sourceSize: Qt.size(box.width * Screen.devicePixelRatio, box.height * Screen.devicePixelRatio)
        source: box.icon.startsWith("/") ? "file://" + box.icon
            : box.icon.includes("://") ? box.icon
            : "image://icon/" + (box.icon !== "" ? box.icon : box.fallback) + "|" + box.fallback + "|" + Screen.devicePixelRatio
        // A file that cannot be read shows the fallback
        onStatusChanged: if (status === Image.Error) source = "image://icon/" + box.fallback + "|" + box.fallback + "|" + Screen.devicePixelRatio
    }
}
