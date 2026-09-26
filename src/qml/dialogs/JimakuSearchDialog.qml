pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Dialog {
    id: root

    required property MpvPlayer player
    property int selectedEntryIndex: -1
    property var savedLink: ({})
    property bool browsingLink: false

    function refreshLink() {
        savedLink = EpisodeLibrary.subtitleLinkForFile(root.player.state.path);
    }

    function browseLink(all) {
        errorLabel.text = "";
        root.browsingLink = true;
        root.selectedEntryIndex = -1;
        root.client.openLinkedEntry(root.savedLink, episodeBox.value, all);
    }
    readonly property bool useJimaku: providerBox.currentIndex === 0
    readonly property var client: useJimaku ? JimakuClient : KitsunekkoClient

    function openForCurrentMedia() {
        errorLabel.text = "";
        JimakuClient.clearSearch();
        KitsunekkoClient.clearSearch();
        root.refreshLink();
        root.browsingLink = false;
        if (savedLink.provider === "jimaku") providerBox.currentIndex = 0;
        else if (savedLink.provider === "kitsunekko") providerBox.currentIndex = 1;
        root.selectedEntryIndex = -1;
        queryField.text = savedLink.name || JimakuClient.suggestedTitle();
        episodeBox.value = JimakuClient.suggestedEpisode();
        root.open();
        Qt.callLater(function() {
            if (!root.useJimaku || JimakuClient.apiKeyConfigured)
            {
                queryField.forceActiveFocus();
                queryField.selectAll();
            }
            else
            {
                apiKeyField.forceActiveFocus();
            }
        });
    }

    function saveKeyAndTest() {
        JimakuClient.apiKey = apiKeyField.text;
        JimakuClient.testConnection();
    }

    function search() {
        errorLabel.text = "";
        if (root.useJimaku) JimakuClient.apiKey = apiKeyField.text;
        root.selectedEntryIndex = -1;
        root.browsingLink = false;
        root.client.search(queryField.text);
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 1040, 1040)
    height: Math.min(parent ? parent.height - 48 : 760, 760)
    modal: true
    title: qsTr("Search Japanese subtitles")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    onClosed: { JimakuClient.cancel(); KitsunekkoClient.cancel(); }

    Connections {
        target: EpisodeLibrary
        function onLibraryChanged() { root.refreshLink(); }
    }

    Connections {
        target: root.player.state
        function onPathChanged() {
            if (root.visible) root.openForCurrentMedia();
        }
    }

    Connections {
        target: root.client

        function onFailed(message) {
            errorLabel.text = message;
        }

        function onSubtitleAttached(fileName, entryName) {
            root.close();
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Subtitle source") }
            ComboBox {
                id: providerBox
                Layout.fillWidth: true
                model: [qsTr("Jimaku"), qsTr("Kitsunekko — AJATT online mirror")]
                currentIndex: JimakuClient.apiKeyConfigured ? 0 : 1
                onActivated: {
                    JimakuClient.cancel();
                    KitsunekkoClient.cancel();
                    root.browsingLink = false;
                    root.client.clearSearch();
                    root.selectedEntryIndex = -1;
                    errorLabel.text = "";
                }
            }
            Button {
                text: qsTr("Refresh catalog")
                visible: !root.useJimaku
                enabled: !root.client.busy && queryField.text.trim().length > 0
                onClicked: {
                    root.selectedEntryIndex = -1;
                    root.browsingLink = false;
                    KitsunekkoClient.refreshCatalog(queryField.text);
                }
            }
            Button {
                text: qsTr("Open website")
                flat: true
                onClicked: Qt.openUrlExternally(root.useJimaku ? "https://jimaku.cc/" : "https://subtitles.ajatt.top/")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !!root.savedLink.provider
            Label {
                Layout.fillWidth: true
                text: qsTr("Linked subtitle title: %1 — %2")
                    .arg(root.savedLink.provider || "").arg(root.savedLink.name || "")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
            Button {
                text: qsTr("Browse episode subtitles")
                enabled: !root.client.busy
                onClicked: {
                    JimakuClient.cancel(); KitsunekkoClient.cancel();
                    providerBox.currentIndex = root.savedLink.provider === "jimaku" ? 0 : 1;
                    if (root.useJimaku) JimakuClient.apiKey = apiKeyField.text;
                    root.browseLink(false);
                }
            }
            Button {
                text: qsTr("Change link")
                onClicked: {
                    root.client.clearSearch(); root.browsingLink = false;
                    root.selectedEntryIndex = -1;
                    queryField.forceActiveFocus(); queryField.selectAll();
                }
            }
            Button {
                text: qsTr("Unlink")
                onClicked: {
                    if (!EpisodeLibrary.clearSubtitleLinkForFile(root.player.state.path))
                        errorLabel.text = qsTr("Could not remove the saved link.");
                    root.browsingLink = false;
                    root.selectedEntryIndex = -1;
                    root.client.clearSearch();
                }
            }
        }

        Frame {
            visible: root.useJimaku
            Layout.fillWidth: true
            padding: 8

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Label {
                    text: qsTr("API key")
                }

                TextField {
                    id: apiKeyField

                    Layout.fillWidth: true
                    text: JimakuClient.apiKey
                    placeholderText: qsTr("Paste your Jimaku API key")
                    echoMode: revealKey.checked ? TextInput.Normal : TextInput.Password
                    selectByMouse: true
                    enabled: !root.client.busy
                    onAccepted: root.saveKeyAndTest()
                }

                ToolButton {
                    id: revealKey

                    checkable: true
                    text: checked ? qsTr("Hide") : qsTr("Show")
                }

                Button {
                    text: root.client.busy ? qsTr("Testing…") : qsTr("Save & test")
                    enabled: !root.client.busy && apiKeyField.text.trim().length > 0
                    onClicked: root.saveKeyAndTest()
                }

                Button {
                    text: qsTr("Get a key")
                    flat: true
                    onClicked: Qt.openUrlExternally("https://jimaku.cc/account")
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            TextField {
                id: queryField

                Layout.fillWidth: true
                placeholderText: qsTr("Anime title")
                selectByMouse: true
                enabled: !root.client.busy
                onAccepted: root.search()
            }

            Label {
                text: qsTr("Episode")
            }

            SpinBox {
                id: episodeBox

                from: -1
                to: 9999
                value: -1
                editable: true
                enabled: !root.client.busy
                textFromValue: function(value, locale) {
                    return value < 0 ? qsTr("All") : value.toString();
                }
                valueFromText: function(text, locale) {
                    const trimmed = text.trim();
                    if (trimmed.length === 0 || trimmed.toLowerCase() ===
                            qsTr("All").toLowerCase())
                        return -1;
                    const parsed = Number(trimmed);
                    return Number.isFinite(parsed) ? Math.round(parsed) : -1;
                }
            }

            Button {
                text: root.client.busy ? qsTr("Searching…") : qsTr("Search")
                enabled: !root.client.busy &&
                         (!root.useJimaku || apiKeyField.text.trim().length > 0) &&
                         queryField.text.trim().length > 0
                onClicked: root.search()
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: MementoPalette.placeholderText
            text: qsTr(
                "Search online, choose the matching title and Japanese subtitle release, " +
                "then add it to your current video. Only your selected file is downloaded."
            )
        }

        Label {
            id: errorLabel

            Layout.fillWidth: true
            visible: text.length > 0
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: "#e57373"
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                Layout.fillWidth: true
                visible: root.client.status.length > 0
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: MementoPalette.placeholderText
                text: root.client.status
            }

            BusyIndicator {
                visible: root.client.busy
                running: visible
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 430
                padding: 8

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("1. Choose title")
                        font.bold: true
                    }

                    ListView {
                        id: entryList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 4
                        model: root.client.searchResults
                        onModelChanged: Qt.callLater(function() { entryList.positionViewAtBeginning(); })
                        currentIndex: root.selectedEntryIndex

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: ItemDelegate {
                            id: entryDelegate

                            required property var modelData
                            required property int index

                            width: ListView.view.width
                            highlighted: index === root.selectedEntryIndex
                            enabled: !root.client.busy
                            onClicked: {
                                root.browsingLink = false;
                                root.selectedEntryIndex = index;
                                errorLabel.text = "";
                                root.client.selectEntry(index, episodeBox.value);
                            }

                            contentItem: ColumnLayout {
                                spacing: 2

                                Label {
                                    Layout.fillWidth: true
                                    text: entryDelegate.modelData.name
                                    textFormat: Text.PlainText
                                    font.bold: true
                                    wrapMode: Text.Wrap
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: text.length > 0
                                    text: {
                                        const english = entryDelegate.modelData.englishName || "";
                                        const japanese = entryDelegate.modelData.japaneseName || "";
                                        if (english.length > 0 && japanese.length > 0)
                                            return english + " · " + japanese;
                                        return english.length > 0 ? english : japanese;
                                    }
                                    textFormat: Text.PlainText
                                    color: MementoPalette.placeholderText
                                    font.pixelSize: 11
                                    wrapMode: Text.Wrap
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            width: Math.max(0, parent.width - 24)
                            visible: !root.client.busy &&
                                     root.client.searchResults.length === 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: MementoPalette.placeholderText
                            text: qsTr("Search results will appear here.")
                        }
                    }
                }
            }

            Frame {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 570
                padding: 8

                ColumnLayout {
                    anchors.fill: parent
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: root.client.selectedEntryName.length > 0 ?
                                qsTr("2. Choose file — %1").arg(
                                    root.client.selectedEntryName) :
                                qsTr("2. Choose file")
                            textFormat: Text.PlainText
                            font.bold: true
                            elide: Text.ElideRight
                        }

                        Button {
                            text: episodeBox.value < 0 ?
                                qsTr("Refresh all files") :
                                qsTr("Refresh episode %1").arg(episodeBox.value)
                            flat: true
                            visible: root.browsingLink || root.selectedEntryIndex >= 0
                            enabled: !root.client.busy
                            onClicked: root.browsingLink ? root.browseLink(false) : root.client.selectEntry(
                                root.selectedEntryIndex, episodeBox.value)
                        }

                        Button {
                            text: qsTr("Show all files")
                            flat: true
                            visible: (root.browsingLink || root.selectedEntryIndex >= 0) &&
                                     root.client.selectedEpisode >= 0
                            enabled: !root.client.busy
                            onClicked: root.browsingLink ? root.browseLink(true) : root.client.selectEntryAllFiles(
                                root.selectedEntryIndex,
                                root.client.selectedEpisode)
                        }
                    }

                    ListView {
                        id: fileList

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 5
                        model: root.client.fileResults
                        onModelChanged: Qt.callLater(function() { fileList.positionViewAtBeginning(); })

                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: Frame {
                            id: fileDelegate

                            required property var modelData
                            required property int index

                            width: ListView.view.width
                            padding: 8
                            hoverEnabled: true

                            background: Rectangle {
                                color: fileDelegate.hovered ?
                                    Qt.rgba(MementoPalette.text.r,
                                            MementoPalette.text.g,
                                            MementoPalette.text.b, 0.07) :
                                    Qt.rgba(MementoPalette.text.r,
                                            MementoPalette.text.g,
                                            MementoPalette.text.b, 0.035)
                                border.color: MementoPalette.border
                                border.width: 1
                                radius: 5
                            }

                            RowLayout {
                                width: parent.width
                                spacing: 8

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2

                                    Label {
                                        Layout.fillWidth: true
                                        text: fileDelegate.modelData.name
                                        textFormat: Text.PlainText
                                        wrapMode: Text.Wrap
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            const bytes = fileDelegate.modelData.size || 0;
                                            const size = bytes < 1024 * 1024 ?
                                                qsTr("%1 KiB").arg(
                                                    Math.max(1, bytes / 1024).toFixed(0)) :
                                                qsTr("%1 MiB").arg(
                                                    (bytes / (1024 * 1024)).toFixed(1));
                                            const episode = fileDelegate.modelData.episode >= 0 ?
                                                qsTr("episode %1").arg(
                                                    fileDelegate.modelData.episode) :
                                                qsTr("episode not tagged");
                                            return fileDelegate.modelData.format +
                                                " · " + size + " · " + episode;
                                        }
                                        textFormat: Text.PlainText
                                        color: MementoPalette.placeholderText
                                        font.pixelSize: 11
                                    }
                                }

                                Button {
                                    text: qsTr("Add to Memento")
                                    enabled: !root.client.busy &&
                                             root.player.state.path.length > 0
                                    onClicked: {
                                        errorLabel.text = "";
                                        root.client.attachResult(
                                            fileDelegate.index);
                                    }
                                }
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            width: Math.max(0, parent.width - 24)
                            visible: !root.client.busy &&
                                     root.client.fileResults.length === 0
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            color: MementoPalette.placeholderText
                            text: !root.browsingLink && root.selectedEntryIndex < 0 ?
                                qsTr("Choose a title first.") :
                                qsTr("No matching subtitle files are shown.")
                        }
                    }
                }
            }
        }
    }
}
