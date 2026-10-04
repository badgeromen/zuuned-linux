import QtQuick
import Qt5Compat.GraphicalEffects

// Animated splash — port of SplashView.swift. Graffiti text with an
// internal scan effect: white text → pink scan line sweeps left to
// right → leaves the orange-to-pink gradient behind; "for zune"
// subtitle fades in near the end.
//
// Contract: fills its parent, paints Theme.bg itself, emits finished()
// once the animation is done AND minimumMs has elapsed. The container
// owns the fade-out.
Item {
    id: root

    property real minimumMs: 1500
    signal finished()

    property bool _animDone: false
    property bool _minElapsed: false
    function _maybeFinish() {
        if (_animDone && _minElapsed)
            finished()
    }

    // Animated state (mirrors the Swift @State)
    property real revealWidth: 0
    property real scanOffset: -200

    readonly property string _displayFamily: Theme.displayFamily

    Rectangle {
        anchors.fill: parent
        color: Theme.bg
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spaceLg

        // ── Graffiti text with scan reveal ──
        // The mac composites the three text layers at NATURAL size and
        // clips the whole stack to 100pt afterwards. Every effect item
        // here must be text-sized — Qt scales a source texture to the
        // effect's bounds, so filling the 100px frame squished the
        // gradient/scan layers vertically against the base text.
        Item {
            id: stack
            width: baseText.implicitWidth
            height: 100
            clip: true
            anchors.horizontalCenter: parent.horizontalCenter

            Item {
                id: textStack
                anchors.centerIn: parent
                width: baseText.implicitWidth
                height: baseText.implicitHeight

                // Layer 1: white base — visible ahead of the scan
                Text {
                    id: baseText
                    text: "zuuned"
                    font.family: root._displayFamily
                    font.pixelSize: 96
                    font.letterSpacing: 96 * 0.08
                    color: Qt.rgba(1, 1, 1, 0.7)
                }

                // Layer 2: gradient text revealed by a left-anchored
                // mask. Outer clip Item animates; inner stays full-size
                // so the gradient span never changes.
                Item {
                    height: textStack.height
                    width: Math.max(0, root.revealWidth)
                    clip: true

                    Item {
                        width: textStack.width
                        height: textStack.height

                        Text {
                            id: gradientLabel
                            text: "zuuned"
                            font.family: root._displayFamily
                            font.pixelSize: 96
                            font.letterSpacing: 96 * 0.08
                            visible: false
                        }
                        LinearGradient {
                            anchors.fill: parent
                            start: Qt.point(0, 0)
                            end: Qt.point(textStack.width, 0)
                            source: gradientLabel
                            gradient: Gradient {
                                GradientStop { position: 0.0; color: Theme.orange }
                                GradientStop { position: 1.0; color: Theme.pink }
                            }
                        }
                    }
                }

                // Layer 3: pink scan-line glow — the leading edge
                Text {
                    id: scanText
                    text: "zuuned"
                    font.family: root._displayFamily
                    font.pixelSize: 96
                    font.letterSpacing: 96 * 0.08
                    color: Theme.pink
                    visible: false
                }
                Item {
                    id: scanMask
                    anchors.fill: parent
                    visible: false
                    // Blur the full-size mask, not the 30px rect — a
                    // layer clamps its effect to its own bounds.
                    layer.enabled: true
                    layer.effect: FastBlur { radius: 16 }
                    Rectangle {
                        x: root.scanOffset
                        width: 30
                        height: parent.height
                        color: "#ffffff"
                    }
                }
                OpacityMask {
                    anchors.fill: parent
                    source: scanText
                    maskSource: scanMask
                }
            }
        }

        // (no tagline under the wordmark)
        Text {
            id: subtitle
            anchors.horizontalCenter: parent.horizontalCenter
            text: ""
            font.pixelSize: 11
            font.weight: Font.Light
            color: Theme.textDim
            opacity: 0
        }
    }

    // ── Animation sequence (timings from SplashView.swift) ──
    ParallelAnimation {
        id: scanAnimation

        // Reveal mask: 0 → text width + 50, 1.8s easeInOut
        NumberAnimation {
            target: root
            property: "revealWidth"
            to: baseText.implicitWidth + 50
            duration: 1800
            easing.type: Easing.InOutQuad
        }
        // Scan line: −200 → text width + 20, 1.8s easeInOut
        NumberAnimation {
            target: root
            property: "scanOffset"
            to: baseText.implicitWidth + 20
            duration: 1800
            easing.type: Easing.InOutQuad
        }
        // Subtitle: 0.5s easeIn after a 1.2s delay
        SequentialAnimation {
            PauseAnimation { duration: 1200 }
            NumberAnimation {
                target: subtitle
                property: "opacity"
                to: 1.0
                duration: 500
                easing.type: Easing.InQuad
            }
        }

        onFinished: {
            root._animDone = true
            root._maybeFinish()
        }
    }

    Timer {
        id: minTimer
        interval: root.minimumMs
        onTriggered: {
            root._minElapsed = true
            root._maybeFinish()
        }
    }

    Component.onCompleted: {
        minTimer.start()
        scanAnimation.start()
    }
}
