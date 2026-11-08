pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Page {
    id: root
    property int rangeDays: 7
    readonly property int revision: ImmersionStats.revision
    readonly property var stats: {
        const updated = root.revision;
        return ImmersionStats.summary(root.rangeDays);
    }
    readonly property var chart: root.stats.daily || []
    readonly property real chartMaximum: {
        let maximum = 60;
        for (const day of root.chart) maximum = Math.max(maximum, day.seconds);
        return maximum;
    }

    function duration(seconds) {
        if (seconds < 60) return qsTr("%1 sec").arg(Math.floor(Math.max(0, seconds)));
        const minutes = Math.floor(seconds / 60);
        if (minutes < 60) return qsTr("%1 min").arg(minutes);
        const hours = Math.floor(minutes / 60);
        const remainder = minutes % 60;
        return remainder ? qsTr("%1h %2m").arg(hours).arg(remainder) : qsTr("%1h").arg(hours);
    }

    background: Rectangle { color: MementoPalette.window }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 18

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 24
                spacing: 12

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: qsTr("Immersion statistics")
                        font.pixelSize: 25
                        font.bold: true
                        color: MementoPalette.windowText
                    }
                    Label {
                        text: qsTr("Your actual playback activity, recorded on this device")
                        color: MementoPalette.placeholderText
                    }
                }
                ButtonGroup { id: rangeGroup }
                Repeater {
                    model: [
                        { label: qsTr("7 days"), days: 7 },
                        { label: qsTr("30 days"), days: 30 },
                        { label: qsTr("All time"), days: 0 }
                    ]
                    delegate: Button {
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: root.rangeDays === modelData.days
                        ButtonGroup.group: rangeGroup
                        onClicked: root.rangeDays = modelData.days
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                spacing: 12
                Repeater {
                    model: [
                        { label: qsTr("Active watching"), value: root.duration(root.stats.activeSeconds), detail: qsTr("Time media advanced") },
                        { label: qsTr("Days immersed"), value: String(root.stats.activeDays), detail: qsTr("At least one minute") },
                        { label: qsTr("Sessions"), value: String(root.stats.sessionCount), detail: qsTr("At least 30 seconds") },
                        { label: qsTr("Episodes completed"), value: String(root.stats.completedCount), detail: qsTr("Reached 90% in playback") }
                    ]
                    delegate: Rectangle {
                        id: card
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        radius: 10
                        color: MementoPalette.base
                        border.color: MementoPalette.border
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 2
                            Label { text: card.modelData.label; color: MementoPalette.placeholderText; font.pixelSize: 12 }
                            Label { text: card.modelData.value; color: MementoPalette.windowText; font.pixelSize: 28; font.bold: true }
                            Label { text: card.modelData.detail; color: MementoPalette.placeholderText; font.pixelSize: 11 }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.preferredHeight: 258
                radius: 10
                color: MementoPalette.base
                border.color: MementoPalette.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 10
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: qsTr("Viewing activity"); font.pixelSize: 17; font.bold: true; color: MementoPalette.windowText; Layout.fillWidth: true }
                        Label { text: root.rangeDays === 0 ? qsTr("Last 30 days") : qsTr("Daily active time"); color: MementoPalette.placeholderText }
                    }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Row {
                            id: barRow
                            anchors.fill: parent
                            spacing: root.chart.length > 7 ? 6 : 14
                            Repeater {
                                model: root.chart
                                delegate: Item {
                                    required property var modelData
                                    required property int index
                                    width: (barRow.width - (root.chart.length - 1) * barRow.spacing) / Math.max(1, root.chart.length)
                                    height: barRow.height
                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: dayLabel.top
                                        anchors.bottomMargin: 6
                                        height: Math.max(3, (parent.height - 30) * modelData.seconds / root.chartMaximum)
                                        radius: 3
                                        color: modelData.seconds > 0 ? MementoPalette.highlight : MementoPalette.mid
                                        opacity: modelData.seconds > 0 ? 0.9 : 0.35
                                        ToolTip.visible: hover.hovered
                                        ToolTip.text: modelData.date + " · " + root.duration(modelData.seconds)
                                        HoverHandler { id: hover }
                                    }
                                    Label {
                                        id: dayLabel
                                        anchors.bottom: parent.bottom
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.chart.length <= 7 || index % 5 === 0 ? modelData.label : ""
                                        font.pixelSize: 10
                                        color: MementoPalette.placeholderText
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 24
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 245
                    radius: 10
                    color: MementoPalette.base
                    border.color: MementoPalette.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10
                        Label { text: qsTr("Most watched"); font.pixelSize: 17; font.bold: true; color: MementoPalette.windowText }
                        Label {
                            visible: root.stats.titles.length === 0
                            text: qsTr("Titles appear here after you watch them.")
                            color: MementoPalette.placeholderText
                        }
                        Repeater {
                            model: root.stats.titles
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 3
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: modelData.title; elide: Text.ElideRight; Layout.fillWidth: true; color: MementoPalette.windowText }
                                    Label { text: root.duration(modelData.seconds); color: MementoPalette.placeholderText }
                                }
                                ProgressBar { Layout.fillWidth: true; from: 0; to: 1; value: modelData.fraction }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 245
                    radius: 10
                    color: MementoPalette.base
                    border.color: MementoPalette.border
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        spacing: 10
                        Label { text: qsTr("Recent sessions"); font.pixelSize: 17; font.bold: true; color: MementoPalette.windowText }
                        Label {
                            visible: root.stats.sessions.length === 0
                            text: qsTr("Sessions appear after 30 seconds of playback.")
                            color: MementoPalette.placeholderText
                        }
                        Repeater {
                            model: root.stats.sessions
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Label { text: modelData.title; elide: Text.ElideRight; Layout.fillWidth: true; color: MementoPalette.windowText }
                                    Label { text: modelData.started; color: MementoPalette.placeholderText; font.pixelSize: 11 }
                                }
                                Label { text: root.duration(modelData.seconds); color: MementoPalette.windowText }
                            }
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.bottomMargin: 20
                text: qsTr("Tracking starts with this update. Pauses, loading, buffering and seeks are excluded from active time.")
                color: MementoPalette.placeholderText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
}
