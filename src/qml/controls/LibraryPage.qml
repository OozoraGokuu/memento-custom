pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Ripose.Memento

Page {
    id: root

    required property MpvPlayer player
    readonly property var filteredLibrary: {
        const query = titleSearch.text.trim().toLowerCase();
        let entries = EpisodeLibrary.library.map(function(item, index) {
            return Object.assign({}, item, { libraryIndex: index });
        }).filter(function(item) { return item.title.toLowerCase().includes(query); });
        if (librarySort.currentIndex === 1) entries.sort(function(a, b) { return a.title.localeCompare(b.title); });
        if (librarySort.currentIndex === 2) entries = entries.filter(function(item) { return item.watchedCount < item.totalCount; });
        return entries;
    }
    readonly property string episodeFilterEntryId:
        EpisodeLibrary.currentEntry.id ?? ""

    Dialog {
        id: aniListDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(760, parent.width - 32)
        height: Math.min(650, parent.height - 32)
        modal: true
        title: qsTr("AniList title matching")
        standardButtons: Dialog.Close
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                AniListMappingPane {
                    Layout.fillWidth: true
                    targetKey: EpisodeLibrary.currentEntry.id || ""
                    targetTitle: EpisodeLibrary.currentEntry.title || ""
                }
                Label {
                    Layout.fillWidth: true
                    text: AniListClient.status
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: !AniListClient.connected
                    text: qsTr("Connect your own account in Options → AniList Integration to enable syncing.")
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    signal torrentSearchRequested()

    onEpisodeFilterEntryIdChanged: episodeSearch.text = ""

    Connections {
        target: EpisodeLibrary

        function onErrorOccurred(message) {
            if (message.length > 0)
            {
                errorDialog.open();
            }
        }
    }

    function alpha(color, opacity) {
        return Qt.rgba(color.r, color.g, color.b, opacity);
    }

    function showError() {
        if (EpisodeLibrary.lastError.length > 0)
        {
            errorDialog.open();
        }
    }

    function playEpisode(index) {
        const path = EpisodeLibrary.episodePath(index);
        if (path.length > 0)
        {
            if (root.player.controller.loadFile(path, false, [
                "demuxer-max-bytes=32MiB",
                "demuxer-max-back-bytes=8MiB",
                "cache-pause-initial=no"
            ]))
            {
                root.player.controller.play();
            }
        }
        else
        {
            root.showError();
        }
    }

    function formatRate(bytes) {
        if (bytes < 1024)
            return qsTr("%1 B/s").arg(bytes);
        if (bytes < 1024 * 1024)
            return qsTr("%1 KiB/s").arg((bytes / 1024).toFixed(1));
        return qsTr("%1 MiB/s").arg((bytes / (1024 * 1024)).toFixed(1));
    }

    function formatSize(bytes) {
        if (bytes < 1024 * 1024)
            return qsTr("%1 KiB").arg(Math.max(0, bytes / 1024).toFixed(0));
        if (bytes < 1024 * 1024 * 1024)
            return qsTr("%1 MiB").arg((bytes / (1024 * 1024)).toFixed(0));
        return qsTr("%1 GiB").arg((bytes / (1024 * 1024 * 1024)).toFixed(1));
    }

    background: Rectangle {
        color: MementoPalette.window
    }

    header: ToolBar {
        implicitHeight: 62

        background: Rectangle {
            color: MementoPalette.window

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: MementoPalette.border
            }
        }

        contentItem: RowLayout {
            spacing: 8

            ColumnLayout {
                Layout.leftMargin: 14
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Media Library")
                    font.pixelSize: 17
                    font.bold: true
                    elide: Text.ElideRight
                }

                Label {
                    Layout.fillWidth: true
                    text: qsTr("Torrents and local episode folders")
                    color: MementoPalette.placeholderText
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }

            Button {
                text: qsTr("Search torrent website")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: root.torrentSearchRequested()
            }

            Button {
                text: qsTr("+ Add")
                flat: true
                focusPolicy: Qt.NoFocus
                onClicked: addMenu.open()

                Menu {
                    id: addMenu
                    y: parent.height

                    Action {
                        text: qsTr("Torrent File…")
                        onTriggered: torrentDialog.open()
                    }
                    Action {
                        text: qsTr("Magnet Link…")
                        onTriggered: magnetDialog.open()
                    }
                    MenuSeparator {}
                    Action {
                        text: qsTr("Episode Folder…")
                        onTriggered: addFolderDialog.open()
                    }
                }
            }

            ToolButton {
                Layout.rightMargin: 4
                text: "×"
                font.pixelSize: 20
                focusPolicy: Qt.NoFocus
                onClicked: MementoSettings.windowLibrary = false

                ToolTip.visible: hovered
                ToolTip.text: qsTr("Close library")
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.topMargin: 10
            Layout.bottomMargin: 5

            Label {
                Layout.fillWidth: true
                text: qsTr("YOUR LIBRARY")
                color: MementoPalette.placeholderText
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1.2
            }

            Label {
                text: EpisodeLibrary.library.length
                color: MementoPalette.placeholderText
                font.pixelSize: 11
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 8
            TextField {
                id: titleSearch
                Layout.fillWidth: true
                placeholderText: qsTr("Search your library")
                selectByMouse: true
            }
            ComboBox { id: librarySort; model: [qsTr("Library order"), qsTr("A–Z"), qsTr("Unfinished")] }
        }

        GridView {
            id: libraryList
            cellWidth: Math.max(220, width / Math.max(1, Math.floor(width / 250)))
            cellHeight: 112

            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, root.height * 0.38)
            Layout.minimumHeight: EpisodeLibrary.library.length > 0 ? 82 : 126
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            clip: true
            model: root.filteredLibrary
            currentIndex: EpisodeLibrary.currentIndex

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: ItemDelegate {
                id: libraryDelegate

                required property var modelData
                required property int index

                readonly property bool current: modelData.libraryIndex === EpisodeLibrary.currentIndex
                readonly property real watchedRatio: modelData.totalCount > 0 ?
                    modelData.watchedCount / modelData.totalCount : 0

                width: GridView.view.cellWidth - 8
                height: GridView.view.cellHeight - 8
                leftPadding: 12
                rightPadding: 12
                topPadding: 9
                bottomPadding: 9
                highlighted: current
                onClicked: EpisodeLibrary.currentIndex = modelData.libraryIndex

                background: Rectangle {
                    radius: 7
                    color: libraryDelegate.current ?
                        root.alpha(MementoPalette.accent, 0.18) :
                        (libraryDelegate.hovered ?
                            root.alpha(MementoPalette.text, 0.07) :
                            root.alpha(MementoPalette.text, 0.025))
                    border.width: libraryDelegate.current ? 1 : 0
                    border.color: root.alpha(MementoPalette.accent, 0.55)

                    Rectangle {
                        visible: libraryDelegate.current
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: parent.height - 18
                        radius: 2
                        color: MementoPalette.accent
                    }
                }

                contentItem: ColumnLayout {
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 7

                        Rectangle {
                            Layout.preferredWidth: typeLabel.implicitWidth + 12
                            Layout.preferredHeight: 20
                            radius: 4
                            color: modelData.type === "torrent" ?
                                root.alpha(MementoPalette.accent, 0.22) :
                                root.alpha(MementoPalette.text, 0.10)

                            Label {
                                id: typeLabel
                                anchors.centerIn: parent
                                text: modelData.type === "torrent" ?
                                    qsTr("TORRENT") : qsTr("FOLDER")
                                color: modelData.type === "torrent" ?
                                    MementoPalette.accent : MementoPalette.text
                                font.pixelSize: 9
                                font.bold: true
                                font.letterSpacing: 0.8
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.title
                            textFormat: Text.PlainText
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: libraryDelegate.current
                            text: qsTr("CURRENT")
                            color: MementoPalette.accent
                            font.pixelSize: 9
                            font.bold: true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: modelData.type === "torrent" ?
                                (modelData.ready ?
                                    qsTr("Ready on demand") : modelData.torrentState) :
                                qsTr("%1 available").arg(modelData.availableCount)
                            textFormat: Text.PlainText
                            color: MementoPalette.placeholderText
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }

                        Label {
                            text: qsTr("%1/%2 watched")
                                .arg(modelData.watchedCount)
                                .arg(modelData.totalCount)
                            color: MementoPalette.placeholderText
                            font.pixelSize: 10
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3
                        radius: 2
                        color: root.alpha(MementoPalette.text, 0.09)

                        Rectangle {
                            width: parent.width * libraryDelegate.watchedRatio
                            height: parent.height
                            radius: parent.radius
                            color: MementoPalette.accent
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 32)
                visible: EpisodeLibrary.library.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                text: qsTr("Your library is empty.\nAdd a torrent, magnet link, or episode folder.")
                color: MementoPalette.placeholderText
                lineHeight: 1.25
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 10
            visible: EpisodeLibrary.library.length > 0
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.preferredHeight: currentDetails.implicitHeight + 20
            visible: EpisodeLibrary.currentIndex >= 0
            radius: 7
            color: root.alpha(MementoPalette.text, 0.045)
            border.width: 1
            border.color: root.alpha(MementoPalette.border, 0.55)

            ColumnLayout {
                id: currentDetails
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Label {
                            Layout.fillWidth: true
                            text: EpisodeLibrary.currentEntry.type === "torrent" ?
                                qsTr("CURRENT TORRENT") : qsTr("CURRENT FOLDER")
                            color: MementoPalette.accent
                            font.pixelSize: 9
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Label {
                            Layout.fillWidth: true
                            text: EpisodeLibrary.currentEntry.title ?? ""
                            textFormat: Text.PlainText
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        text: qsTr("Continue")
                        flat: true
                        focusPolicy: Qt.NoFocus
                        enabled: (EpisodeLibrary.currentEntry.nextEpisodeIndex ?? -1) >= 0
                        onClicked: root.playEpisode(
                            EpisodeLibrary.currentEntry.nextEpisodeIndex)
                    }

                    Label {
                        visible: (EpisodeLibrary.currentEntry.audioTrack ?? -1) >= 0
                        text: qsTr("Audio: %1").arg(EpisodeLibrary.currentEntry.audioTrack)
                        color: MementoPalette.placeholderText
                        ToolTip.visible: audioHelp.hovered
                        ToolTip.text: qsTr("Choosing an audio track while playing remembers its number for every episode in this library entry.")
                        HoverHandler { id: audioHelp }
                    }
                    Button {
                        text: qsTr("AniList…")
                        onClicked: aniListDialog.open()
                    }

                    ToolButton {
                        text: "⋮"
                        font.pixelSize: 20
                        focusPolicy: Qt.NoFocus
                        onClicked: itemMenu.open()

                        Menu {
                            id: itemMenu
                            y: parent.height

                            Action {
                                text: qsTr("Choose Episode Folder…")
                                enabled: EpisodeLibrary.currentEntry.type === "folder"
                                onTriggered: contentFolderDialog.open()
                            }
                            Action {
                                text: qsTr("Refresh Library Item")
                                onTriggered: EpisodeLibrary.rescan()
                            }
                            MenuSeparator {}
                            Action {
                                text: qsTr("Remove from Library…")
                                onTriggered: removeDialog.open()
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 12

                    Label {
                        text: qsTr("%1 episodes")
                            .arg(EpisodeLibrary.currentEntry.totalCount ?? 0)
                        color: MementoPalette.placeholderText
                        font.pixelSize: 10
                    }

                    Label {
                        text: qsTr("%1 watched")
                            .arg(EpisodeLibrary.currentEntry.watchedCount ?? 0)
                        color: MementoPalette.placeholderText
                        font.pixelSize: 10
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: EpisodeLibrary.currentEntry.type === "torrent"
                        text: qsTr("%1 · %2 peers · %3% cached")
                            .arg(root.formatRate(
                                EpisodeLibrary.currentEntry.downloadRate ?? 0))
                            .arg(EpisodeLibrary.currentEntry.peers ?? 0)
                            .arg(Math.round(
                                (EpisodeLibrary.currentEntry.downloadProgress ?? 0) * 100))
                        horizontalAlignment: Text.AlignRight
                        color: MementoPalette.placeholderText
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 14
            Layout.rightMargin: 10
            Layout.topMargin: 12
            Layout.bottomMargin: 5
            visible: EpisodeLibrary.currentIndex >= 0

            Label {
                Layout.fillWidth: true
                text: qsTr("EPISODES")
                color: MementoPalette.placeholderText
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1.2
            }

            Label {
                text: qsTr("%1 total").arg(EpisodeLibrary.episodes.length)
                color: MementoPalette.placeholderText
                font.pixelSize: 10
            }
        }

        TextField {
            id: episodeSearch

            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            visible: EpisodeLibrary.episodes.length > 8
            placeholderText: qsTr("Search episodes")
            selectByMouse: true
            leftPadding: 10
            rightPadding: 10
        }

        Label {
            Layout.fillWidth: true
            Layout.leftMargin: 18
            Layout.rightMargin: 18
            Layout.topMargin: 16
            visible: EpisodeLibrary.currentIndex >= 0 &&
                     EpisodeLibrary.currentEntry.type === "torrent" &&
                     !(EpisodeLibrary.currentEntry.ready ?? false)
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("Loading the torrent’s episode list. Nothing downloads until you play an episode.")
            color: MementoPalette.placeholderText
        }

        ListView {
            id: episodeList

            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 6
            spacing: 2
            clip: true
            model: EpisodeLibrary.episodes

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: ItemDelegate {
                id: episodeDelegate

                required property var modelData
                required property int index

                readonly property bool searchMatch:
                    !episodeSearch.visible || episodeSearch.text.length === 0 ||
                    modelData.title.toLowerCase().includes(
                        episodeSearch.text.toLowerCase()) ||
                    modelData.relativePath.toLowerCase().includes(
                        episodeSearch.text.toLowerCase()) ||
                    String(modelData.number).includes(episodeSearch.text)
                readonly property bool playing: modelData.path.length > 0 &&
                    root.player.state.path === modelData.path

                width: ListView.view.width
                height: searchMatch ? 52 : 0
                visible: searchMatch
                enabled: modelData.available
                leftPadding: 8
                rightPadding: 6
                highlighted: playing
                onClicked: root.playEpisode(index)

                background: Rectangle {
                    radius: 5
                    color: episodeDelegate.playing ?
                        root.alpha(MementoPalette.accent, 0.16) :
                        (episodeDelegate.hovered ?
                            root.alpha(MementoPalette.text, 0.065) :
                            "transparent")
                }

                contentItem: RowLayout {
                    spacing: 9

                    Label {
                        Layout.preferredWidth: 30
                        horizontalAlignment: Text.AlignRight
                        text: modelData.number
                        color: episodeDelegate.playing ?
                            MementoPalette.accent : MementoPalette.placeholderText
                        font.bold: episodeDelegate.playing
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Label {
                            Layout.fillWidth: true
                            text: (episodeDelegate.playing ? "▶  " : "") +
                                modelData.title
                            textFormat: Text.PlainText
                            font.bold: episodeDelegate.playing
                            font.strikeout: modelData.watched
                            elide: Text.ElideMiddle
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                visible: modelData.relativePath !== modelData.title
                                text: modelData.relativePath
                                textFormat: Text.PlainText
                                color: MementoPalette.placeholderText
                                font.pixelSize: 9
                                elide: Text.ElideMiddle
                            }

                            Label {
                                visible: modelData.size > 0
                                text: root.formatSize(modelData.size)
                                color: MementoPalette.placeholderText
                                font.pixelSize: 9
                            }
                        }
                    }

                    Label {
                        visible: !modelData.available
                        text: EpisodeLibrary.currentEntry.type === "torrent" ?
                            qsTr("LOADING") : qsTr("MISSING")
                        color: MementoPalette.placeholderText
                        font.pixelSize: 9
                        font.bold: true
                    }

                    CheckBox {
                        checked: modelData.watched
                        enabled: true
                        focusPolicy: Qt.NoFocus
                        onClicked: EpisodeLibrary.setEpisodeWatched(index, checked)

                        ToolTip.visible: hovered
                        ToolTip.text: checked ?
                            qsTr("Mark unwatched") : qsTr("Mark watched")
                    }
                }
            }
        }
    }

    FileDialog {
        id: torrentDialog
        currentFolder: Utils.getFileOpenDirectory(
            MementoSettings.behaviorFileOpenDirectory)
        fileMode: FileDialog.OpenFile
        title: qsTr("Add Torrent to Library")
        nameFilters: [qsTr("Torrent Files (*.torrent)"), qsTr("All Files (*.*)")]
        onAccepted: {
            const index = EpisodeLibrary.addTorrent(selectedFile);
            if (index < 0)
            {
                root.showError();
            }
        }
    }

    Dialog {
        id: magnetDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Add Magnet Link")
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextArea {
            id: magnetText
            width: Math.min(560, root.width - 48)
            implicitHeight: 110
            placeholderText: qsTr("Paste a magnet:?xt=urn:btih:… link")
            wrapMode: TextEdit.WrapAnywhere
            selectByMouse: true
        }

        onOpened: {
            magnetText.text = "";
            magnetText.forceActiveFocus();
        }
        onAccepted: {
            if (EpisodeLibrary.addMagnet(magnetText.text) < 0)
            {
                root.showError();
            }
        }
    }

    FolderDialog {
        id: addFolderDialog
        currentFolder: Utils.getFileOpenDirectory(
            MementoSettings.behaviorFileOpenDirectory)
        title: qsTr("Add Episode Folder to Library")
        onAccepted: {
            if (EpisodeLibrary.addFolder(selectedFolder) < 0)
            {
                root.showError();
            }
        }
    }

    FolderDialog {
        id: contentFolderDialog
        currentFolder: EpisodeLibrary.currentEntry.folder?.length > 0 ?
                           EpisodeLibrary.currentEntry.folder :
                           Utils.getFileOpenDirectory(
                               MementoSettings.behaviorFileOpenDirectory)
        title: qsTr("Choose Episode Folder")
        onAccepted: {
            if (!EpisodeLibrary.setContentFolder(
                    EpisodeLibrary.currentIndex, selectedFolder))
            {
                root.showError();
            }
        }
    }

    Dialog {
        id: removeDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Remove from Library?")
        standardButtons: Dialog.Yes | Dialog.Cancel

        ColumnLayout {
            width: Math.min(implicitWidth, 460)

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Remove “%1” from Memento's library?")
                    .arg(EpisodeLibrary.currentEntry.title ?? "")
                textFormat: Text.PlainText
            }

            CheckBox {
                id: deleteCacheCheck
                visible: EpisodeLibrary.currentEntry.type === "torrent"
                text: qsTr("Also delete cached torrent data")
            }
        }

        onOpened: deleteCacheCheck.checked = false
        onAccepted: EpisodeLibrary.removeEntry(
            EpisodeLibrary.currentIndex, deleteCacheCheck.checked)
    }

    Dialog {
        id: errorDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Library Error")
        standardButtons: Dialog.Ok

        Label {
            width: Math.min(implicitWidth, 460)
            wrapMode: Text.Wrap
            text: EpisodeLibrary.lastError
            textFormat: Text.PlainText
        }
    }
}
