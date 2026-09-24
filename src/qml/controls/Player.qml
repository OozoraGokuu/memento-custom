import QtQuick
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

    property bool ocrMode: false
    property bool subtitleRendererReady: false

    signal auxiliarySearchRequested(string text)
    signal jimakuSearchRequested()
    signal torrentSearchRequested()

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
        if (root.ocrMode)
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
        const useMemento = MementoSettings.searchHideMpvSubs;
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
            useMpvSecondary && secondaryShown);
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
        let text = (source || "").replace(subtitleText.regexFilter, "");
        if (MementoSettings.searchReplaceNewlines)
            text = text.split("\n").join(
                MementoSettings.searchReplaceNewlinesWith);
        return text;
    }

    onFileLoaded: {
        root.synchronizeSubtitleRenderers();
        if (JimakuClient.autoFetch && JimakuClient.apiKeyConfigured)
        {
            Qt.callLater(function() {
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
        enabled: subtitleText.visible && subtitleText.selectedText.length > 0
        shortcut: StandardKey.Copy
        onTriggered: subtitleClipboard.setText(subtitleText.selectedText)
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

        readonly property var regexFilter: Utils.safeRegex(MementoSettings.searchRemoveRegex, "g")

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

            if (!MementoSettings.searchHideMpvSubs ||
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
            if (root.ocrMode || !MementoSettings.searchHideMpvSubs ||
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

    PlayerMenu {
        id: menu
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        visible: Features.isMacos
        player: root
        onJimakuSearchRequested: root.jimakuSearchRequested()
        onTorrentSearchRequested: root.torrentSearchRequested()
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
        anchors {
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        visible: false
        player: root

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
