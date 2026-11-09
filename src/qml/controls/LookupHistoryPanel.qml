pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Rectangle {
    id: root

    property int rangeDays: 7
    property int page: 0
    readonly property int pageSize: 10
    readonly property int revision: LookupHistory.revision
    readonly property var results: {
        const updated = root.revision;
        return LookupHistory.query(searchField.text.trim(), root.rangeDays,
                                   root.page * root.pageSize, root.pageSize);
    }
    readonly property var entries: root.results.entries || []
    readonly property int total: root.results.total || 0
    readonly property int pageCount: Math.max(1, Math.ceil(root.total / root.pageSize))
    readonly property int totalSaved: {
        const updated = root.revision;
        return LookupHistory.query("", 0, 0, 1).total || 0;
    }

    signal lookupRequested(string word)
    signal sceneRequested(string path, real position)

    function timestamp(value) {
        const date = new Date(value);
        return isNaN(date.getTime()) ? "" : Qt.formatDateTime(date, Qt.DefaultLocaleShortDate);
    }

    function positionLabel(value) {
        const seconds = Math.floor(Math.max(0, Number(value) || 0));
        const hours = Math.floor(seconds / 3600);
        const minutes = Math.floor(seconds / 60) % 60;
        const remainder = String(seconds % 60).padStart(2, "0");
        return hours > 0
            ? hours + ":" + String(minutes).padStart(2, "0") + ":" + remainder
            : minutes + ":" + remainder;
    }

    onRangeDaysChanged: page = 0
    onPageCountChanged: Qt.callLater(function() {
        root.page = Math.min(root.page, root.pageCount - 1);
    })

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 36
    radius: 10
    color: MementoPalette.base
    border.color: MementoPalette.border

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 18
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Dictionary lookup history")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.pixelSize: 17
                    font.bold: true
                    color: MementoPalette.windowText
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("New lookups are saved on this device, starting with this update. Up to 10,000 recent lookups are kept.")
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    font.pixelSize: 12
                    color: MementoPalette.placeholderText
                }
            }

            Button {
                text: qsTr("Clear history")
                enabled: root.totalSaved > 0 || LookupHistory.lastError.length > 0
                onClicked: clearDialog.open()
            }
        }

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: qsTr("Search words, readings, subtitles or titles")
            selectByMouse: true
            onTextChanged: root.page = 0
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: LookupHistory.lastError
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: MementoPalette.windowText
        }

        Label {
            Layout.fillWidth: true
            text: root.total === 1 ? qsTr("1 lookup") : qsTr("%1 lookups").arg(root.total)
            textFormat: Text.PlainText
            color: MementoPalette.placeholderText
            font.pixelSize: 12
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: 12
            Layout.bottomMargin: 12
            visible: root.entries.length === 0
            text: searchField.text.trim().length > 0
                ? qsTr("No lookups match your search in this period.")
                : root.totalSaved > 0
                    ? qsTr("No lookups in this period. Choose a different date range to see more.")
                    : qsTr("Look up a word in the dictionary to start your history. Words from subtitles will include the line and episode they came from.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: MementoPalette.placeholderText
        }

        Repeater {
            model: root.entries

            delegate: Rectangle {
                id: entryCard
                required property var modelData
                readonly property bool hasScene: !!modelData.path
                    && Number.isFinite(Number(modelData.position))
                    && Number(modelData.position) >= 0

                Layout.fillWidth: true
                implicitHeight: entryContent.implicitHeight + 24
                radius: 6
                color: MementoPalette.window

                ColumnLayout {
                    id: entryContent
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 12
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                Layout.fillWidth: true
                                text: entryCard.modelData.word || ""
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                font.pixelSize: 18
                                font.bold: true
                                color: MementoPalette.windowText
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: text.length > 0 && text !== entryCard.modelData.word
                                text: entryCard.modelData.reading || ""
                                textFormat: Text.PlainText
                                wrapMode: Text.Wrap
                                color: MementoPalette.placeholderText
                            }
                        }

                        Label {
                            text: root.timestamp(entryCard.modelData.lookedUp)
                            textFormat: Text.PlainText
                            color: MementoPalette.placeholderText
                            font.pixelSize: 11
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: entryCard.modelData.text || ""
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: MementoPalette.windowText
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: {
                            const title = entryCard.modelData.title || "";
                            const episode = entryCard.modelData.episode || "";
                            const source = episode && episode !== title
                                ? title ? title + " · " + episode : episode
                                : title;
                            if (!entryCard.hasScene) return source;
                            return source
                                ? source + " · " + root.positionLabel(entryCard.modelData.position)
                                : root.positionLabel(entryCard.modelData.position);
                        }
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        font.pixelSize: 12
                        color: MementoPalette.placeholderText
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Button {
                            text: qsTr("Look up")
                            onClicked: root.lookupRequested(entryCard.modelData.word)
                        }

                        Button {
                            visible: entryCard.hasScene
                            text: qsTr("Go to scene")
                            onClicked: root.sceneRequested(entryCard.modelData.path,
                                                           entryCard.modelData.position)
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: qsTr("Remove")
                            onClicked: LookupHistory.remove(entryCard.modelData.id)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.total > root.pageSize
            spacing: 12

            Label {
                Layout.fillWidth: true
                text: qsTr("%1–%2 of %3")
                    .arg(root.page * root.pageSize + 1)
                    .arg(Math.min((root.page + 1) * root.pageSize, root.total))
                    .arg(root.total)
                textFormat: Text.PlainText
                color: MementoPalette.placeholderText
            }

            Button {
                text: qsTr("Previous")
                enabled: root.page > 0
                onClicked: root.page--
            }

            Button {
                text: qsTr("Next")
                enabled: root.page + 1 < root.pageCount
                onClicked: root.page++
            }
        }
    }

    Dialog {
        id: clearDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(440, parent ? parent.width - 40 : 440)
        implicitHeight: clearMessage.implicitHeight + 120
        modal: true
        title: qsTr("Clear dictionary lookup history?")
        standardButtons: Dialog.Yes | Dialog.Cancel
        contentWidth: Math.max(1, width - leftPadding - rightPadding)
        contentHeight: clearMessage.implicitHeight

        contentItem: Label {
            id: clearMessage
            width: clearDialog.contentWidth
            text: qsTr("Delete all saved lookups, including those outside the selected date range? This cannot be undone.")
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
        }

        onAccepted: {
            if (LookupHistory.clear()) root.page = 0;
        }
    }
}
