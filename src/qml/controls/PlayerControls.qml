import QtQuick
import QtCore
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Rectangle {
    id: root

    required property MpvPlayer player

    readonly property real position: durationSlider.position

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

    property int lastSubtitleSeekDirection: 0
    property real subtitleSeekOrigin: 0

    /**
     * A single press jumps to the adjacent subtitle start. A repeated press in
     * the same direction changes that gesture to a three-second relative seek.
     */
    function subtitleAwareSeek(direction) {
        if (!Number.isFinite(root.player.state.timePosition)) return;
        if (subtitleSeekRepeatTimer.running &&
            root.lastSubtitleSeekDirection === direction)
        {
            subtitleSeekRepeatTimer.stop();
            root.player.controller.seek(Math.max(
                0, root.subtitleSeekOrigin + direction * 3));
            root.player.controller.showText(direction < 0 ?
                qsTr("Back 3 seconds") : qsTr("Forward 3 seconds"));
            root.lastSubtitleSeekDirection = 0;
            return;
        }

        root.subtitleSeekOrigin = root.player.state.timePosition;
        root.lastSubtitleSeekDirection = direction;
        subtitleSeekRepeatTimer.restart();
        const model = SubtitleLists.primary;
        if (model && model.fullTimelineReady) {
            const target = model.adjacentSubtitleStart(
                root.subtitleSeekOrigin, direction, root.player.state.subtitle.delay);
            if (Number.isFinite(target) && target >= 0)
                root.player.controller.seek(target);
        } else {
            root.player.controller.subtitleSeek(direction, true);
        }
    }

    Connections {
        target: root.player.state
        function onPathChanged() {
            subtitleSeekRepeatTimer.stop();
            root.lastSubtitleSeekDirection = 0;
        }
    }

    Timer {
        id: subtitleSeekRepeatTimer
        interval: 550
        repeat: false
        onTriggered: root.lastSubtitleSeekDirection = 0
    }

    height: 50
    color: MementoPalette.window

    /* Make sure mouse events aren't sent to mpv */
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
    }

    RowLayout {
        id: layout

        readonly property int defaultMargin: 10

        anchors.fill: parent
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
            onClicked: root.subtitleAwareSeek(-1)

            ToolTip.visible: hovered
            ToolTip.text: qsTr("Previous subtitle · double press: back 3 seconds")
        }

        ToolButton {
            id: seekNextButton
            focusPolicy: Qt.NoFocus
            icon.name: MementoSettings.interfaceSystemIcons ? "media-seek-forward" : null
            icon.source: Utils.toImageProvider("fast-forward", MementoPalette.text)
            onClicked: root.subtitleAwareSeek(1)

            ToolTip.visible: hovered
            ToolTip.text: qsTr("Next subtitle · double press: forward 3 seconds")
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
            text: qsTr("★ Highlight")
            focusPolicy: Qt.NoFocus
            enabled: !!root.player.state.path && root.player.state.duration > 0
            onClicked: root.highlightTimestamp()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Save the current timestamp as a highlight")
            Accessible.name: qsTr("Highlight current timestamp")
        }

        ToolButton {
            text: qsTr("Highlights (%1)").arg(root.highlights.length)
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
            Layout.preferredWidth: 150
            focusPolicy: Qt.NoFocus
            from: 0
            to: root.player.state.maxVolume
            value: root.player.state.volume
            onMoved: root.player.controller.setVolume(value)
        }

        Label {
            id: volumeLabel
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
