import QtQuick

// Port of MusicRows.swift MusicBreadcrumbBar:
// "‹ back   Artists / Daft Punk / Discovery" — every segment clickable
// except the last; bottom border underneath.
Item {
    id: bar

    // crumbs[0] is the pivot root title; the rest are drill titles.
    property var crumbs: []
    // Left inset — hosts pass the page spine so the trail lines up
    // with the band and the content frame
    property real inset: Theme.spaceXxxl

    signal backClicked()
    // depth 0 = pivot root (pop everything), depth n = keep n drills.
    signal crumbClicked(int depth)

    implicitHeight: rowContent.implicitHeight + Theme.spaceSm * 2

    Row {
        id: rowContent
        anchors.left: parent.left
        anchors.leftMargin: bar.inset
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spaceXs

        Rectangle {
            id: backButton
            anchors.verticalCenter: parent.verticalCenter
            width: backLabel.implicitWidth + Theme.spaceMd * 2
            height: backLabel.implicitHeight + Theme.spaceXs * 2
            radius: Theme.radiusMd
            color: backArea.containsMouse ? Theme.cardHover : "transparent"
            border.width: 1
            border.color: Theme.borderLight

            Text {
                id: backLabel
                anchors.centerIn: parent
                text: "‹ back"
                font.pixelSize: 14
                font.weight: Font.Light
                color: Theme.textSecondary
            }
            MouseArea {
                id: backArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: bar.backClicked()
            }
        }

        Repeater {
            model: bar.crumbs

            Row {
                id: crumbEntry
                required property string modelData
                required property int index
                readonly property bool isLast: index === bar.crumbs.length - 1

                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spaceXs

                Text {
                    visible: crumbEntry.index > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: "/"
                    font.pixelSize: 11
                    font.weight: Font.Light
                    color: Theme.textDim
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: crumbEntry.modelData
                    font.pixelSize: 14
                    font.weight: Font.Light
                    color: crumbEntry.isLast ? Theme.textPrimary
                          : (crumbArea.containsMouse ? Theme.textPrimary : Theme.textSecondary)

                    MouseArea {
                        id: crumbArea
                        anchors.fill: parent
                        enabled: !crumbEntry.isLast
                        hoverEnabled: !crumbEntry.isLast
                        cursorShape: crumbEntry.isLast ? Qt.ArrowCursor : Qt.PointingHandCursor
                        onClicked: bar.crumbClicked(crumbEntry.index)
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }
}
