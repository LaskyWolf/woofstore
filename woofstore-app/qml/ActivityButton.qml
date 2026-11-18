import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

// The Activity control at the bottom of the sidebar: a calendar icon, the label and a 3 px progress line
// when the sidebar is open, a progress arc around the icon when it is collapsed.
AbstractButton {
    id: button
    property bool compact: false
    property bool busy: false
    property bool indeterminate: false
    property bool attention: false   // Yellow dot: something needs the person
    property int progress: 100
    property int result: 0           // Last package operation not yet looked at: 0 none, 1 succeeded, 2 failed
    readonly property real reveal: Math.max(0, Math.min(1, (width - 84) / 48))
    property real grow: 1

    implicitHeight: 48
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: "Show activity"
    ToolTip.visible: hovered
    ToolTip.delay: Application.styleHints.mousePressAndHoldInterval
    ToolTip.text: busy && indeterminate ? "Package activity running · progress unavailable" : "Show activity"
    HoverHandler { cursorShape: Qt.PointingHandCursor }

    // The check or cross springs in when a package operation ends
    onResultChanged: if (result) pop.restart()
    NumberAnimation { id: pop; target: button; property: "grow"; from: 0; to: 1; duration: 260; easing.type: Easing.OutBack }

    background: Rectangle {
        radius: 8
        color: button.hovered || button.activeFocus ? button.palette.button : "transparent"
        Rectangle {
            visible: button.activeFocus
            anchors.fill: parent
            anchors.margins: 1
            radius: 8
            color: "transparent"
            border.color: button.palette.highlight
        }
    }
    contentItem: Item {
        Lucide {
            id: calendar
            x: 20 - 11
            y: 9
            width: 22
            height: 22
            name: "calendar"
            color: button.palette.windowText
        }
        Shape {  // The mark of the last result, inside the calendar
            visible: button.result > 0
            x: calendar.x
            y: calendar.y
            width: 24
            height: 24
            scale: 22 / 24
            transformOrigin: Item.TopLeft
            preferredRendererType: Shape.CurveRenderer
            transform: Scale { origin.x: 12; origin.y: 16; xScale: button.grow; yScale: button.grow }
            ShapePath {
                strokeColor: button.result === 1 ? "#27ae60" : "#da4453"
                strokeWidth: 2.4
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg { path: button.result === 1 ? "M9 16L11 18L15 14" : "M14 14L10 18M10 14L14 18" }
            }
        }
        Shape {  // Progress arc around the icon while the sidebar is collapsed
            visible: button.busy && button.compact
            x: 20 - 18
            y: 2
            width: 36
            height: 36
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: button.palette.highlight
                strokeWidth: 2
                fillColor: "transparent"
                PathAngleArc {
                    centerX: 18; centerY: 18; radiusX: 18; radiusY: 18
                    startAngle: -90
                    sweepAngle: button.indeterminate ? 240 : button.progress * 360 / 100
                }
            }
        }
        Rectangle {
            visible: button.attention
            x: 20 + 10 - 6
            y: 11 - 6
            width: 12
            height: 12
            radius: 6
            color: "#f5c211"
            border.width: 2
            border.color: button.palette.window
        }
        Text {
            visible: button.reveal > 0
            x: 44 - (1 - button.reveal) * 10
            y: 5
            width: button.width - 48
            height: 27
            verticalAlignment: Text.AlignVCenter
            text: "Activity"
            color: button.palette.windowText
            opacity: button.reveal
        }
        Rectangle {  // Track and progress line
            visible: button.reveal > 0
            opacity: button.reveal
            x: 12
            y: 39
            width: button.width - 24
            height: 3
            radius: 1.5
            color: button.palette.mid
            Rectangle {
                width: parent.width * (button.indeterminate ? 0.3 : button.progress / 100)
                height: 3
                radius: 1.5
                color: button.palette.highlight
            }
        }
    }
}
