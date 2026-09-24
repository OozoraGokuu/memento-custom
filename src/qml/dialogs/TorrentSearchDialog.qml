pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Dialog {
    id: root

    required property MpvPlayer player
    property bool choosingEpisode: false
    property string selectedTorrentId: ""
    property string pendingMagnet: ""
    readonly property bool selectedTorrentActive:
        choosingEpisode && (EpisodeLibrary.currentEntry.id || "") === selectedTorrentId

    function chooseEpisode(index) {
        EpisodeLibrary.currentIndex = index;
        selectedTorrentId = EpisodeLibrary.currentEntry.id || "";
        episodeFilter.text = "";
        choosingEpisode = true;
    }

    function playEpisode(index) {
        if (!selectedTorrentActive) {
            errorLabel.text = qsTr("The selected release changed. Choose it again.");
            return;
        }
        const path = EpisodeLibrary.episodePath(index);
        if (path.length === 0) {
            errorLabel.text = EpisodeLibrary.lastError;
            return;
        }
        if (root.player.controller.loadFile(path, false, [
            "demuxer-max-bytes=32MiB", "demuxer-max-back-bytes=8MiB",
            "cache-pause-initial=no"
        ])) {
            root.player.controller.play();
            root.close();
        } else {
            errorLabel.text = qsTr("The stream could not be opened.");
        }
    }

    function suggestedQuery() {
        let value = root.player.state.title || "";
        value = value.replace(/\.[^.]+$/, "");
        value = value.replace(/\[[^\]]*\]/g, " ");
        value = value.replace(/\bS\d{1,2}(?:E|OVA)\d{1,3}\b/gi, " ");
        value = value.replace(/\b(?:EP?|Episode)[ ._-]*\d{1,4}\b/gi, " ");
        return value.replace(/\s+/g, " ").trim();
    }

    function openForCurrentMedia() {
        errorLabel.text = "";
        choosingEpisode = false;
        if (queryField.text.trim().length === 0)
        {
            queryField.text = root.suggestedQuery();
        }
        root.open();
        Qt.callLater(function() {
            queryField.forceActiveFocus();
            queryField.selectAll();
        });
    }

    function search() {
        pendingMagnet = "";
        errorLabel.text = "";
        if (!TorrentSearchClient.configureProvider(providerField.text.trim())) return;
        TorrentSearchClient.search(queryField.text);
    }

    function addMagnet(magnet) {
        errorLabel.text = "";
        const index = EpisodeLibrary.addMagnet(magnet);
        if (index < 0)
        {
            errorLabel.text = EpisodeLibrary.lastError;
            return;
        }
        root.chooseEpisode(index);
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 920, 920)
    height: Math.min(parent ? parent.height - 48 : 720, 720)
    modal: true
    title: root.choosingEpisode ? qsTr("Choose an episode to stream") : qsTr("Find anime streams")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    onClosed: { pendingMagnet = ""; TorrentSearchClient.cancel(); }

    Connections {
        target: TorrentSearchClient

        function onTorrentReady(fileUrl, title) {
            root.pendingMagnet = "";
            const index = EpisodeLibrary.addTorrent(fileUrl);
            if (index < 0)
            {
                errorLabel.text = EpisodeLibrary.lastError;
                return;
            }
            root.chooseEpisode(index);
        }

        function onFailed(message) {
            if (root.pendingMagnet.length > 0) {
                const magnet = root.pendingMagnet;
                root.pendingMagnet = "";
                root.addMagnet(magnet);
            } else {
                errorLabel.text = message;
            }
        }
    }

    Connections {
        target: EpisodeLibrary
        function onErrorOccurred(message) {
            if (root.visible && root.choosingEpisode) errorLabel.text = message;
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Label {
            visible: !root.choosingEpisode
            text: qsTr("Your torrent website or RSS feed")
        }
        TextField {
            id: providerField
            visible: !root.choosingEpisode
            Layout.fillWidth: true
            text: TorrentSearchClient.baseUrl.toString()
            placeholderText: qsTr("https://your-provider.example/rss")
            selectByMouse: true
            enabled: !TorrentSearchClient.busy
            onEditingFinished: TorrentSearchClient.baseUrl = text.trim()
        }
        Label {
            visible: !root.choosingEpisode
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Enter your website or its torrent RSS feed. Memento can find an RSS link advertised by the website; otherwise paste the feed URL. Search uses the feed’s q parameter.")
            color: MementoPalette.placeholderText
        }

        RowLayout {
            visible: !root.choosingEpisode
            Layout.fillWidth: true

            TextField {
                id: queryField

                Layout.fillWidth: true
                placeholderText: qsTr("Anime title or release name")
                selectByMouse: true
                enabled: !TorrentSearchClient.busy
                onAccepted: root.search()
            }

            Button {
                text: TorrentSearchClient.busy ? qsTr("Searching…") : qsTr("Search")
                enabled: !TorrentSearchClient.busy && providerField.text.trim().length > 0 && queryField.text.trim().length > 0
                onClicked: root.search()
            }
        }

        RowLayout {
            visible: !root.choosingEpisode
            Layout.fillWidth: true

            Button {
                text: qsTr("Open your website")
                flat: true
                enabled: TorrentSearchClient.baseUrl.toString().length > 0
                onClicked: Qt.openUrlExternally(TorrentSearchClient.baseUrl)
            }
        }

        RowLayout {
            visible: !root.choosingEpisode
            Layout.fillWidth: true
            Label { text: qsTr("Sort by") }
            ComboBox {
                id: sortBox
                textRole: "text"
                valueRole: "value"
                model: [
                    { text: qsTr("Most seeders"), value: "seeders" },
                    { text: qsTr("Newest releases"), value: "newest" }
                ]
                currentIndex: TorrentSearchClient.sortOrder === "newest" ? 1 : 0
                enabled: !TorrentSearchClient.busy
                onActivated: TorrentSearchClient.sortOrder = currentValue
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("DNS") }
            ComboBox {
                textRole: "text"
                model: [
                    { text: qsTr("Cloudflare, then system fallback") },
                    { text: qsTr("System default") }
                ]
                currentIndex: TorrentSearchClient.cloudflareDns ? 0 : 1
                enabled: !TorrentSearchClient.busy
                onActivated: TorrentSearchClient.cloudflareDns = currentIndex === 0
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: MementoPalette.placeholderText
            text: qsTr(
                "Choose a release, pick an episode, and play while it downloads. " +
                "Memento downloads the selected episode; other files stay unselected."
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

        Label {
            Layout.fillWidth: true
            visible: !root.choosingEpisode && TorrentSearchClient.status.length > 0
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: MementoPalette.placeholderText
            text: TorrentSearchClient.status
        }

        Label {
            Layout.fillWidth: true
            visible: !root.choosingEpisode && TorrentSearchClient.networkRoute.length > 0
            text: TorrentSearchClient.networkRoute
            textFormat: Text.PlainText
            color: MementoPalette.placeholderText
            wrapMode: Text.Wrap
        }

        BusyIndicator {
            Layout.alignment: Qt.AlignHCenter
            visible: TorrentSearchClient.busy || (root.selectedTorrentActive && EpisodeLibrary.episodes.length === 0)
            running: visible
        }

        RowLayout {
            visible: root.choosingEpisode
            Layout.fillWidth: true
            Button {
                text: qsTr("Back to releases")
                onClicked: { root.choosingEpisode = false; errorLabel.text = ""; }
            }
            Label {
                Layout.fillWidth: true
                text: root.selectedTorrentActive ? EpisodeLibrary.currentEntry.title : qsTr("Release selection changed")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.bold: true
            }
            Button {
                text: qsTr("Media Library")
                onClicked: { MementoSettings.windowLibrary = true; root.close(); }
            }
        }

        TextField {
            id: episodeFilter
            visible: root.choosingEpisode
            Layout.fillWidth: true
            placeholderText: qsTr("Filter episodes by number or filename")
            selectByMouse: true
        }

        ListView {
            visible: root.choosingEpisode
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 5
            model: root.selectedTorrentActive ? EpisodeLibrary.episodes : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: ItemDelegate {
                id: episodeDelegate
                required property var modelData
                required property int index
                readonly property bool matches: episodeFilter.text.trim().length === 0 ||
                    modelData.title.toLowerCase().indexOf(episodeFilter.text.trim().toLowerCase()) >= 0
                width: ListView.view.width
                visible: matches
                height: matches ? implicitHeight : 0
                onClicked: root.playEpisode(index)
                contentItem: RowLayout {
                    Label {
                        Layout.fillWidth: true
                        text: episodeDelegate.modelData.title
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                    }
                    Label {
                        text: episodeDelegate.modelData.watched ? qsTr("Watched") : ""
                        color: MementoPalette.placeholderText
                    }
                    Button {
                        text: qsTr("Play")
                        onClicked: root.playEpisode(episodeDelegate.index)
                    }
                }
            }
            Label {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 24)
                visible: root.selectedTorrentActive && EpisodeLibrary.episodes.length === 0
                text: qsTr("Waiting for torrent metadata from peers… You can return to releases if this torrent has no peers.")
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }

        ListView {
            id: resultsList
            visible: !root.choosingEpisode

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: TorrentSearchClient.results
            onModelChanged: Qt.callLater(function() { resultsList.positionViewAtBeginning(); })

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }

            delegate: Frame {
                id: resultDelegate

                required property var modelData
                required property int index

                width: ListView.view.width
                padding: 10
                hoverEnabled: true

                background: Rectangle {
                    color: resultDelegate.hovered ?
                        Qt.rgba(MementoPalette.text.r, MementoPalette.text.g,
                                MementoPalette.text.b, 0.07) :
                        Qt.rgba(MementoPalette.text.r, MementoPalette.text.g,
                                MementoPalette.text.b, 0.035)
                    border.color: MementoPalette.border
                    border.width: 1
                    radius: 6
                }

                ColumnLayout {
                    width: parent.width
                    spacing: 5

                    Label {
                        Layout.fillWidth: true
                        text: resultDelegate.modelData.title
                        textFormat: Text.PlainText
                        font.bold: true
                        wrapMode: Text.Wrap
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Label {
                            Layout.fillWidth: true
                            text: qsTr("%1 • %2 • %3 seeders • %4 leechers")
                                .arg(resultDelegate.modelData.size || qsTr("Unknown size"))
                                .arg(resultDelegate.modelData.uploader || qsTr("Anonymous"))
                                .arg(resultDelegate.modelData.seeders)
                                .arg(resultDelegate.modelData.leechers)
                            textFormat: Text.PlainText
                            color: MementoPalette.placeholderText
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: resultDelegate.modelData.trusted
                            text: qsTr("TRUSTED")
                            color: MementoPalette.accent
                            font.bold: true
                            font.pixelSize: 10
                        }

                        Label {
                            visible: resultDelegate.modelData.remake
                            text: qsTr("REMAKE")
                            color: "#ffb74d"
                            font.bold: true
                            font.pixelSize: 10
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        Button {
                            text: TorrentSearchClient.busy ?
                                qsTr("Please wait…") : qsTr("Choose release")
                            enabled: !TorrentSearchClient.busy
                            onClicked: {
                                errorLabel.text = "";
                                root.pendingMagnet = resultDelegate.modelData.magnet || "";
                                TorrentSearchClient.downloadResult(resultDelegate.index);
                            }
                        }

                        Button {
                            text: qsTr("Use magnet")
                            flat: true
                            visible: resultDelegate.modelData.magnet.length > 0
                            enabled: !TorrentSearchClient.busy
                            onClicked: root.addMagnet(resultDelegate.modelData.magnet)
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Button {
                            text: qsTr("Details")
                            flat: true
                            onClicked: Qt.openUrlExternally(
                                resultDelegate.modelData.detailUrl)
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.max(0, parent.width - 40)
                visible: !TorrentSearchClient.busy && TorrentSearchClient.results.length === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: MementoPalette.placeholderText
                text: qsTr("Search results will appear here. Playback continues while you browse.")
            }
        }
    }
}
