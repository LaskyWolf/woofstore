import QtQuick
import QtQuick.Shapes

// A turning arc for "loading"; it only runs while it is visible.
Item {
    id: spinner
    property real angle: 0
    readonly property real pen: width / 10
    implicitWidth: 40
    implicitHeight: 40
    Timer { interval: 60; repeat: true; running: spinner.visible; onTriggered: spinner.angle = (spinner.angle + 30) % 360 }
    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: spinner.palette.highlight
            strokeWidth: spinner.pen
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathAngleArc {
                centerX: spinner.width / 2; centerY: spinner.height / 2
                radiusX: spinner.width / 2 - 1.5 * spinner.pen; radiusY: radiusX
                startAngle: spinner.angle
                sweepAngle: 270
            }
        }
    }
}
