import QtQuick
import QtCore
import QtQuick.Window
import QtQuick.Controls
import Ripose.Memento

MpvPlayer {
    id: root

    readonly property bool oscVisible:
        !root.ocrMode && (
            controls.visible ||
            (!Features.isMacos && menu.visible)
        )
    readonly property bool oscHovered:
        !root.ocrMode && (
            subtitleHover.hovered ||
            secondarySubtitleHover.hovered ||
            (!Features.isMacos && menu.anyHovered) ||
            controlsHover.hovered
        )

    readonly property bool pause: root.state.pause

    readonly property var primarySubtitleTrack:
        root.subtitleTrackForId(root.state.sid)
    readonly property var secondarySubtitleTrack:
        root.subtitleTrackForId(root.state.secondarySid)
    readonly property bool primarySubtitleIsBitmap:
        root.primarySubtitleTrack?.bitmapSubtitle ?? false
    readonly property bool secondarySubtitleIsBitmap:
        root.secondarySubtitleTrack?.bitmapSubtitle ?? false

    property point cursorPosition: Qt.point(0, 0)

    Settings {
        id: movieSettings
        category: "MovieMode"
        property bool enabled: true
    }
    Settings {
        id: streamSettings
        category: "WindowShare"
        property bool includeTextSubtitles: true
    }
    readonly property bool streamSubtitlesCanBeHidden:
        !root.primarySubtitleIsBitmap && !root.secondarySubtitleIsBitmap
    readonly property bool privateSubtitlesActive:
        Features.isX11 && !streamSettings.includeTextSubtitles &&
        root.streamSubtitlesCanBeHidden
    onPrivateSubtitlesActiveChanged: root.synchronizeSubtitleRenderers()

    function updatePrivateSubtitleWindowPosition() {
        if (!root.Window.window) return;
        const position = root.mapToGlobal(0, 0);
        privatePrimarySubtitleWindow.x = Math.round(position.x);
        privatePrimarySubtitleWindow.y = Math.round(position.y + subtitleText.y);
        privateSecondarySubtitleWindow.x = Math.round(position.x);
        privateSecondarySubtitleWindow.y = Math.round(position.y + secondarySubtitleText.y);
    }
    onXChanged: root.updatePrivateSubtitleWindowPosition()
    onYChanged: root.updatePrivateSubtitleWindowPosition()
    onWidthChanged: root.updatePrivateSubtitleWindowPosition()
    onHeightChanged: root.updatePrivateSubtitleWindowPosition()
    Component.onCompleted: Qt.callLater(root.updatePrivateSubtitleWindowPosition)

    Connections {
        target: root.Window.window
        function onXChanged() { root.updatePrivateSubtitleWindowPosition(); }
        function onYChanged() { root.updatePrivateSubtitleWindowPosition(); }
        function onVisibilityChanged() { Qt.callLater(root.updatePrivateSubtitleWindowPosition); }
    }
    Connections {
        target: subtitleText
        function onYChanged() { root.updatePrivateSubtitleWindowPosition(); }
    }
    Connections {
        target: secondarySubtitleText
        function onYChanged() { root.updatePrivateSubtitleWindowPosition(); }
    }
    readonly property bool movieModeActive: movieSettings.enabled && root.state.fullscreen
    property bool screenshotSentencePending: false
    property string screenshotSentence: ""

    function playerShortcutAllowed() {
        let item = root.Window.window?.activeFocusItem;
        while (item) {
            if (item === subtitleText || item === root) return true;
            // Preserve Copy/Cut in editable fields and subtitle list selections.
            if (typeof item.selectAll === "function") return false;
            item = item.parent;
        }
        return false;
    }

    function copyCurrentSubtitle() {
        if (!subtitleText.text.trim()) return;
        subtitleClipboard.setText(subtitleText.text);
        root.controller.showText(qsTr("Subtitle copied"));
    }

    function copyScreenshotAndSentence() {
        if (root.screenshotSentencePending) {
            subtitleClipboard.setText(root.screenshotSentence);
            root.screenshotSentencePending = false;
            root.controller.showText(qsTr("Sentence copied"));
            return;
        }
        const result = root.controller.copySubtitleScreenshot();
        if (!result.ok) {
            root.controller.showText(qsTr("Could not capture the current video frame"));
            return;
        }
        root.screenshotSentence = root.cleanSubtitleText(result.text);
        root.screenshotSentencePending = root.screenshotSentence.trim().length > 0;
        root.controller.showText(root.screenshotSentencePending ?
            qsTr("Screenshot copied — press Ctrl+X again for the sentence") :
            qsTr("Screenshot copied — no subtitle text at this position"));
    }

    onMovieModeActiveChanged: {
        menuFadeIn.stop();
        menuFadeOut.stop();
        controlsFadeIn.stop();
        controlsFadeOut.stop();
        if (root.movieModeActive) {
            menu.visible = false;
            controls.visible = false;
        } else {
            root.showOsc();
            interactiveTimer.restart();
        }
    }

    property bool ocrMode: false
    property bool subtitleRendererReady: false

    signal auxiliarySearchRequested(string text)
    signal jimakuSearchRequested()
    signal nextSubtitleSearchRequested()
    signal torrentSearchRequested()
    signal statisticsRequested()
    signal aniListSearchRequested()

    enum OscVisibility
    {
        OscAuto,
        OscVisible,
        OscHidden
    }

    property int oscVisibility: Player.OscAuto

    /**
     * Shows the OSC with fade in.
     */
    function showOsc() {
        if (root.ocrMode || root.movieModeActive)
        {
            return;
        }

        if (root.oscVisibility === Player.OscHidden)
        {
            return;
        }

        if (!Features.isMacos)
        {
            menuFadeOut.stop();
            menuFadeIn.start();
        }
        controlsFadeOut.stop();
        controlsFadeIn.start();
    }

    /**
     * Hides the OSC with fade out.
     *
     * @param force Unconditionally hide the OSC if true.
     */
    function hideOsc(force) {
        if (!force && root.oscVisibility === Player.OscVisible)
        {
            return;
        }

        if (!Features.isMacos)
        {
            menuFadeIn.stop();
            menuFadeOut.start();
        }
        controlsFadeIn.stop();
        controlsFadeOut.start();
    }

    /**
     * Put the player into OCR mode.
     */
    function startOcrMode() {
        ocrOverlay.start();
    }

    /**
     * Get the player out of OCR mode.
     */
    function cancelOcrMode() {
        ocrOverlay.cancel();
    }

    /**
     * Handles mouse movement.
     */
    function mouseMoved(x, y) {
        root.controller.sendMouse(x, y);
        cursorTimer.restart();
    }

    /** Find the subtitle track currently assigned to an mpv subtitle slot. */
    function subtitleTrackForId(trackId) {
        const tracks = root.state.subtitleTracks;
        for (let i = 0; i < tracks.length; ++i)
        {
            if (tracks[i].id === trackId)
            {
                return tracks[i];
            }
        }
        return null;
    }

    /** Select an mpv or Memento renderer independently for each track. */
    function synchronizeSubtitleRenderers() {
        if (!root.subtitleRendererReady)
        {
            return;
        }
        const useMemento = MementoSettings.searchHideMpvSubs ||
            root.privateSubtitlesActive;
        const useMpvPrimary = root.ocrMode || !useMemento ||
            root.primarySubtitleIsBitmap;
        const useMpvSecondary = root.ocrMode || !useMemento ||
            root.secondarySubtitleIsBitmap;
        const playbackAllowsSubtitles = !MementoSettings.searchHideSubs ||
            root.state.pause;
        const primaryShown = root.ocrMode ||
            (playbackAllowsSubtitles && menu.showSubtitles &&
             (!MementoSettings.behaviorSubtitleCursorShow ||
              itemCursorSubtitleShow.show));
        const secondaryShown = root.ocrMode ||
            (playbackAllowsSubtitles && menu.showSubtitles &&
             (!MementoSettings.behaviorSecondarySubtitleCursorShow ||
              itemCursorSecondarySubtitleShow.show));
        root.controller.setSubtitleVisibility(
            useMpvPrimary && primaryShown);
        root.controller.setSecondarySubtitleVisibility(
            useMpvSecondary && secondaryShown && (!secondarySubtitleWindow.visible || root.secondarySubtitleIsBitmap));
    }

    onPrimarySubtitleIsBitmapChanged: root.synchronizeSubtitleRenderers()
    onSecondarySubtitleIsBitmapChanged: root.synchronizeSubtitleRenderers()
    onOcrModeChanged: root.synchronizeSubtitleRenderers()

    Connections {
        target: root

        function onInitialized() {
            root.subtitleRendererReady = true;
            root.synchronizeSubtitleRenderers();
        }
    }

    /** Apply the same text cleanup to both Memento subtitle tracks. */
    function cleanSubtitleText(source) {
        let text = MementoSettings.filterSubtitleText(source || "", MementoSettings.searchRemoveRegex);
        if (MementoSettings.searchReplaceNewlines)
            text = text.split("\n").join(
                MementoSettings.searchReplaceNewlinesWith);
        return text;
    }

    onFileLoaded: {
        root.screenshotSentencePending = false;
        root.synchronizeSubtitleRenderers();
        JimakuClient.cancel();
        KitsunekkoClient.cancel();
        if (JimakuClient.autoFetch)
        {
            Qt.callLater(function() {
                const link = EpisodeLibrary.subtitleLinkForFile(root.state.path);
                if (link.provider === "kitsunekko")
                    KitsunekkoClient.fetchLinkedForCurrentMedia();
                else if (JimakuClient.apiKeyConfigured &&
                         (!link.provider || link.provider === "jimaku"))
                    JimakuClient.fetchForCurrentMedia();
            });
        }
    }

    Keys.onPressed: function(event) {
        if (root.ocrMode && event.key === Qt.Key_Escape)
        {
            root.cancelOcrMode();
            event.accepted = true;
            return;
        }
        if (event.modifiers === Qt.NoModifier &&
            (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            if (!event.isAutoRepeat)
                controls.seekAdjacentSubtitle(event.key === Qt.Key_Left ? -1 : 1);
            event.accepted = true;
            return;
        }
        root.controller.sendKeyPress(event.key, event.modifiers);
    }

    onCursorPositionChanged: root.mouseMoved(root.cursorPosition.x, root.cursorPosition.y)

    onOscVisibilityChanged: {
        switch (root.oscVisibility)
        {
        case Player.OscAuto:
            interactiveTimer.restart();
            break;

        case Player.OscVisible:
            root.showOsc();
            break;

        case Player.OscHidden:
            root.hideOsc();
            break;
        }
    }

    Action {
        id: cycleOscVisibilityAction
        shortcut: MementoSettings.keybinds.profile?.oscVisibility
        onTriggered: {
            const oscVisibilityText = qsTr("OSC Visibility: %1");

            switch (root.oscVisibility)
            {
            case Player.OscAuto:
                root.oscVisibility = Player.OscVisible;
                root.controller.showText(oscVisibilityText.arg(qsTr("Visible")));
                break;

            case Player.OscVisible:
                root.oscVisibility = Player.OscHidden;
                root.controller.showText(oscVisibilityText.arg(qsTr("Hidden")));
                break;

            case Player.OscHidden:
                root.oscVisibility = Player.OscAuto;
                root.controller.showText(oscVisibilityText.arg(qsTr("Auto")));
                break;
            }
        }
    }

    Action {
        id: sendToMigakuAction
        enabled: MigakuClient.enabled && !MigakuClient.busy &&
                 root.state.pause
        shortcut: "Ctrl+Shift+M"
        onTriggered: MigakuClient.exportCurrentSubtitle()
    }

    Action {
        id: copySubtitleSelectionAction
        objectName: "copyCurrentSubtitleAction"
        enabled: root.playerShortcutAllowed() && subtitleText.text.trim().length > 0
        shortcut: StandardKey.Copy
        onTriggered: root.copyCurrentSubtitle()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        autoRepeat: false
        enabled: root.playerShortcutAllowed() && root.state.path.length > 0
        onActivated: {
            const info = EpisodeLibrary.playbackInfo(root.state.path);
            const result = root.controller.saveScreenshot(
                info.title || "", info.filename || root.state.path);
            root.controller.showText(result.ok ?
                qsTr("Screenshot saved: %1").arg(result.path) : result.error);
        }
    }

    Shortcut {
        sequence: "Ctrl+X"
        autoRepeat: false
        enabled: root.playerShortcutAllowed() && root.state.path.length > 0
        onActivated: root.copyScreenshotAndSentence()
    }

    Shortcut {
        sequence: "Escape"
        enabled: root.movieModeActive && !root.ocrMode && root.playerShortcutAllowed()
        onActivated: root.controller.setFullscreen(false)
    }

    Connections {
        target: MigakuClient

        function onFilesExported(baseName) {
            root.controller.showText(
                qsTr("Saved image and sentence audio: %1").arg(baseName));
        }

        function onExportFailed(error) {
            root.controller.showText(
                qsTr("Migaku export failed: %1").arg(error));
        }
    }

    Connections {
        target: KitsunekkoClient
        function onSubtitleAttached(fileName, entryName) {
            root.controller.showText(qsTr("Linked subtitle loaded: %1").arg(fileName));
        }
        function onFailed(error) {
            root.controller.showText(qsTr("Kitsunekko: %1").arg(error));
        }
    }

    Connections {
        target: JimakuClient

        function onSubtitleAttached(fileName, entryName) {
            root.controller.showText(
                qsTr("Jimaku subtitle attached: %1").arg(fileName));
        }

        function onFailed(error) {
            root.controller.showText(
                qsTr("Jimaku: %1").arg(error));
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        propagateComposedEvents: true
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        cursorShape: root.oscHovered ? Qt.ArrowCursor : cursorTimer.cursorShape
        enabled: !root.ocrMode

        onPositionChanged: function(event) {
            root.cursorPosition = Qt.point(event.x, event.y);
            event.accepted = false;
        }

        onClicked: function(event) {
            /* This hack makes it so keys are still handled and the player has focus */
            root.forceActiveFocus();
            root.focus = false;
            root.forceActiveFocus();

            definitionPopup.clearResults();
            root.controller.sendMouseButton(event.x, event.y, event.button, true);
            event.accepted = false;
        }

        onDoubleClicked: function(event) {
            root.controller.sendMouseButton(event.x, event.y, event.button, false);
            event.accepted = false;
        }
    }

    /* This handler is needed because the itemCursor Items block
     * onPositionChanged in MouseArea */
    HoverHandler {
        id: hoverHandler

        readonly property point position: hoverHandler.point.position

        onPositionChanged: root.cursorPosition = Qt.point(position.x, position.y)
    }

    WheelHandler {
        enabled: !root.ocrMode
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: function(event) {
            root.controller.sendWheel(event.x, event.y, event.angleDelta);
        }
    }

    Timer {
        id: interactiveTimer
        interval: MementoSettings.behaviorOscDuration
        repeat: false
        running: false
        onTriggered: {
            if (!root.oscHovered && !definitionPopup.visible)
            {
                root.hideOsc();
            }
        }
    }

    MoveTimer {
        id: minMouseMoveTimer
        running: !root.ocrMode
        minMovement: MementoSettings.behaviorOscMinMove
        cursorPosition: root.cursorPosition
        onMouseMoved: {
            interactiveTimer.restart();
            if (!root.oscVisible)
            {
                root.showOsc();
            }
        }
    }

    CursorTimer {
        id: cursorTimer
        player: root
    }

    onOscHoveredChanged: {
        interactiveTimer.restart();
        cursorTimer.restart();
    }

    /* Inhibit the screensaver during playback */
    Screensaver {
        id: screensaver
    }
    onPauseChanged: {
        root.synchronizeSubtitleRenderers();
        if (root.state.pause)
        {
            screensaver.uninhibit();
        }
        else
        {
            screensaver.inhibit(qsTr("Playing media"));
        }
    }

    /* Implements subtitle cursor show */
    Item {
        id: itemCursorSubtitleShow

        readonly property bool show: hoverCursorSubtitleShow.hovered ||
                                     subtitleHover.hovered ||
                                     subtitleText.hoverIndex !== -1 || // This prevents flashing on edges
                                     controlsHover.hovered

        anchors {
            left: root.left
            right: root.right
            bottom: root.bottom
        }
        height: root.height * 0.25
        focusPolicy: Qt.NoFocus
        visible: MementoSettings.behaviorSubtitleCursorShow

        HoverHandler {
            id: hoverCursorSubtitleShow
        }

        onShowChanged: root.synchronizeSubtitleRenderers()
        onVisibleChanged: root.synchronizeSubtitleRenderers()
    }

    /* Implements secondary subtitle cursor show */
    Item {
        id: itemCursorSecondarySubtitleShow

        readonly property bool show:
            hoverCursorSecondarySubtitleShow.hovered ||
            (!Features.isMacos && menu.anyHovered)

        anchors {
            left: root.left
            right: root.right
            top: root.top
        }
        height: root.height * 0.25
        focusPolicy: Qt.NoFocus
        visible: MementoSettings.behaviorSecondarySubtitleCursorShow

        HoverHandler {
            id: hoverCursorSecondarySubtitleShow
        }

        /**
         * Toggles secondary subtitle visibility.
         */
        function updateSeconarySubtitleVisibility() {
            root.synchronizeSubtitleRenderers();
        }

        onShowChanged: updateSeconarySubtitleVisibility()
        onVisibleChanged: updateSeconarySubtitleVisibility()
    }

    Popup {
        id: definitionPopup

        property point cursorAtSearch: Qt.point(0, 0)
        property int lastHoverIndex: -1

        readonly property int playerWidth: root.width
        readonly property int playerHeight: root.height
        readonly property bool paused: root.state.pause
        readonly property real position: controls.position

        readonly property bool shouldOpen: dictionarySearch.terms.length > 0 || dictionarySearch.kanji

        x: {
            let idealPosition = cursorAtSearch.x - (width / 2);
            idealPosition = Math.max(idealPosition, 0);
            idealPosition = Math.min(idealPosition, root.width - width);
            return idealPosition;
        }
        y: {
            let idealPosition = subtitleText.y - height;
            if (idealPosition < 0)
            {
                idealPosition = subtitleText.y + subtitleText.height;
            }
            return idealPosition;
        }
        width: MementoSettings.interfacePopupWidth
        height: MementoSettings.interfacePopupHeight
        closePolicy: Popup.NoAutoClose

        onShouldOpenChanged: definitionPopup.shouldOpen ? definitionPopup.open() : definitionPopup.close()
        onOpened: {
            const popupFits = x >= 0 &&
                            y >= 0 &&
                            x + width <= parent.width &&
                            y + height <= parent.height;
            if (!popupFits)
            {
                definitionPopup.close();
            }
        }
        onClosed: {
            definitionPopup.clearResults();
            interactiveTimer.restart();
        }

        onPlayerWidthChanged: definitionPopup.clearResults()
        onPlayerHeightChanged: definitionPopup.clearResults()
        onPausedChanged: definitionPopup.clearResults()
        onPositionChanged: definitionPopup.clearResults()

        Rectangle {
            id: dictionaryBorderRectangle
            anchors.fill: parent
            color: "transparent"
            border.color: MementoPalette.border
            border.width: Features.isUnix ? 1 : 0

            DefinitionPage {
                id: definitionPage
                anchors.fill: parent
                anchors.margins: dictionaryBorderRectangle.border.width
                search: dictionarySearch

                /**
                 * Selects the longest cloze match or clears the selection on clear.
                 */
                function updateSelection() {
                    if (dictionarySearch.terms.length === 0 &&
                        dictionarySearch.kanji === null)
                    {
                        subtitleText.clearSelection();
                        return;
                    }
                    let selectionLength = 0;
                    if (dictionarySearch.terms.length > 0)
                    {
                        selectionLength =
                            dictionarySearch.terms[0].clozeBody.length;
                    }
                    else if (dictionarySearch.kanji !== null)
                    {
                        selectionLength = 1;
                    }
                    subtitleText.select(
                                subtitleText.hoverIndex,
                                subtitleText.hoverIndex + selectionLength);
                }

                onClosePressed: definitionPopup.clearResults()

                Connections {
                    target: dictionarySearch

                    function onKanjiChanged() {
                        definitionPage.updateSelection();
                    }

                    function onTermsChanged() {
                        definitionPage.updateSelection();
                    }
                }
            }
        }

        DictionarySearch {
            id: dictionarySearch
            onLookupCompleted: function(word, reading, text, context) {
                LookupHistory.record(word, reading, text, context);
            }
        }

        Timer {
            id: definitionPopupTimer
            interval: MementoSettings.searchDelay
            running: false
            repeat: false
            onTriggered: {
                if (subtitleText.hoverIndex !== definitionPopup.lastHoverIndex)
                {
                    return;
                }
                else if (!root.state.pause)
                {
                    return;
                }

                Qt.callLater(definitionPopup.search, subtitleText.hoverIndex);
            }
        }

        /**
         * Starts the search timer and sets start state.
         */
        function startSearchTimer() {
            if (subtitleText.hoverIndex === definitionPopup.lastHoverIndex)
            {
                return;
            }
            definitionPopup.lastHoverIndex = subtitleText.hoverIndex;
            if (subtitleText.hoverIndex < 0)
            {
                return;
            }
            else if (!root.state.pause)
            {
                return;
            }
            definitionPopupTimer.restart();
        }

        /**
         * Executes a search using the hover index of the subtitle text.
         * @param index The index into the subtitle text to search.
         */
        function search(index) {
            if (index < 0 || index >= subtitleText.text.length)
            {
                return;
            }

            definitionPopup.cursorAtSearch = root.cursorPosition;
            definitionPage.resetStack();

            const text = subtitleText.text;
            const query = text.substring(index);
            dictionarySearch.historyContext = LookupHistory.contextFor(
                        root.state.path, root.state.timePosition);
            dictionarySearch.searchTerms(query, text, index);
            dictionarySearch.searchKanji(text.charAt(index), text, index);
        }

        /**
         * Clears search results.
         */
        function clearResults() {
            definitionPopup.lastHoverIndex = -1;
            definitionPage.resetStack();
            dictionarySearch.clearResults();
        }
    }

    Menu {
        id: subtitleContextMenu

        Action {
            text: qsTr("Copy Selected Text")
            enabled: subtitleText.selectedText.length > 0
            onTriggered: subtitleClipboard.setText(subtitleText.selectedText)
        }

        Action {
            text: qsTr("Copy Current Subtitle")
            enabled: subtitleText.text.length > 0
            onTriggered: subtitleClipboard.setText(subtitleText.text)
        }

        MenuSeparator {}

        Action {
            text: qsTr("Export Image and Sentence Audio")
            enabled: MigakuClient.enabled && !MigakuClient.busy && root.state.pause
            onTriggered: MigakuClient.exportCurrentSubtitle()
        }
    }

    SubtitleText {
        id: subtitleText
        opacity: root.privateSubtitlesActive ? 0 : 1


        anchors {
            horizontalCenter: root.horizontalCenter
            bottom: root.bottom

            /* Makes sure UI elements don't obscure the subtitles or that they go offscreen */
            bottomMargin: {
                let minValue = controls.visible ? controls.height : 0;
                let maxValue = root.height - subtitleText.height;
                if (!Features.isMacos && menu.visible)
                {
                    maxValue -= menu.height;
                }
                let margin = root.height * MementoSettings.interfaceSubtitleOffset;
                margin = Math.max(margin, minValue);
                margin = Math.min(margin, maxValue);
                return margin;
            }
        }

        /* Prevents text from being wider than the window */
        transformOrigin: Item.Bottom
        scale: subtitleText.width > root.width ? (root.width / subtitleText.width) : 1.0

        font.family: MementoSettings.interfaceSubtitleFont.family
        font.bold: MementoSettings.interfaceSubtitleFont.bold
        font.italic: MementoSettings.interfaceSubtitleFont.italic
        font.underline: MementoSettings.interfaceSubtitleFont.underline
        font.pixelSize: root.height * MementoSettings.interfaceSubtitleScale
        font.weight: MementoSettings.interfaceSubtitleFont.weight
        font.overline: MementoSettings.interfaceSubtitleFont.overline
        font.strikeout: MementoSettings.interfaceSubtitleFont.strikeout
        font.letterSpacing: MementoSettings.interfaceSubtitleFont.letterSpacing
        font.wordSpacing: MementoSettings.interfaceSubtitleFont.wordSpacing
        font.kerning: MementoSettings.interfaceSubtitleFont.kerning
        font.preferShaping: MementoSettings.interfaceSubtitleFont.preferShaping
        font.hintingPreference: MementoSettings.interfaceSubtitleFont.hintingPreference
        font.styleName: MementoSettings.interfaceSubtitleFont.styleName

        color: MementoSettings.interfaceSubtitleColor
        background: MementoSettings.interfaceSubtitleBackground
        stroke: MementoSettings.interfaceSubtitleStrokeColor
        strokeSize: MementoSettings.interfaceSubtitleStroke
        lineSpacing: MementoSettings.interfaceSubtitleLineSpacing

        text: {
            const model = SubtitleLists.primary;
            const source = model?.fullTimelineReady ?
                model.activeText : root.state.subtitle.text;
            return root.cleanSubtitleText(source);
        }

        visible: {
            if (root.ocrMode)
            {
                return false;
            }

            if (!(MementoSettings.searchHideMpvSubs || root.privateSubtitlesActive) ||
                root.primarySubtitleIsBitmap)
            {
                return false;
            }

            /* Implement unconditional hiding of subtitles */
            if (!menu.showSubtitles)
            {
                return false;
            }

            /* Implement hide Memento subtitles while playing media */
            if (MementoSettings.searchHideSubs && !root.state.pause)
            {
                return false;
            }

            /* Implements subtitle cursor show */
            if (MementoSettings.behaviorSubtitleCursorShow)
            {
                return itemCursorSubtitleShow.show;
            }

            return true;
        }

        function autoCopySubtitle() {
            if (MementoSettings.behaviorSubtitleAutoCopy && text.trim().length > 0)
                subtitleClipboard.setText(text);
        }
        onTextChanged: autoCopySubtitle()
        Connections {
            target: MementoSettings
            function onBehaviorSubtitleAutoCopyChanged() {
                subtitleText.autoCopySubtitle();
            }
        }

        Clipboard {
            id: subtitleClipboard
        }
        onDoubleClicked: subtitleClipboard.setText(text)
        onSelectionFinished: subtitleClipboard.setText(selectedText)
        onRightClicked: subtitleContextMenu.popup()

        HoverHandler {
            id: subtitleHover

            /* Implements auto pause on hover */
            onHoveredChanged: {
                if (subtitleHover.hovered && MementoSettings.searchPauseOnHover)
                {
                    root.controller.pause()
                }
            }
        }

        /* Implement searching */
        onHoverIndexChanged: {
            switch (MementoSettings.searchMethod)
            {
            case MementoSetting.SearchMethodHover:
                definitionPopup.startSearchTimer();
                break;

            case MementoSetting.SearchMethodModifier:
                if (KeyTracker.modifierHeld(MementoSettings.searchModifier))
                {
                    Qt.callLater(definitionPopup.search, subtitleText.hoverIndex);
                }
                break;
            }
        }
        onMiddleClicked: {
            if (MementoSettings.searchMiddleMouseScan)
            {
                Qt.callLater(definitionPopup.search, subtitleText.hoverIndex);
            }
        }

        Connections {
            target: KeyTracker
            function onModifiersChanged() {
                if (MementoSettings.searchMethod === MementoSetting.SearchMethodModifier &&
                        subtitleHover.hovered &&
                        KeyTracker.modifierHeld(MementoSettings.searchModifier))
                {
                    Qt.callLater(definitionPopup.search, subtitleText.hoverIndex);
                }
            }
        }
    }

    SubtitleText {
        id: secondarySubtitleText
        opacity: root.privateSubtitlesActive ? 0 : 1

        anchors {
            horizontalCenter: root.horizontalCenter
            top: root.top
            topMargin: {
                let margin = root.height *
                    MementoSettings.interfaceSubtitleOffset;
                if (!Features.isMacos && menu.visible)
                    margin = Math.max(margin, menu.height);
                return margin;
            }
        }

        transformOrigin: Item.Top
        scale: secondarySubtitleText.width > root.width ?
            (root.width / secondarySubtitleText.width) : 1.0

        font.family: MementoSettings.interfaceSubtitleFont.family
        font.bold: MementoSettings.interfaceSubtitleFont.bold
        font.italic: MementoSettings.interfaceSubtitleFont.italic
        font.underline: MementoSettings.interfaceSubtitleFont.underline
        font.pixelSize: root.height * MementoSettings.interfaceSubtitleScale
        font.weight: MementoSettings.interfaceSubtitleFont.weight
        font.overline: MementoSettings.interfaceSubtitleFont.overline
        font.strikeout: MementoSettings.interfaceSubtitleFont.strikeout
        font.letterSpacing: MementoSettings.interfaceSubtitleFont.letterSpacing
        font.wordSpacing: MementoSettings.interfaceSubtitleFont.wordSpacing
        font.kerning: MementoSettings.interfaceSubtitleFont.kerning
        font.preferShaping: MementoSettings.interfaceSubtitleFont.preferShaping
        font.hintingPreference: MementoSettings.interfaceSubtitleFont.hintingPreference
        font.styleName: MementoSettings.interfaceSubtitleFont.styleName

        color: MementoSettings.interfaceSubtitleColor
        background: MementoSettings.interfaceSubtitleBackground
        stroke: MementoSettings.interfaceSubtitleStrokeColor
        strokeSize: MementoSettings.interfaceSubtitleStroke
        lineSpacing: MementoSettings.interfaceSubtitleLineSpacing

        text: {
            const model = SubtitleLists.secondary;
            const source = model?.fullTimelineReady ?
                model.activeText : root.state.secondarySubtitle.text;
            return root.cleanSubtitleText(source);
        }

        visible: {
            if (secondarySubtitleWindow.visible || root.ocrMode ||
                !(MementoSettings.searchHideMpvSubs || root.privateSubtitlesActive) ||
                root.secondarySubtitleIsBitmap || !menu.showSubtitles ||
                root.state.secondarySid <= 0)
            {
                return false;
            }
            if (MementoSettings.searchHideSubs && !root.state.pause)
            {
                return false;
            }
            if (MementoSettings.behaviorSecondarySubtitleCursorShow)
            {
                return itemCursorSecondarySubtitleShow.show;
            }
            return true;
        }

        onDoubleClicked: subtitleClipboard.setText(text)
        onSelectionFinished: subtitleClipboard.setText(selectedText)

        HoverHandler {
            id: secondarySubtitleHover

            onHoveredChanged: {
                if (secondarySubtitleHover.hovered &&
                    MementoSettings.searchPauseOnHover)
                {
                    root.controller.pause();
                }
            }
        }
    }

    Connections {
        target: MementoSettings

        function onSearchHideMpvSubsChanged() {
            root.synchronizeSubtitleRenderers();
        }

        function onSearchHideSubsChanged() {
            root.synchronizeSubtitleRenderers();
        }
    }

    // A separate top-level surface stays visible locally while a capture of
    // the Memento player window contains only the video and its controls.
    Window {
        id: privatePrimarySubtitleWindow
        title: qsTr("Memento — Private subtitles")
        transientParent: root.Window.window
        flags: Qt.Tool | Qt.FramelessWindowHint |
            Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        width: root.width
        height: Math.max(1, Math.ceil(subtitleText.height))
        visible: root.privateSubtitlesActive && subtitleText.visible &&
            subtitleText.text.length > 0 && root.Window.window?.visible &&
            root.Window.window?.visibility !== Window.Minimized
        onVisibleChanged: Qt.callLater(root.updatePrivateSubtitleWindowPosition)

        SubtitleText {
            x: subtitleText.x
            y: 0
            width: subtitleText.width
            height: subtitleText.height
            transformOrigin: Item.Bottom
            scale: subtitleText.scale
            visible: subtitleText.visible
            text: subtitleText.text
            font: subtitleText.font
            color: subtitleText.color
            background: subtitleText.background
            stroke: subtitleText.stroke
            strokeSize: subtitleText.strokeSize
            lineSpacing: subtitleText.lineSpacing
        }
    }

    Window {
        id: privateSecondarySubtitleWindow
        title: qsTr("Memento — Private secondary subtitles")
        transientParent: root.Window.window
        flags: Qt.Tool | Qt.FramelessWindowHint |
            Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        width: root.width
        height: Math.max(1, Math.ceil(secondarySubtitleText.height))
        visible: root.privateSubtitlesActive && secondarySubtitleText.visible &&
            secondarySubtitleText.text.length > 0 && root.Window.window?.visible &&
            root.Window.window?.visibility !== Window.Minimized
        onVisibleChanged: Qt.callLater(root.updatePrivateSubtitleWindowPosition)

        SubtitleText {
            x: secondarySubtitleText.x
            y: 0
            width: secondarySubtitleText.width
            height: secondarySubtitleText.height
            transformOrigin: Item.Top
            scale: secondarySubtitleText.scale
            visible: secondarySubtitleText.visible
            text: secondarySubtitleText.text
            font: secondarySubtitleText.font
            color: secondarySubtitleText.color
            background: secondarySubtitleText.background
            stroke: secondarySubtitleText.stroke
            strokeSize: secondarySubtitleText.strokeSize
            lineSpacing: secondarySubtitleText.lineSpacing
        }
    }

    Window {
        id: secondarySubtitleWindow
        title: qsTr("Memento — Secondary subtitles")
        width: 900
        height: 300
        minimumWidth: 360
        minimumHeight: 160
        color: "#101010"
        transientParent: null
        onVisibleChanged: root.synchronizeSubtitleRenderers()

        Text {
            anchors.fill: parent
            anchors.margins: 24
            color: MementoSettings.interfaceSubtitleColor
            font.family: MementoSettings.interfaceSubtitleFont.family
            font.pixelSize: Math.max(24, Math.min(64, secondarySubtitleWindow.height * 0.16))
            minimumPixelSize: 12
            fontSizeMode: Text.Fit
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: {
                if (root.state.secondarySid <= 0)
                    return qsTr("Select a secondary subtitle track from Subtitles → Second Track.");
                if (root.secondarySubtitleIsBitmap)
                    return qsTr("This window requires a text subtitle track (such as SRT or ASS).");
                if (!menu.showSubtitles || (MementoSettings.searchHideSubs && !root.state.pause))
                    return "";
                return secondarySubtitleText.text;
            }
        }
        Shortcut {
            sequence: "F11"
            onActivated: secondarySubtitleWindow.visibility === Window.FullScreen ?
                secondarySubtitleWindow.showNormal() : secondarySubtitleWindow.showFullScreen()
        }
        Shortcut {
            sequence: "Escape"
            onActivated: secondarySubtitleWindow.showNormal()
        }
    }

    PlayerMenu {
        id: menu
        objectName: "playerMenuBar"
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        visible: Features.isMacos
        player: root
        movieModeEnabled: movieSettings.enabled
        streamSubtitlesIncluded: streamSettings.includeTextSubtitles
        streamSubtitlesCanBeHidden: root.streamSubtitlesCanBeHidden
        onStreamSubtitlesIncludedRequested: function(included) {
            streamSettings.includeTextSubtitles = included;
        }
        onMovieModeToggled: movieSettings.enabled = !movieSettings.enabled
        secondarySubtitlesDetached: secondarySubtitleWindow.visible
        onDetachSecondarySubtitlesRequested: {
            if (secondarySubtitleWindow.visible) {
                secondarySubtitleWindow.close();
            } else {
                secondarySubtitleWindow.show();
                secondarySubtitleWindow.raise();
                secondarySubtitleWindow.requestActivate();
            }
        }
        onJimakuSearchRequested: root.jimakuSearchRequested()
        onNextSubtitleSearchRequested: root.nextSubtitleSearchRequested()
        onTorrentSearchRequested: root.torrentSearchRequested()
        onStatisticsRequested: root.statisticsRequested()
        onAniListSearchRequested: root.aniListSearchRequested()
        onShowSubtitlesChanged: root.synchronizeSubtitleRenderers()

        OpacityAnimator on opacity {
            id: menuFadeIn
            to: 1
            duration: Math.max(MementoSettings.behaviorOscFadeDuration, 1)
            running: false
            onStarted: menu.visible = true
        }

        OpacityAnimator on opacity {
            id: menuFadeOut
            to: 0
            duration: Math.max(MementoSettings.behaviorOscFadeDuration, 1)
            running: false
            onFinished: menu.visible = false
        }

        onOcrModeRequested: root.startOcrMode()
    }

    PlayerControls {
        id: controls
        objectName: "playerControlBar"
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        visible: false
        player: root
        canCopySubtitle: subtitleText.text.trim().length > 0
        sentencePending: root.screenshotSentencePending
        onCopySubtitleRequested: root.copyCurrentSubtitle()
        onCopyScreenshotRequested: root.copyScreenshotAndSentence()

        OpacityAnimator on opacity {
            id: controlsFadeIn
            to: 1
            duration: Math.max(MementoSettings.behaviorOscFadeDuration, 1)
            running: false
            onStarted: controls.visible = true
        }

        OpacityAnimator on opacity {
            id: controlsFadeOut
            to: 0
            duration: Math.max(MementoSettings.behaviorOscFadeDuration, 1)
            running: false
            onFinished: controls.visible = false
        }

        HoverHandler {
            id: controlsHover
        }
    }

    PlayerOcrOverlay {
        id: ocrOverlay
        player: root
        onHideOscRequested: {
            definitionPopup.clearResults();
            interactiveTimer.stop();
            root.hideOsc(true);
        }
        onModeChanged: (enabled) => root.ocrMode = enabled
        onRestoreOscRequested: {
            if (root.oscVisibility === Player.OscVisible)
            {
                root.showOsc();
            }
            else
            {
                interactiveTimer.restart();
            }
        }
        onShowTextRequested: (text) => root.controller.showText(text)
        onTextRecognized: (text) => root.auxiliarySearchRequested(text)
    }
}
