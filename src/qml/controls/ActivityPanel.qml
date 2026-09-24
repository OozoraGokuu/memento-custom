pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Frame {
    id: root
    objectName: "activityPanel"
    required property MpvPlayer mediaPlayer
    property string lastMessage: ""
    readonly property var tasks: {
        let items = [];
        if (mediaPlayer.loading || mediaPlayer.buffering)
            items.push({text: mediaPlayer.loading ? qsTr("Loading episode…") : qsTr("Buffering episode…"), progress: -1});
        if (EpisodeLibrary.currentEntry.loadingMetadata === true)
            items.push({text: EpisodeLibrary.currentEntry.torrentState || qsTr("Waiting for torrent metadata and peers…"), progress: -1});
        if (SubtitleLists.primary?.loading || SubtitleLists.secondary?.loading)
            items.push({text: qsTr("Reading subtitles…"), progress: -1});
        if (JimakuClient.busy) items.push({text: JimakuClient.status || qsTr("Loading subtitles…"), progress: -1});
        if (KitsunekkoClient.busy) items.push({text: KitsunekkoClient.status || qsTr("Loading subtitle library…"), progress: -1});
        if (DictionaryController.busy || DictionaryController.modifyingDatabase)
            items.push({text: DictionaryController.busy ? DictionaryController.activity : qsTr("Updating dictionaries…"), progress: DictionaryController.busy ? DictionaryController.progress : -1});
        if (TorrentSearchClient.busy) items.push({text: TorrentSearchClient.status, progress: -1});
        if (MigakuClient.busy) items.push({text: qsTr("Exporting sentence image and audio…"), progress: -1});
        return items;
    }
    visible: tasks.length > 0 || lastMessage.length > 0
    width: Math.min(380, parent.width - 24)
    padding: 14
    background: Rectangle { color: "#ee20232b"; radius: 10; border.color: "#555b6b" }
    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Label { text: qsTr("Activity"); color: "white"; font.bold: true; Layout.fillWidth: true }
            ToolButton { text: "×"; visible: root.tasks.length === 0; onClicked: root.lastMessage = ""; Accessible.name: qsTr("Dismiss activity") }
        }
        Repeater {
            model: root.tasks
            delegate: ColumnLayout {
                required property var modelData
                Layout.fillWidth: true
                Label { text: modelData.text; color: "white"; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ProgressBar { Layout.fillWidth: true; indeterminate: modelData.progress < 0; value: Math.max(0, modelData.progress) }
            }
        }
        Label { visible: root.lastMessage.length > 0; text: root.lastMessage; color: "#d8dfec"; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Button { visible: root.mediaPlayer.loading || root.mediaPlayer.buffering; text: qsTr("Stop loading episode"); onClicked: root.mediaPlayer.controller.stop() }
    }
    Timer { id: dismissTimer; interval: 10000; onTriggered: root.lastMessage = "" }
    Connections {
        target: DictionaryController
        function onActivityChanged() {
            if (!DictionaryController.busy) { root.lastMessage = DictionaryController.activity; dismissTimer.restart(); }
        }
    }
    Connections {
        target: EpisodeLibrary
        function onErrorOccurred(message) { root.lastMessage = message; dismissTimer.restart(); }
    }
    Connections {
        target: root.mediaPlayer
        function onPlaybackStatusChanged() {
            if (root.mediaPlayer.playbackError.length) { root.lastMessage = root.mediaPlayer.playbackError; dismissTimer.restart(); }
        }
    }
}
