import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Rectangle {
    id: root

    required property MpvPlayer player
    property bool canCopySubtitle: false
    property bool sentencePending: false
    signal copySubtitleRequested()
    signal copyScreenshotRequested()

    readonly property real position: durationSlider.position
    property real pendingSubtitleSeekTarget: -1
    property string pendingSubtitleSeekPath: ""

    Timer {
        id: pendingSubtitleSeekTimer
        interval: 700
        onTriggered: root.pendingSubtitleSeekTarget = -1
    }

    Connections {
        target: root.player.state
        function onPathChanged() {
            pendingSubtitleSeekTimer.stop();
            root.pendingSubtitleSeekTarget = -1;
            root.pendingSubtitleSeekPath = "";
        }
    }

    function seekAdjacentSubtitle(direction) {
        const state = root.player.state;
        if (state.sid <= 0) {
            root.player.controller.showText(qsTr("No primary subtitle track selected"));
            return;
        }

        const model = SubtitleLists.primary;
        if (!model?.fullTimelineReady) {
            pendingSubtitleSeekTimer.stop();
            root.pendingSubtitleSeekTarget = -1;
            root.player.controller.subtitleSeek(direction, true);
            return;
        }

        const position = pendingSubtitleSeekTimer.running &&
            root.pendingSubtitleSeekPath === state.path ?
            root.pendingSubtitleSeekTarget : state.timePosition;
        if (!Number.isFinite(position)) return;
        const target = model.adjacentSubtitleStart(
            position, direction, state.subtitle.delay);
        if (target < 0) {
            root.player.controller.showText(direction < 0 ?
                qsTr("No previous subtitle") : qsTr("No next subtitle"));
            return;
        }
        root.pendingSubtitleSeekTarget = target;
        root.pendingSubtitleSeekPath = state.path;
        pendingSubtitleSeekTimer.restart();
        root.player.controller.seek(target);
    }

    Settings {
        id: highlightSettings
        category: "TimestampHighlights"
        property string saved: "{}"
    }

    readonly property var highlightMap: {
        try { return JSON.parse(highlightSettings.saved); }
        catch (error) { return {}; }
    }
    readonly property var highlights: highlightMap[root.player.state.path] || []

    function highlightTimestamp() {
        const time = root.player.state.timePosition;
        const path = root.player.state.path;
        if (!path || !Number.isFinite(time) || time < 0) return;
        const map = JSON.parse(highlightSettings.saved || "{}");
        const items = map[path] || [];
        if (items.some(t => Math.abs(t - time) < 1)) {
            root.player.controller.showText(qsTr("This timestamp is already highlighted"));
            return;
        }
        items.push(time);
        items.sort((a, b) => a - b);
        map[path] = items;
        highlightSettings.saved = JSON.stringify(map);
        highlightSettings.sync();
        root.player.controller.showText(qsTr("Highlighted %1").arg(Utils.toTimeString(time)));
    }

    function removeHighlight(time) {
        const map = JSON.parse(highlightSettings.saved || "{}");
        map[root.player.state.path] = root.highlights.filter(t => t !== time);
        highlightSettings.saved = JSON.stringify(map);
        highlightSettings.sync();
    }

    height: 84
    color: MementoPalette.window

    /* Make sure mouse events aren't sent to mpv */
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    RowLayout {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.rightMargin: 10
        height: 34
        ToolButton {
            objectName: "copySubtitleButton"
            text: qsTr("Copy text")
            focusPolicy: Qt.NoFocus
            enabled: root.canCopySubtitle
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Copy current subtitle (Ctrl+C)")
            onClicked: root.copySubtitleRequested()
        }

        ToolButton {
            objectName: "copyScreenshotButton"
            text: root.sentencePending ? qsTr("Copy sentence") : qsTr("Copy image")
            focusPolicy: Qt.NoFocus
            enabled: root.player.state.path.length > 0
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Ctrl+X: copy screenshot with subtitles, then its sentence")
            onClicked: root.copyScreenshotRequested()
        }

    }

    RowLayout {
        id: layout

        readonly property int defaultMargin: 10

        anchors.fill: parent
        anchors.topMargin: 34
        anchors.leftMargin: layout.defaultMargin
        anchors.rightMargin: layout.defaultMargin

        ToolButton {
            id: playButton
            focusPolicy: Qt.NoFocus
            visible: root.player.state.pause
            icon.name: MementoSettings.interfaceSystemIcons ? "media-playback-start" : null
            icon.source: Utils.toImageProvider("play", MementoPalette.text)
            onClicked: root.player.controller.play()
        }

        ToolButton {
            id: pauseButton
            focusPolicy: Qt.NoFocus
            visible: !root.player.state.pause
            icon.name: MementoSettings.interfaceSystemIcons ? "media-playback-pause" : null
            icon.source: Utils.toImageProvider("pause", MementoPalette.text)
            onClicked: root.player.controller.pause()
        }

        ToolButton {
            id: skipPrevButton
            focusPolicy: Qt.NoFocus
            icon.name: MementoSettings.interfaceSystemIcons ? "media-skip-backward" : null
            icon.source: Utils.toImageProvider("skip-previous", MementoPalette.text)
            onClicked: root.player.controller.playlistPrev()
        }

        ToolButton {
            id: seekPrevButton
            focusPolicy: Qt.NoFocus
            icon.name: MementoSettings.interfaceSystemIcons ? "media-seek-backward" : null
            icon.source: Utils.toImageProvider("fast-rewind", MementoPalette.text)
            onClicked: root.seekAdjacentSubtitle(-1)

            ToolTip.visible: hovered
            ToolTip.text: qsTr("Previous subtitle")
        }

        ToolButton {
            id: seekNextButton
            focusPolicy: Qt.NoFocus
            icon.name: MementoSettings.interfaceSystemIcons ? "media-seek-forward" : null
            icon.source: Utils.toImageProvider("fast-forward", MementoPalette.text)
            onClicked: root.seekAdjacentSubtitle(1)

            ToolTip.visible: hovered
            ToolTip.text: qsTr("Next subtitle")
        }

        ToolButton {
            id: skipNextButton
            focusPolicy: Qt.NoFocus
            icon.name: MementoSettings.interfaceSystemIcons ? "media-skip-forward" : null
            icon.source: Utils.toImageProvider("skip-next", MementoPalette.text)
            onClicked: root.player.controller.playlistNext()
        }

        ToolButton {
            id: highlightButton
            text: root.width < 850 ? "★" : qsTr("★ Highlight")
            focusPolicy: Qt.NoFocus
            enabled: !!root.player.state.path && root.player.state.duration > 0
            onClicked: root.highlightTimestamp()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Save the current timestamp as a highlight")
            Accessible.name: qsTr("Highlight current timestamp")
        }

        ToolButton {
            text: root.width < 850 ? qsTr("★ %1").arg(root.highlights.length) : qsTr("Highlights (%1)").arg(root.highlights.length)
            focusPolicy: Qt.NoFocus
            enabled: root.highlights.length > 0
            onClicked: highlightMenu.popup()
            Menu {
                id: highlightMenu
                Instantiator {
                    model: root.highlights
                    delegate: Menu {
                        required property var modelData
                        title: "★ " + Utils.toTimeString(modelData)
                        MenuItem {
                            text: qsTr("Jump to highlight")
                            onTriggered: root.player.controller.seek(modelData)
                        }
                        MenuItem {
                            text: qsTr("Remove highlight")
                            onTriggered: root.removeHighlight(modelData)
                        }
                    }
                    onObjectAdded: (index, object) => highlightMenu.insertMenu(index, object)
                    onObjectRemoved: (index, object) => highlightMenu.removeMenu(object)
                }
            }
        }

        Label {
            id: positionLabel
            Layout.preferredWidth: durationLabel.width
            Layout.leftMargin: layout.defaultMargin
            Layout.rightMargin: layout.defaultMargin
            focusPolicy: Qt.NoFocus
            horizontalAlignment: Text.AlignHCenter
            text: Utils.toTimeString(durationSlider.value)
        }

        PlayerSlider {
            id: durationSlider
            Layout.fillWidth: true
            focusPolicy: Qt.NoFocus
            enabled: from !== to
            from: 0
            to: root.player.state.duration
            chapters: root.player.state.chapters
            highlights: root.highlights
            onMoved: root.player.controller.seek(value)

            Timer {
                id: durationUpdateTimer
                interval: 250
                running: true
                repeat: true
                onTriggered: durationSlider.value = root.player.state.timePosition
            }
        }

        Label {
            id: durationLabel
            Layout.leftMargin: layout.defaultMargin
            Layout.rightMargin: layout.defaultMargin
            focusPolicy: Qt.NoFocus
            horizontalAlignment: Text.AlignHCenter
            text: Utils.toTimeString(root.player.state.duration);
        }

        Slider {
            id: volumeSlider
            visible: root.width >= 1000
            Layout.preferredWidth: 150
            focusPolicy: Qt.NoFocus
            from: 0
            to: root.player.state.maxVolume
            value: root.player.state.volume
            onMoved: root.player.controller.setVolume(value)
        }

        Label {
            id: volumeLabel
            visible: root.width >= 1000
            Layout.leftMargin: layout.defaultMargin
            Layout.rightMargin: layout.defaultMargin
            Layout.preferredWidth: 30
            focusPolicy: Qt.NoFocus
            horizontalAlignment: Text.AlignHCenter
            text: `${root.player.state.volume}%`
        }

        ToolButton {
            id: subtitleListButton
            focusPolicy: Qt.NoFocus
            icon.source: Utils.toImageProvider("list", MementoPalette.text)
            onClicked: MementoSettings.windowSubtitleList = !MementoSettings.windowSubtitleList
        }

        ToolButton {
            id: ocrButton
            focusPolicy: Qt.NoFocus
            visible: Features.ocr && MementoSettings.ocrEnabled
            icon.source: Utils.toImageProvider("eye", MementoPalette.text)
            onClicked: root.player.startOcrMode()
        }

        ToolButton {
            id: fullscreenButton
            focusPolicy: Qt.NoFocus
            visible: !root.player.state.fullscreen
            icon.name: MementoSettings.interfaceSystemIcons ? "view-fullscreen" : null
            icon.source: Utils.toImageProvider("fullscreen", MementoPalette.text)
            onClicked: root.player.controller.setFullscreen(true)
        }

        ToolButton {
            id: fullscreenExitButton
            focusPolicy: Qt.NoFocus
            visible: root.player.state.fullscreen
            icon.name: MementoSettings.interfaceSystemIcons ? "view-restore" : null
            icon.source: Utils.toImageProvider("fullscreen-exit", MementoPalette.text)
            onClicked: root.player.controller.setFullscreen(false)
        }
    }

    Loader {
        anchors.bottom: root.top
        anchors.bottomMargin: 5
        x: durationSlider.x + durationSlider.xPosition - width / 2

        active: MementoSettings.behaviorOscPreviewThumbnails
        sourceComponent: Component {
            PlayerThumbnail {
                visible: durationSlider.hovered && !durationSlider.pressed
                opacity: active ? 1 : 0
                path: root.player.state.path
                position: durationSlider.cursorValue
            }
        }
    }
}
