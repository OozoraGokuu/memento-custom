pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Rectangle {
    id: root
    property int selectedYear: new Date().getFullYear()
    property string selectedDate: ""
    property int refreshToken: 0
    property int currentYear: new Date().getFullYear()
    readonly property var calendarData: {
        const refreshed = root.refreshToken;
        return ImmersionStats.calendar(root.selectedYear);
    }
    readonly property var selectedDay: {
        const date = root.selectedDate || root.calendarData.defaultDate;
        for (const day of root.calendarData.days) {
            if (day.date === date && day.enabled) return day;
        }
        return null;
    }
    readonly property var greens: [MementoPalette.alternateBase, "#205c3b", "#2d8750", "#42b965", "#78df88"]
    readonly property var levels: [qsTr("No playback"), qsTr("Under 15 minutes"),
        qsTr("15–29 minutes"), qsTr("30–59 minutes"), qsTr("60 minutes or more")]

    implicitHeight: content.implicitHeight + 36
    implicitWidth: 600
    radius: 10
    color: MementoPalette.base
    border.color: MementoPalette.border
    onSelectedYearChanged: selectedDate = ""
    onVisibleChanged: if (visible) refresh()

    function duration(seconds) {
        if (seconds < 60) return qsTr("%1 sec").arg(Math.floor(Math.max(0, seconds)));
        const minutes = Math.floor(seconds / 60);
        if (minutes < 60) return qsTr("%1 min").arg(minutes);
        const hours = Math.floor(minutes / 60);
        return minutes % 60 ? qsTr("%1h %2m").arg(hours).arg(minutes % 60) : qsTr("%1h").arg(hours);
    }

    function refresh() {
        const year = new Date().getFullYear();
        if (root.selectedYear === root.currentYear && year !== root.currentYear) {
            root.selectedYear = year;
            root.selectedDate = "";
        }
        root.currentYear = year;
        ++root.refreshToken;
    }

    // Rebuild the grid at most twice a minute while watching; replacing all
    // delegates every playback sample would interrupt hover and keyboard focus.
    Timer {
        interval: 30000
        repeat: true
        running: root.visible
        onTriggered: root.refresh()
    }

    ColumnLayout {
        id: content
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Immersion calendar")
                font.pixelSize: 17
                font.bold: true
                color: MementoPalette.windowText
                Layout.fillWidth: true
            }
            ComboBox {
                id: yearPicker
                Layout.preferredWidth: 106
                model: root.calendarData.years
                currentIndex: root.calendarData.years.indexOf(root.selectedYear)
                Accessible.name: qsTr("Calendar year")
                onActivated: root.selectedYear = Number(currentText)
            }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("%1 watched · %2 active days in %3")
                .arg(root.duration(root.calendarData.activeSeconds))
                .arg(root.calendarData.activeDays).arg(root.calendarData.year)
            color: MementoPalette.placeholderText
            wrapMode: Text.WordWrap
        }

        Flickable {
            id: grid
            readonly property real step: Math.max(16, Math.min(21, (width - 38) / root.calendarData.weeks))
            Layout.fillWidth: true
            Layout.preferredHeight: 30 + 7 * step + 14
            contentWidth: 38 + root.calendarData.weeks * step
            contentHeight: height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

            Repeater {
                model: root.calendarData.months
                delegate: Label {
                    required property var modelData
                    x: 38 + modelData.week * grid.step
                    y: 0
                    text: modelData.label
                    font.pixelSize: 11
                    color: MementoPalette.placeholderText
                }
            }
            Repeater {
                model: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
                delegate: Label {
                    required property int index
                    required property string modelData
                    x: 0
                    y: 25 + index * grid.step
                    width: 34
                    height: grid.step - 3
                    text: index % 2 === 0 ? modelData : ""
                    font.pixelSize: 10
                    verticalAlignment: Text.AlignVCenter
                    color: MementoPalette.placeholderText
                }
            }
            Repeater {
                model: root.calendarData.days
                delegate: Button {
                    id: cell
                    required property int index
                    required property var modelData
                    x: 38 + Math.floor(index / 7) * grid.step
                    y: 25 + (index % 7) * grid.step
                    width: grid.step - 3
                    height: grid.step - 3
                    padding: 0
                    visible: modelData.inYear
                    enabled: modelData.enabled
                    hoverEnabled: true
                    Accessible.name: modelData.label + ": " + root.duration(modelData.seconds)
                    Accessible.description: qsTr("Show this day's viewing activity")
                    onClicked: root.selectedDate = modelData.date
                    onActiveFocusChanged: {
                        if (!activeFocus) return;
                        if (x < grid.contentX) grid.contentX = x;
                        else if (x + width > grid.contentX + grid.width)
                            grid.contentX = x + width - grid.width;
                    }
                    background: Rectangle {
                        radius: 3
                        color: root.greens[cell.modelData.level]
                        opacity: cell.modelData.future ? 0.32 : 1
                        border.width: cell.activeFocus || cell.hovered ||
                            (root.selectedDay && root.selectedDay.date === cell.modelData.date) ? 2 : 1
                        border.color: cell.activeFocus || cell.hovered ||
                            (root.selectedDay && root.selectedDay.date === cell.modelData.date)
                            ? MementoPalette.windowText : MementoPalette.border
                    }
                    contentItem: Item {}
                    ToolTip.visible: hovered || activeFocus
                    ToolTip.delay: 200
                    ToolTip.text: modelData.label + " · " + root.duration(modelData.seconds)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            Label {
                Layout.fillWidth: true
                text: qsTr("Select a day to see what you watched.")
                color: MementoPalette.placeholderText
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Label { text: qsTr("Less"); font.pixelSize: 10; color: MementoPalette.placeholderText }
            Repeater {
                model: 5
                delegate: Rectangle {
                    required property int index
                    Layout.preferredWidth: 13
                    Layout.preferredHeight: 13
                    radius: 3
                    color: root.greens[index]
                    border.color: MementoPalette.border
                    HoverHandler { id: legendHover }
                    ToolTip.visible: legendHover.hovered
                    ToolTip.text: root.levels[index]
                }
            }
            Label { text: qsTr("More"); font.pixelSize: 10; color: MementoPalette.placeholderText }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: MementoPalette.border }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 7
            Label {
                Layout.fillWidth: true
                text: root.selectedDay ? root.selectedDay.label : ""
                font.bold: true
                color: MementoPalette.windowText
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: !root.selectedDay || root.selectedDay.seconds <= 0
                    ? qsTr("No playback recorded on this day.")
                    : qsTr("%1 watched · %2 episodes completed")
                        .arg(root.duration(root.selectedDay.seconds)).arg(root.selectedDay.completedCount)
                color: MementoPalette.placeholderText
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.selectedDay ? root.selectedDay.titles : []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: modelData.title
                        elide: Text.ElideRight
                        color: MementoPalette.windowText
                    }
                    Label { text: root.duration(modelData.seconds); color: MementoPalette.placeholderText }
                }
            }
        }
    }
}
