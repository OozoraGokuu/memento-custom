import QtCore
import QtQml.Models
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Ripose.Memento

MenuBar {
    id: root

    required property MpvPlayer player

    /* true if subtitles should be shown, false if they should be hidden */
    property alias showSubtitles: actionShowSubtitles.checked

    /* true if any children are under the cursor, false otherwise */
    readonly property bool anyHovered: {
        if (hovered)
        {
            return true;
        }
        for (let i = 0; i < root.contentChildren.length; ++i)
        {
            if (root.contentChildren[i].highlighted)
            {
                return true;
            }
        }
        return false;
    }

    /* The minimum width of each top level menu */
    property int minimumMenuWidth: 225

    readonly property bool currentEpisodeWatched: {
        /* Access the count so this binding refreshes when watched state changes. */
        const watchedRevision = EpisodeLibrary.watchedCount;
        return EpisodeLibrary.isWatched(root.player.state.path);
    }

    signal ocrModeRequested()
    signal jimakuSearchRequested()
    signal nextSubtitleSearchRequested()
    signal torrentSearchRequested()

    Connections {
        target: root.player.state

        function onTimePositionChanged(position) {
            const duration = root.player.state.duration;
            if (duration > 0 && position >= duration * 0.9)
            {
                EpisodeLibrary.setWatched(root.player.state.path, true);
            }
        }

        function onPathChanged() {
            EpisodeLibrary.recordPlayed(root.player.state.path);
        }
    }

    /**
     * Turns a track into a display string.
     * @param track The track to turn into a display string.
     * @return The display string of the track.
     */
    function makeTrackName(track) {
        let name = qsTr("Track %1").arg(track.id);
        if (track.language)
        {
            name += ` [${track.language}]`;
        }
        if (track.title)
        {
            name += ` - ${track.title}`;
        }
        return name;
    }

    Component.onCompleted: {
        if (Features.isWindows)
        {
            root.background = windowsBackground;
        }
    }

    resources: [
        Page {
            id: windowsBackground
        }
    ]

    Clipboard {
        id: clipboard
    }

    Menu {
        title: qsTr("&Media")
        width: Math.max(contentWidth + leftPadding + rightPadding, root.minimumMenuWidth)

        Action {
            text: qsTr("&Open File...")
            shortcut: MementoSettings.keybinds.profile?.openFile
            onShortcutChanged: {
                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    checkable = !checkable;
                    checkable = !checkable;
                }
            }
            onTriggered: loadFileDialog.open()
        }

        Action {
            text: qsTr("Open &Episode Folder...")
            onTriggered: loadFolderDialog.open()
        }

        Action {
            text: qsTr("Add &Torrent to Library...")
            onTriggered: loadTorrentDialog.open()
        }

        Action {
            text: qsTr("Add &Magnet Link to Library...")
            onTriggered: loadMagnetDialog.open()
        }

        Action {
            text: qsTr("Find Anime &Streams...")
            shortcut: "Ctrl+Shift+N"
            onTriggered: root.torrentSearchRequested()
        }

        Action {
            text: qsTr("Show Media &Library")
            checkable: true
            checked: MementoSettings.windowLibrary
            onTriggered: MementoSettings.windowLibrary = checked
        }

        Menu {
            id: chooseEpisodeMenu
            title: qsTr("&Choose Episode")
            enabled: EpisodeLibrary.totalCount > 0

            Instantiator {
                model: EpisodeLibrary.episodes

                delegate: Action {
                    required property var modelData
                    required property int index

                    /* The count refreshes labels when watched state changes. */
                    readonly property int watchedRevision: EpisodeLibrary.watchedCount
                    readonly property bool current:
                        EpisodeLibrary.containsFile(root.player.state.path) &&
                        root.player.state.path === modelData.path

                    text: {
                        const playingMarker = current ? "▶ " : "";
                        const watchedMarker = watchedRevision >= 0 &&
                                              modelData.watched ? "✓ " : "";
                        return playingMarker + watchedMarker + modelData.title;
                    }
                    enabled: modelData.available
                    onTriggered: {
                        const path = EpisodeLibrary.episodePath(index);
                        if (path.length > 0 && root.player.controller.loadFile(
                            path, false, [
                                "demuxer-max-bytes=32MiB",
                                "demuxer-max-back-bytes=8MiB",
                                "cache-pause-initial=no"
                            ]))
                        {
                            root.player.controller.play();
                        }
                    }
                }

                onObjectAdded: function(index, object) {
                    chooseEpisodeMenu.insertAction(index, object);
                }
                onObjectRemoved: function(index, object) {
                    chooseEpisodeMenu.removeAction(object);
                }
            }
        }

        MenuSeparator {}

        Menu {
            title: EpisodeLibrary.currentIndex >= 0 ?
                       qsTr("Episodes Watched: %1 / %2")
                           .arg(EpisodeLibrary.watchedCount)
                           .arg(EpisodeLibrary.totalCount) :
                       qsTr("Episodes Watched")
            enabled: EpisodeLibrary.currentIndex >= 0

            Action {
                text: EpisodeLibrary.currentEntry.type === "torrent" ?
                          EpisodeLibrary.currentEntry.torrentState :
                      EpisodeLibrary.folder.length > 0 ?
                          qsTr("Folder: %1").arg(EpisodeLibrary.folderName) :
                          qsTr("Episode folder not selected")
                enabled: false
            }

            Action {
                text: qsTr("Mark Current Episode Watched")
                enabled: EpisodeLibrary.containsFile(root.player.state.path) &&
                         !root.currentEpisodeWatched
                onTriggered: EpisodeLibrary.setWatched(root.player.state.path, true)
            }

            Action {
                text: qsTr("Mark Current Episode Unwatched")
                enabled: EpisodeLibrary.containsFile(root.player.state.path) &&
                         root.currentEpisodeWatched
                onTriggered: EpisodeLibrary.setWatched(root.player.state.path, false)
            }

            Action {
                text: EpisodeLibrary.currentEntry.type === "torrent" ?
                          qsTr("Refresh Torrent") : qsTr("Rescan Folder")
                onTriggered: EpisodeLibrary.rescan()
            }
        }

        MenuSeparator {}

        Action {
            text: qsTr("&Open URL...")
            shortcut: MementoSettings.keybinds.profile?.openUrl
            onShortcutChanged: {
                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    checkable = !checkable;
                    checkable = !checkable;
                }
            }
            onTriggered: loadUrlDialog.open()
        }

        Menu {
            id: recentMenu
            title: qsTr("&Recent")
            enabled: MementoSettings.recentFiles.length > 0

            Instantiator {
                model: MementoSettings.recentFiles
                delegate: Action {
                    text: modelData
                    onTriggered: root.player.controller.loadFile(modelData)
                }

                onObjectAdded: function(index, object) {
                    recentMenu.insertAction(index, object);

                    /* This is a hack to force a refresh after adding the item.
                     * For some reason the items are all added with blank text,
                     * until something causes a visual update. */
                    if (Features.isMacos)
                    {
                        object.checkable = !object.checkable;
                        object.checkable = !object.checkable;
                    }
                }
                onObjectRemoved: function(index, object) {
                    recentMenu.removeAction(object);
                }
            }

            MenuSeparator {}

            Action {
                text: qsTr("&Clear Recents")
                onTriggered: MementoSettings.recentFilesClear()
            }
        }
    }

    Menu {
        id: audioMenu
        title: qsTr("&Audio")
        width: Math.max(contentWidth + leftPadding + rightPadding, root.minimumMenuWidth)

        ActionGroup {
            id: audioTrackGroup
            exclusive: true
        }

        Action {
            checkable: true
            checked: root.player.state.aid === 0
            ActionGroup.group: audioTrackGroup
            text: qsTr("None")
            onTriggered: root.player.controller.setAid(0)
        }

        Instantiator {
            model: root.player.state.audioTracks
            delegate: Action {
                checkable: true
                checked: root.player.state.aid === modelData.id
                ActionGroup.group: audioTrackGroup
                text: root.makeTrackName(modelData)
                onTriggered: root.player.controller.setAid(modelData.id)
            }

            onObjectAdded: function(index, object) {
                audioMenu.insertAction(index + 1, object);

                /* This is a hack to force a refresh after adding the item.
                 * For some reason the items are all added with blank text,
                 * until something causes a visual update. */
                if (Features.isMacos)
                {
                    object.checkable = !object.checkable;
                    object.checkable = !object.checkable;
                }
            }
            onObjectRemoved: function(index, object) {
                audioMenu.removeAction(object);
            }
        }
    }

    Menu {
        id: subtitleMenu
        title: qsTr("&Subtitle")
        width: Math.max(contentWidth + leftPadding + rightPadding, root.minimumMenuWidth)

        Action {
            checkable: true
            checked: MementoSettings.behaviorSubtitlePause
            text: qsTr("&Auto Pause")
            shortcut: MementoSettings.keybinds.profile?.subtitleAutoPause
            onShortcutChanged: {
                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    checkable = !checkable;
                    checkable = !checkable;
                }
            }
            onTriggered: MementoSettings.behaviorSubtitlePause = checked
        }

        Menu {
            title: qsTr("&Size and Position")

            Action {
                enabled: MementoSettings.interfaceSubtitleScale < 1.0
                text: qsTr("&Increase Size")
                shortcut: MementoSettings.keybinds.profile?.subtitleIncreaseSize
                onShortcutChanged: {
                    /* Hack to force a refresh */
                    if (Features.isMacos)
                    {
                        checkable = !checkable;
                        checkable = !checkable;
                    }
                }
                onTriggered: MementoSettings.interfaceSubtitleScale =
                             Math.min(MementoSettings.interfaceSubtitleScale + 0.005, 1.0)
            }

            Action {
                enabled: MementoSettings.interfaceSubtitleScale > 0.001
                text: qsTr("&Decrease Size")
                shortcut: MementoSettings.keybinds.profile?.subtitleDecreaseSize
                onShortcutChanged: {
                    /* Hack to force a refresh */
                    if (Features.isMacos)
                    {
                        checkable = !checkable;
                        checkable = !checkable;
                    }
                }
                onTriggered: MementoSettings.interfaceSubtitleScale =
                             Math.max(MementoSettings.interfaceSubtitleScale - 0.005, 0.001)
            }

            Action {
                enabled: MementoSettings.interfaceSubtitleOffset < 1.0
                text: qsTr("&Move Up")
                shortcut: MementoSettings.keybinds.profile?.subtitleMoveUp
                onShortcutChanged: {
                    /* Hack to force a refresh */
                    if (Features.isMacos)
                    {
                        checkable = !checkable;
                        checkable = !checkable;
                    }
                }
                onTriggered: MementoSettings.interfaceSubtitleOffset =
                             Math.min(MementoSettings.interfaceSubtitleOffset + 0.005, 1.0)
            }

            Action {
                enabled: MementoSettings.interfaceSubtitleOffset > 0
                text: qsTr("&Move Down")
                shortcut: MementoSettings.keybinds.profile?.subtitleMoveDown
                onShortcutChanged: {
                    /* Hack to force a refresh */
                    if (Features.isMacos)
                    {
                        checkable = !checkable;
                        checkable = !checkable;
                    }
                }
                onTriggered: MementoSettings.interfaceSubtitleOffset =
                             Math.max(MementoSettings.interfaceSubtitleOffset - 0.005, 0.0)
            }
        }

        Action {
            id: actionShowSubtitles
            text: qsTr("&Show Subtitles")
            checkable: true
            checked: true
            shortcut: MementoSettings.keybinds.profile?.subtitleShow
            onShortcutChanged: {
                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    checkable = !checkable;
                    checkable = !checkable;
                }
            }
        }

        MenuSeparator {}

        Action {
            text: qsTr("&Add Subtitle...")
            onTriggered: loadSubtitleDialog.open()
        }

        Action {
            text: JimakuClient.busy ?
                      qsTr("Fetching from &Jimaku…") :
                      qsTr("Fetch from &Jimaku")
            enabled: !JimakuClient.busy && root.player.state.path.length > 0
            shortcut: "Ctrl+Shift+J"
            onTriggered: JimakuClient.fetchForCurrentMedia()
        }

        Action {
            text: qsTr("Fetch subtitle from previous search — episode %1")
                .arg((EpisodeLibrary.lastSubtitleSearch.episode ?? -1) + 1)
            enabled: (EpisodeLibrary.lastSubtitleSearch.episode ?? -1) >= 0 &&
                     root.player.state.path.length > 0 && !JimakuClient.busy && !KitsunekkoClient.busy
            onTriggered: root.nextSubtitleSearchRequested()
        }

        Action {
            text: qsTr("Search Japanese &Subtitles...")
            shortcut: "Ctrl+Shift+K"
            onTriggered: root.jimakuSearchRequested()
        }

        Menu {
            id: secondarySubtitleMenu
            title: qsTr("&Second Track")

            ActionGroup {
                id: secondarySubtitleTrackGroup
                exclusive: true
            }

            Action {
                checkable: true
                checked: root.player.state.secondarySid === 0
                ActionGroup.group: secondarySubtitleTrackGroup
                text: qsTr("None")
                onTriggered: root.player.controller.setSecondarySid(0)
            }

            Instantiator {
                model: root.player.state.subtitleTracks
                delegate: Action {
                    checkable: true
                    checked: root.player.state.secondarySid === modelData.id
                    enabled: root.player.state.sid !== modelData.id
                    ActionGroup.group: secondarySubtitleTrackGroup
                    text: root.makeTrackName(modelData)
                    onTriggered: root.player.controller.setSecondarySid(modelData.id)
                }

                onObjectAdded: function(index, object) {
                    secondarySubtitleMenu.insertAction(index + 1, object);

                    /* This is a hack to force a refresh after adding the item.
                     * For some reason the items are all added with blank text,
                     * until something causes a visual update. */
                    if (Features.isMacos)
                    {
                        object.checkable = !object.checkable;
                        object.checkable = !object.checkable;
                    }
                }
                onObjectRemoved: function(index, object) {
                    secondarySubtitleMenu.removeAction(object);
                }
            }
        }

        MenuSeparator {}

        ActionGroup {
            id: subtitleTrackGroup
            exclusive: true
        }

        Action {
            id: subtitleNoneAction

            readonly property int index: {
                for (let i = 0; i < subtitleMenu.count; ++i)
                {
                    if (subtitleMenu.itemAt(i).action === subtitleNoneAction)
                    {
                        return i;
                    }
                }
                return -1;
            }

            checkable: true
            checked: root.player.state.sid === 0
            ActionGroup.group: subtitleTrackGroup
            text: qsTr("None")
            onTriggered: root.player.controller.setSid(0)
        }

        Instantiator {
            model: root.player.state.subtitleTracks
            delegate: Action {
                checkable: true
                checked: root.player.state.sid === modelData.id
                enabled: root.player.state.secondarySid !== modelData.id
                ActionGroup.group: subtitleTrackGroup
                text: root.makeTrackName(modelData)
                onTriggered: root.player.controller.setSid(modelData.id)
            }

            onObjectAdded: function(index, object) {
                subtitleMenu.insertAction(subtitleNoneAction.index + index + 1, object);

                /* This is a hack to force a refresh after adding the item.
                 * For some reason the items are all added with blank text,
                 * until something causes a visual update. */
                if (Features.isMacos)
                {
                    object.checkable = !object.checkable;
                    object.checkable = !object.checkable;
                }
            }
            onObjectRemoved: function(index, object) {
                subtitleMenu.removeAction(object);
            }
        }
    }

    Menu {
        id: toolsMenu
        title: qsTr("&Tools")
        width: Math.max(contentWidth + leftPadding + rightPadding, root.minimumMenuWidth)

        Action {
            text: qsTr("&Show Search")
            checkable: true
            checked: MementoSettings.windowSearch
            shortcut: MementoSettings.keybinds.profile?.showSearch
            onShortcutChanged: {
                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    checkable = !checkable;
                    checkable = !checkable;
                }
            }
            onTriggered: MementoSettings.windowSearch = checked
        }

        Action {
            text: qsTr("&Show Subtitle List")
            checkable: true
            checked: MementoSettings.windowSubtitleList
            shortcut: MementoSettings.keybinds.profile?.showSubtitleList
            onShortcutChanged: {
                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    checkable = !checkable;
                    checkable = !checkable;
                }
            }
            onTriggered: MementoSettings.windowSubtitleList = checked
        }

        Instantiator {
            /* Hide the action if OCR is disabled */
            model: Features.ocr && MementoSettings.ocrEnabled ? 1 : 0

            delegate: Action {
                text: qsTr("&Start OCR")
                shortcut: MementoSettings.keybinds.profile?.startOcr
                onShortcutChanged: {
                    /* Hack to force a refresh */
                    if (Features.isMacos)
                    {
                        checkable = !checkable;
                        checkable = !checkable;
                    }
                }
                onTriggered: root.ocrModeRequested()
            }

            onObjectAdded: function(index, object) {
                toolsMenu.insertAction(toolsMenu.count, object);

                /* Hack to force a refresh */
                if (Features.isMacos)
                {
                    object.checkable = !object.checkable;
                    object.checkable = !object.checkable;
                }
            }
            onObjectRemoved: function(index, object) {
                toolsMenu.removeAction(object);
            }
        }
    }

    Menu {
        id: settingsMenu
        title: qsTr("&Settings")
        width: Math.max(contentWidth + leftPadding + rightPadding, root.minimumMenuWidth)

        Menu {
            id: ankiProfileMenu
            title: qsTr("&Anki Profile")
            enabled: AnkiConfig.enabled

            ActionGroup {
                id: ankiProfileGroup
                exclusive: true
            }

            Instantiator {
                model: AnkiConfig.profiles
                delegate: Action {
                    checkable: true
                    checked: AnkiConfig.profile === modelData
                    ActionGroup.group: ankiProfileGroup
                    text: modelData.name
                    onTriggered: AnkiConfig.setProfile(modelData.name)
                }

                onObjectAdded: function(index, object) {
                    ankiProfileMenu.insertAction(index, object);

                    /* This is a hack to force a refresh after adding the item.
                     * For some reason the items are all added with blank text,
                     * until something causes a visual update. */
                    if (Features.isMacos)
                    {
                        object.checkable = !object.checkable;
                        object.checkable = !object.checkable;
                    }
                }
                onObjectRemoved: function(index, object) {
                    ankiProfileMenu.removeAction(object);
                }
            }
        }

        Action {
            text: qsTr("&Options")
            shortcut: "Ctrl+,"
            onTriggered: optionsWindow.show()
        }

        Action {
            text: qsTr("&Open Config")
            onTriggered: Features.isWindows ?
                             Qt.openUrlExternally(`file:///${MementoPaths.config}`) :
                             Qt.openUrlExternally(`file://${MementoPaths.config}`)
        }

        Action {
            text: qsTr("&Check for Updates")
            onTriggered: updateDialog.check(false)
        }

        Action {
            text: qsTr("&About Memento")
            onTriggered: aboutWindow.show()
        }
    }

    FileDialog {
        id: loadFileDialog
        currentFolder: Utils.getFileOpenDirectory(MementoSettings.behaviorFileOpenDirectory)
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            qsTr("Video Files (%1)").arg("*.webm *.mkv *.vob *.ogv *.ogg *.avi *.mov *.qt *.mp4 *.m4p *.m4v *.mpg *.mp2 *.mpeg *.mpe *.mpv)"),
            qsTr("All Files (*.*)")
        ]
        onAccepted: root.player.controller.loadFile(selectedFiles)
    }

    FolderDialog {
        id: loadFolderDialog
        currentFolder: EpisodeLibrary.folder.length > 0 ?
                           EpisodeLibrary.folder :
                           Utils.getFileOpenDirectory(MementoSettings.behaviorFileOpenDirectory)
        title: qsTr("Select Episode Folder")
        onAccepted: {
            const files = EpisodeLibrary.open(selectedFolder);
            if (files.length > 0)
            {
                root.player.controller.loadFile(files);
            }
            MementoSettings.windowLibrary = true;
        }
    }

    FileDialog {
        id: loadTorrentDialog
        currentFolder: Utils.getFileOpenDirectory(MementoSettings.behaviorFileOpenDirectory)
        fileMode: FileDialog.OpenFile
        title: qsTr("Add Torrent to Library")
        nameFilters: [qsTr("Torrent Files (*.torrent)"), qsTr("All Files (*.*)")]
        onAccepted: {
            const index = EpisodeLibrary.addTorrent(selectedFile);
            if (index < 0)
            {
                libraryErrorDialog.open();
                return;
            }
            MementoSettings.windowLibrary = true;
        }
    }

    Dialog {
        id: loadMagnetDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Add Magnet Link")
        standardButtons: Dialog.Ok | Dialog.Cancel

        TextArea {
            id: magnetText
            width: 520
            implicitHeight: 110
            placeholderText: qsTr("Paste a magnet:?xt=urn:btih:… link")
            wrapMode: TextEdit.WrapAnywhere
            selectByMouse: true
        }

        onOpened: {
            magnetText.text = clipboard.text();
            magnetText.selectAll();
        }
        onAccepted: {
            if (EpisodeLibrary.addMagnet(magnetText.text) < 0)
                libraryErrorDialog.open();
            else
                MementoSettings.windowLibrary = true;
        }
    }

    Dialog {
        id: libraryErrorDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("Could Not Add Library Item")
        standardButtons: Dialog.Ok

        Label {
            width: Math.min(implicitWidth, 480)
            wrapMode: Text.Wrap
            text: EpisodeLibrary.lastError
            textFormat: Text.PlainText
        }
    }

    Dialog {
        id: loadUrlDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        title: qsTr("Open Stream")
        onOpened: {
            urlTextField.text = clipboard.text();
            urlTextField.selectAll();
        }
        onClosed: urlTextField.text = ""
        onAccepted: root.player.controller.loadFile(urlTextField.text)

        TextField {
            id: urlTextField
            implicitWidth: 400
            focus: true
            placeholderText: qsTr("Enter URL")
            onAccepted: loadUrlDialog.accept()
        }
    }

    FileDialog {
        id: loadSubtitleDialog
        currentFolder: Utils.getFileOpenDirectory(MementoSettings.behaviorFileOpenDirectory)
        fileMode: FileDialog.OpenFile
        nameFilters: [
            qsTr("Subtitle Files (%1)").arg("*.ass *.idx *.lrc *.mks *.pgs *.rt *.scc *.smi *.srt *.ssa *.sub *.sup *.utf-8 *.utf *.utf8 *.vtt)"),
            qsTr("All Files (*.*)")
        ]
        title: qsTr("Select Subtitle File")
        onAccepted: root.player.controller.loadSubtitle(selectedFile)
    }

    UpdateDialog {
        id: updateDialog

        Component.onCompleted: {
            if (MementoSettings.applicationAutoUpdateCheck)
            {
                updateDialog.check(true);
            }
        }
    }

    OptionsWindow {
        id: optionsWindow
    }

    AboutWindow {
        id: aboutWindow
    }
}
