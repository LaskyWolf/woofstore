import QtQuick
import QtQuick.Shapes

// Lucide icons (ISC license) on their 24 x 24 grid: one stroke, the theme colour.
// "arch" is the filled Arch logo, "aur" the same logo as an outline (a user-built layer on top of Arch).
Item {
    id: icon
    property string name
    property color color: palette.windowText
    property real weight: 2
    implicitWidth: 22
    implicitHeight: 22

    readonly property bool logo: name === "arch" || name === "aur"
    readonly property string archPath: "m127.98 12.07c-10.316 25.309-16.543 41.855-28.031 66.41 7.043 7.4609 15.691 16.156 29.734 25.977-15.098-6.207-25.395-12.445-33.094-18.918-14.703 30.68-37.742 74.391-84.492 158.39 36.746-21.219 65.23-34.293 91.773-39.289-1.1406-4.8945-1.7852-10.195-1.7422-15.734l0.042969-1.1719c0.58203-23.551 12.828-41.645 27.336-40.418 14.508 1.2266 25.781 21.316 25.199 44.867-0.10938 4.4219-0.60938 8.6914-1.4805 12.641 26.258 5.1328 54.438 18.18 90.684 39.105-7.1484-13.156-13.527-25.016-19.621-36.316-9.5938-7.4336-19.605-17.117-40.023-27.594 14.035 3.6406 24.082 7.8516 31.914 12.555-61.941-115.32-66.957-130.66-88.199-180.5z"
    readonly property var paths: ({
        "calendar": "M8 2V6M16 2V6M3 10H21M5 4H19A2 2 0 0 1 21 6V20A2 2 0 0 1 19 22H5A2 2 0 0 1 3 20V6A2 2 0 0 1 5 4Z",
        "compass": "M22 12A10 10 0 1 1 2 12A10 10 0 1 1 22 12ZM16.24 7.76L14.436 13.171L13.171 14.436L7.76 16.24L9.564 10.829L10.829 9.564Z",
        "list": "M4 3H9A1 1 0 0 1 10 4V9A1 1 0 0 1 9 10H4A1 1 0 0 1 3 9V4A1 1 0 0 1 4 3ZM4 14H9A1 1 0 0 1 10 15V20A1 1 0 0 1 9 21H4A1 1 0 0 1 3 20V15A1 1 0 0 1 4 14ZM14 4H21M14 9H21M14 15H21M14 20H21",
        "refresh": "M3 12A9 9 0 0 1 18.58 5.86L21 8M21 3V8H16M21 12A9 9 0 0 1 5.42 18.14L3 16M3 21V16H8",
        "package": "M12 2.3L20.7 7.2V16.8L12 21.7L3.3 16.8V7.2ZM3.3 7.2L12 12L20.7 7.2M12 12V21.7",
        "app-window": "M4 4H20A2 2 0 0 1 22 6V22A2 2 0 0 1 20 24H4A2 2 0 0 1 2 22V6A2 2 0 0 1 4 4ZM2 9H22M6 6.5H6.1M9 6.5H9.1",
        "trophy": "M6 9H4.5A2.5 2.5 0 0 1 4.5 4H6M18 9H19.5A2.5 2.5 0 0 0 19.5 4H18M4 22H20M10 14.7V17L7 22M14 14.7V17L17 22M6 2H18V9A6 6 0 0 1 6 9Z",
        "external-link": "M15 3H21V9M10 14L21 3M18 13V19A2 2 0 0 1 16 21H5A2 2 0 0 1 3 19V8A2 2 0 0 1 5 6H11",
        "shield": "M12 2.5L20 5.5V12C20 17 16 20.5 12 22C8 20.5 4 17 4 12V5.5ZM9 12L11 14L15 10",
        "gamepad": "M8.5 6H15.5A6 6 0 0 1 15.5 18H8.5A6 6 0 0 1 8.5 6ZM7 10V14M5 12H9M15 11H15.01M18 13H18.01",
        "monitor": "M4 3H20A2 2 0 0 1 22 5V15A2 2 0 0 1 20 17H4A2 2 0 0 1 2 15V5A2 2 0 0 1 4 3ZM8 21H16M12 17V21",
        "cpu": "M6 4H18A2 2 0 0 1 20 6V18A2 2 0 0 1 18 20H6A2 2 0 0 1 4 18V6A2 2 0 0 1 6 4ZM10 9H14A1 1 0 0 1 15 10V14A1 1 0 0 1 14 15H10A1 1 0 0 1 9 14V10A1 1 0 0 1 10 9ZM9 1V4M15 1V4M9 20V23M15 20V23M1 9H4M1 15H4M20 9H23M20 15H23",
        "terminal": "M4 17L10 11L4 5M12 19H20",
        "grid": "M4 3H9A1 1 0 0 1 10 4V9A1 1 0 0 1 9 10H4A1 1 0 0 1 3 9V4A1 1 0 0 1 4 3ZM15 3H20A1 1 0 0 1 21 4V9A1 1 0 0 1 20 10H15A1 1 0 0 1 14 9V4A1 1 0 0 1 15 3ZM4 14H9A1 1 0 0 1 10 15V20A1 1 0 0 1 9 21H4A1 1 0 0 1 3 20V15A1 1 0 0 1 4 14ZM15 14H20A1 1 0 0 1 21 15V20A1 1 0 0 1 20 21H15A1 1 0 0 1 14 20V15A1 1 0 0 1 15 14Z",
        "circle-check": "M22 12A10 10 0 1 1 2 12A10 10 0 1 1 22 12ZM8 12L11 15L16 9",
        "search-x": "M19 11A8 8 0 1 1 3 11A8 8 0 1 1 19 11ZM21 21L16.65 16.65M13.5 8.5L8.5 13.5M8.5 8.5L13.5 13.5",
        "circle-x": "M22 12A10 10 0 1 1 2 12A10 10 0 1 1 22 12ZM15 9L9 15M9 9L15 15",
        "newspaper": "M8 2H20A2 2 0 0 1 22 4V20A2 2 0 0 1 20 22H8A2 2 0 0 1 6 20V4A2 2 0 0 1 8 2ZM6 9H4L2 11V20L4 22H6M18 14H10M15 18H10M10 6H18V10H10Z",
        "trending": "M22 7L13.5 15.5L8.5 10.5L2 17M16 7H22V13",
        "star": "M12 2.8L14.7 9.08L21.51 9.71L16.37 14.22L17.88 20.89L12 17.4L6.12 20.89L7.63 14.22L2.49 9.71L9.3 9.08Z",
        "sparkle": "M12 2.5L14.3 9.7L21.5 12L14.3 14.3L12 21.5L9.7 14.3L2.5 12L9.7 9.7Z",
        "panel-close": "M5 3H19A2 2 0 0 1 21 5V19A2 2 0 0 1 19 21H5A2 2 0 0 1 3 19V5A2 2 0 0 1 5 3ZM9 3V21M16 15L13 12L16 9",
        "panel-open": "M5 3H19A2 2 0 0 1 21 5V19A2 2 0 0 1 19 21H5A2 2 0 0 1 3 19V5A2 2 0 0 1 5 3ZM9 3V21M14 9L17 12L14 15"
    })

    Shape {
        width: icon.logo ? 256 : 24
        height: width
        scale: icon.width / width
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: icon.name === "arch" ? "transparent" : icon.color
            strokeWidth: icon.logo ? 14 : icon.weight
            fillColor: icon.name === "arch" ? icon.color : "transparent"
            fillRule: ShapePath.OddEvenFill
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.logo ? icon.archPath : (icon.paths[icon.name] ?? "") }
        }
    }
}
