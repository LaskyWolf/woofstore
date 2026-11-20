import QtQuick
import QtQuick.Shapes

// The magnifier inside a search field: declare `SearchIcon {}` in the TextField, with leftPadding: 35.
// Breeze's 16 px edit-find, drawn here: the icon provider picks the thinner 22 px one at fractional scales.
Shape {
    x: 10
    anchors.verticalCenter: parent.verticalCenter
    width: 16
    height: 16
    preferredRendererType: Shape.CurveRenderer
    ShapePath {
        strokeColor: "transparent"
        fillColor: palette.windowText
        PathSvg { path: "M6.5 2C4.007 2 2 4.01 2 6.5C2 8.993 4.01 11 6.5 11C7.5636 11 8.5263 10.6188 9.2949 10.0059L13.293 14.0039L14 13.2969L10.002 9.2988C10.6176 8.529 11 7.5653 11 6.5C11 4.007 8.99 2 6.5 2ZM6.5 3C8.439 3 10 4.561 10 6.5C10 8.439 8.439 10 6.5 10C4.561 10 3 8.439 3 6.5C3 4.561 4.561 3 6.5 3Z" }
    }
}
