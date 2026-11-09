pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Dialog {
    id: root
    required property MpvPlayer player
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1100, parent.width - 32)
    height: Math.min(implicitHeight, parent.height - 32)
    title: qsTr("Export Video")
    modal: true
    property string mediaPath: ""
    property string folder: ""
    property var tracks: []
    readonly property double startSeconds: parseTime(startField.text)
    readonly property double endSeconds: parseTime(endField.text)
    readonly property int maxHeight: [0, 1080, 720, 480][resolution.currentIndex]
    readonly property bool mediaChanged: mediaPath !== player.state.path
    readonly property var estimate: {
        // Recalculate even when a different video uses the same section times.
        const width = player.state.videoWidth;
        const height = player.state.videoHeight;
        const duration = player.state.duration;
        const audioId = player.state.aid;
        if (width <= 0 || height <= 0 || duration <= 0)
            return {valid: false, error: qsTr("Open a video with a known duration first.")};
        return VideoExporter.estimate(startSeconds, endSeconds,
            maxHeight, quality.currentIndex, audio.checked && audioId > 0);
    }

    readonly property bool validSelection: !!estimate.valid && !mediaChanged &&
        !(primary.currentValue > 0 && primary.currentValue === secondary.currentValue)
    readonly property string previewSelection: JSON.stringify([
        mediaPath, startSeconds, endSeconds, maxHeight, quality.currentIndex,
        primary.currentValue, secondary.currentValue, audio.checked,
        player.state.aid, player.state.vid, player.state.subtitle.delay,
        player.state.secondarySubtitle.delay])
    onPreviewSelectionChanged: invalidatePreview()
    onMediaChangedChanged: { if (mediaChanged) invalidatePreview(); }
    onAboutToHide: invalidatePreview()
    function invalidatePreview() {
        previewPane.pause();
        if (VideoExporter.previewPath.length || VideoExporter.preparingPreview)
            VideoExporter.clearPreview();
    }
    Connections {
        target: root.player.state
        function onSubtitleTracksChanged() { root.invalidatePreview(); }
    }

    function formatTime(value) {
        let ms = Math.round(Math.max(0, value) * 1000);
        let h = Math.floor(ms / 3600000);
        let m = Math.floor(ms / 60000) % 60;
        let s = Math.floor(ms / 1000) % 60;
        return h.toString().padStart(2, "0") + ":" + m.toString().padStart(2, "0") + ":" +
            s.toString().padStart(2, "0") + "." + (ms % 1000).toString().padStart(3, "0");
    }
    function parseTime(text) {
        if (!/^\d+:\d{2}:\d{2}(\.\d{1,3})?$/.test(text.trim())) return NaN;
        let parts = text.trim().split(":");
        if (+parts[1] >= 60 || +parts[2] >= 60) return NaN;
        return +parts[0] * 3600 + +parts[1] * 60 + +parts[2];
    }
    function prepare() {
        if (VideoExporter.busy) { open(); return; }
        invalidatePreview();
        mediaPath = player.state.path;
        folder = VideoExporter.destination();
        const duration = player.state.duration;
        const start = Math.max(0, Math.min(player.state.timePosition, duration - 0.1));
        startField.text = formatTime(start);
        endField.text = formatTime(Math.min(duration, start + 10));
        let available = [{text: qsTr("None"), trackId: -1}];
        let primaryIndex = 0, secondaryIndex = 0;
        for (let track of player.state.subtitleTracks) {
            let label = qsTr("Track %1").arg(track.id);
            if (track.language) label += " · " + track.language;
            if (track.title) label += " · " + track.title;
            available.push({text: label, trackId: track.id});
            if (track.id === player.state.sid) primaryIndex = available.length - 1;
            if (track.id === player.state.secondarySid) secondaryIndex = available.length - 1;
        }
        tracks = available;
        primary.currentIndex = primaryIndex;
        secondary.currentIndex = secondaryIndex;
        audio.checked = player.state.aid > 0;
        open();
    }
    contentItem: ScrollView {
        clip: true
        contentWidth: availableWidth
        implicitHeight: form.implicitHeight
        GridLayout {
            id: form
            width: parent.width
            columns: root.width >= 920 ? 2 : 1
            columnSpacing: 24
            rowSpacing: 20
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 520
                Layout.alignment: Qt.AlignTop
                spacing: 14
                Label { text: qsTr("Selected section"); font.bold: true }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    enabled: !VideoExporter.busy && !root.mediaChanged
                    Label { text: qsTr("Start") }
                    TextField { id: startField; objectName: "exportStart"; Layout.fillWidth: true; placeholderText: "00:00:00.000"; selectByMouse: true; Accessible.name: qsTr("Section start time") }
                    Button { text: qsTr("Use current time"); onClicked: startField.text = root.formatTime(root.player.state.timePosition) }
                    Label { text: qsTr("End") }
                    TextField { id: endField; objectName: "exportEnd"; Layout.fillWidth: true; placeholderText: "00:00:10.000"; selectByMouse: true; Accessible.name: qsTr("Section end time") }
                    Button { text: qsTr("Use current time"); onClicked: endField.text = root.formatTime(root.player.state.timePosition) }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.mediaChanged ? qsTr("The video changed. Close and reopen this dialog to select a new section.") :
                        root.estimate.valid ? qsTr("%1 seconds selected · hours:minutes:seconds.milliseconds").arg(root.estimate.seconds.toFixed(2)) : root.estimate.error
                    wrapMode: Text.Wrap
                    color: root.estimate.valid && !root.mediaChanged ? palette.text : "#e9a36b"
                }
                GridLayout {
                    columns: 2
                    Layout.fillWidth: true
                    enabled: !VideoExporter.busy && !root.mediaChanged
                    Label { text: qsTr("Resolution") }
                    ComboBox { id: resolution; Layout.fillWidth: true; model: [qsTr("Original"), "1080p", "720p", "480p"] }
                    Label { text: qsTr("Quality") }
                    ComboBox { id: quality; Layout.fillWidth: true; model: [qsTr("Compact"), qsTr("Balanced"), qsTr("High")]; currentIndex: 1 }
                    Label { text: qsTr("Primary subtitles") }
                    ComboBox { id: primary; Layout.fillWidth: true; model: root.tracks; textRole: "text"; valueRole: "trackId" }
                    Label { text: qsTr("Secondary subtitles") }
                    ComboBox { id: secondary; Layout.fillWidth: true; model: root.tracks; textRole: "text"; valueRole: "trackId" }
                    Label { text: qsTr("Audio") }
                    CheckBox { id: audio; text: qsTr("Include selected audio track"); enabled: root.player.state.aid > 0 }
                }
                Label { Layout.fillWidth: true; text: qsTr("Selected subtitles are permanently visible in the exported video. Choose None for both to export without subtitles. Playback subtitle delays are applied."); wrapMode: Text.Wrap; opacity: 0.75 }
                Frame {
                    Layout.fillWidth: true
                    ColumnLayout {
                        anchors.fill: parent
                        Label { text: root.estimate.valid ? qsTr("Estimated size: %1 MB").arg(root.estimate.sizeMB.toFixed(1)) : qsTr("Estimated size: —"); font.bold: true; font.pixelSize: 18 }
                        Label { text: root.estimate.valid ? qsTr("MP4 · H.264 · %1 × %2 · size may vary").arg(root.estimate.width).arg(root.estimate.height) : qsTr("MP4 · H.264"); opacity: 0.75 }
                        Label { text: qsTr("Save to: %1").arg(root.folder); wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                    }
                }
                Label { visible: primary.currentValue > 0 && primary.currentValue === secondary.currentValue; text: qsTr("Choose different subtitle tracks."); color: "#e9a36b" }
                ProgressBar { visible: VideoExporter.busy; value: VideoExporter.progress; Layout.fillWidth: true }
                Label { visible: VideoExporter.status.length > 0; text: VideoExporter.status; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                RowLayout {
                    Layout.fillWidth: true
                    Button { text: qsTr("Open folder"); enabled: !VideoExporter.busy && VideoExporter.outputPath.length > 0; onClicked: VideoExporter.openFolder() }
                    Item { Layout.fillWidth: true }
                    Button { text: VideoExporter.busy ? qsTr("Keep watching") : qsTr("Close"); onClicked: root.close() }
                    Button { visible: VideoExporter.busy; text: VideoExporter.preparingPreview ? qsTr("Cancel preview") : qsTr("Cancel export"); onClicked: VideoExporter.cancel() }
                    Button {
                        text: qsTr("Export section")
                        objectName: "exportSection"
                        visible: !VideoExporter.busy
                        highlighted: true
                        enabled: root.validSelection
                        onClicked: {
                            previewPane.pause();
                            VideoExporter.start(root.mediaPath, root.startSeconds, root.endSeconds,
                                root.maxHeight, quality.currentIndex, primary.currentValue ?? -1,
                                secondary.currentValue ?? -1, audio.checked);
                        }
                    }
                }
            }
            ExportVideoPreview {
                id: previewPane
                Layout.fillWidth: true
                Layout.preferredWidth: 460
                Layout.alignment: Qt.AlignTop
                active: root.visible
                canPreview: root.validSelection
                sourcePath: VideoExporter.previewPath
                onRequested: VideoExporter.preview(root.mediaPath, root.startSeconds, root.endSeconds,
                    root.maxHeight, quality.currentIndex, primary.currentValue ?? -1,
                    secondary.currentValue ?? -1, audio.checked)
            }
        }
    }
}
