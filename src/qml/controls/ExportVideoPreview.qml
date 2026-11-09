pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

ColumnLayout {
    id: root
    property bool active: false
    property bool canPreview: false
    property string sourcePath: ""
    readonly property var player: previewPlayer
    readonly property bool ready: active && sourcePath.length > 0 &&
        previewPlayer.initialized && player.state.path === sourcePath &&
        player.state.duration > 0 && !player.loading
    onSourcePathChanged: synchronizeSource()
    onActiveChanged: synchronizeSource()
    function synchronizeSource() {
        if (!previewPlayer.initialized) return;
        if (active && sourcePath.length) previewPlayer.controller.loadFile(sourcePath);
        else {
            previewPlayer.controller.pause();
            previewPlayer.controller.stop();
        }
    }
    signal requested()
    spacing: 10

    function timeLabel(seconds) {
        const value = Math.max(0, Math.floor(seconds));
        return Math.floor(value / 60).toString() + ":" + (value % 60).toString().padStart(2, "0");
    }
    function pause() { if (player && ready) player.controller.pause(); }

    RowLayout {
        Layout.fillWidth: true
        Label { text: qsTr("Preview"); font.bold: true; Layout.fillWidth: true }
        Button {
            objectName: "generateExportPreview"
            text: root.sourcePath.length ? qsTr("Refresh preview") : qsTr("Preview section")
            enabled: root.canPreview && !VideoExporter.busy
            onClicked: root.requested()
        }
    }
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(160, width * 9 / 16)
        color: "black"
        radius: 4
        clip: true
        // Reuse one player across previews; unload the file when the dialog closes.
        MpvPreview {
            id: previewPlayer
            objectName: "exportPreviewPlayer"
            visible: root.sourcePath.length > 0
            anchors.fill: parent
            property bool initialized: false
            onInitialized: {
                initialized = true;
                root.synchronizeSource();
            }
            onFileLoaded: {
                mutePreview.checked = true;
                controller.setVolume(0);
                controller.pause();
            }
        }
        Label {
            anchors.centerIn: parent
            width: parent.width - 36
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            color: "#d8dfec"
            visible: !root.sourcePath.length || !!root.player?.playbackError
            text: root.player?.playbackError || (VideoExporter.preparingPreview ?
                qsTr("Preparing preview… %1%").arg(Math.round(VideoExporter.progress * 100)) :
                qsTr("Preview the selected section with your export settings."))
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Button {
            objectName: "playExportPreview"
            text: root.player && !root.player.state.pause ? qsTr("Pause") : qsTr("Play")
            enabled: root.ready
            onClicked: {
                if (!root.player.state.pause) root.player.controller.pause();
                else {
                    if (root.player.state.timePosition >= root.player.state.duration - 0.1)
                        root.player.controller.seek(0);
                    root.player.controller.play();
                }
            }
        }
        Slider {
            id: seek
            objectName: "seekExportPreview"
            Layout.fillWidth: true
            enabled: root.ready
            from: 0
            to: Math.max(0.1, root.sourcePath.length ? root.player.state.duration : 0)
            value: root.sourcePath.length ? root.player.state.timePosition : 0
            Accessible.name: qsTr("Preview position")
            onMoved: root.player.controller.seek(value)
        }
        Label {
            text: root.sourcePath.length ? root.timeLabel(root.player.state.timePosition) + " / " + root.timeLabel(root.player.state.duration) : "0:00 / 0:00"
        }
    }
    CheckBox {
        id: mutePreview
        objectName: "muteExportPreview"
        text: qsTr("Mute preview")
        checked: true
        enabled: root.ready && root.player.state.aid > 0
        onToggled: { if (root.player) root.player.controller.setVolume(checked ? 0 : 80); }
    }
    Label {
        text: qsTr("Uses the selected quality, audio and subtitles. Changing the selection clears the preview.")
        wrapMode: Text.Wrap
        Layout.fillWidth: true
        opacity: 0.75
    }
}
