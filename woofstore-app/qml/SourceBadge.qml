import QtQuick

// Colour and glyph that identify where an app comes from.
Rectangle {
    id: badge
    property string source
    readonly property var look: source === "Flatpak" ? ["#3584e4", "package"]
        : source === "Arch" ? ["#1c9fb4", "arch"]
        : source === "AUR" || source === "AUR / local" ? ["#e5a50a", "aur"]
        : source === "AppImage" ? ["#9141ac", "app-window"]
        : source === "WoofStore" ? ["#26a269", "sparkle"]   // The store itself, updated from its GitHub releases
        : ["#6c757d", "grid"]
    implicitWidth: 28
    implicitHeight: 28
    radius: width * 0.25
    color: look[0]
    Lucide {
        x: badge.width * 0.2
        y: badge.width * 0.2
        width: badge.width * 0.6
        height: width
        name: badge.look[1]
        color: "white"
    }
}
