pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Dialog {
    id: root

    required property MpvPlayer player
    readonly property bool creatingRoom: roomTabs.currentIndex === 0
    readonly property string roomCode: creatingRoom ? preferences.createdRoom : joinRoomField.text.trim()
    property bool launchFailed: false

    function savePreferences() {
        preferences.host = hostField.text.trim();
        preferences.displayName = nameField.text.trim();
        preferences.joinedRoom = joinRoomField.text.trim();
        preferences.syncplayPath = syncplayPathField.text.trim();
    }

    function openForCurrentMedia() {
        if (preferences.createdRoom.length === 0)
        {
            preferences.createdRoom = WatchTogether.createRoomCode();
        }
        statusLabel.text = "";
        launchFailed = false;
        root.open();
        Qt.callLater(function() {
            if (root.creatingRoom)
            {
                createdRoomField.forceActiveFocus();
                createdRoomField.selectAll();
            }
            else
            {
                joinRoomField.forceActiveFocus();
            }
        });
    }

    function copyInvite() {
        const host = hostField.text.trim();
        const room = root.roomCode;
        if (host.length === 0 || room.length === 0) return;
        root.savePreferences();
        clipboard.setText(qsTr(
            "Watch together in Memento\nServer: %1\nRoom: %2\n\n" +
            "Install Syncplay 1.7.5 or newer. In Memento for Windows, " +
            "open Media → Watch Together…, choose Join room, and enter " +
            "these details. Open " +
            "the same video in the synced player using your own file or " +
            "accessible stream."
        ).arg(host).arg(room));
        launchFailed = false;
        statusLabel.text = qsTr("Invite copied to clipboard.");
    }

    function startSession() {
        const host = hostField.text.trim();
        const room = root.roomCode;
        const name = nameField.text.trim();
        if (host.length === 0 || room.length === 0 || name.length === 0)
        {
            launchFailed = true;
            statusLabel.text = qsTr("Enter a server, room code, and display name.");
            return;
        }

        root.savePreferences();
        const result = WatchTogether.launch(
            host, room, name, root.player.state.path,
            syncplayPathField.text.trim()
        );
        launchFailed = result.length > 0;
        statusLabel.text = launchFailed ? result :
            qsTr("Syncplay launched. Finish its setup if prompted, then share the invite details with the other viewers.");
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 36 : 560, 560)
    height: Math.min(parent ? parent.height - 36 : 610, 610)
    modal: true
    title: qsTr("Watch Together")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape
    onClosed: root.savePreferences()

    Settings {
        id: preferences
        category: "WatchTogether"
        property string host: "syncplay.pl:8995"
        property string displayName: ""
        property string createdRoom: ""
        property string joinedRoom: ""
        property string syncplayPath: ""
    }

    Clipboard {
        id: clipboard
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 10

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr(
                    "Syncplay 1.7.5 or newer opens a separate Memento window " +
                    "and keeps playback in sync. Each viewer needs their own " +
                    "access to the same video."
                )
            }

            TabBar {
                id: roomTabs
                Layout.fillWidth: true

                TabButton {
                    text: qsTr("Create room")
                    onClicked: statusLabel.text = ""
                }
                TabButton {
                    text: qsTr("Join room")
                    onClicked: statusLabel.text = ""
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Room code")
            }

            RowLayout {
                visible: root.creatingRoom
                Layout.fillWidth: true

                TextField {
                    id: createdRoomField
                    Layout.fillWidth: true
                    text: preferences.createdRoom
                    readOnly: true
                    selectByMouse: true
                }

                Button {
                    text: qsTr("Copy code")
                    enabled: preferences.createdRoom.length > 0
                    onClicked: {
                        clipboard.setText(preferences.createdRoom);
                        root.launchFailed = false;
                        statusLabel.text = qsTr("Room code copied to clipboard.");
                    }
                }

                Button {
                    text: qsTr("New code")
                    onClicked: {
                        preferences.createdRoom = WatchTogether.createRoomCode();
                        statusLabel.text = "";
                    }
                }
            }

            TextField {
                id: joinRoomField
                visible: !root.creatingRoom
                Layout.fillWidth: true
                text: preferences.joinedRoom
                placeholderText: qsTr("Enter the host's room code")
                selectByMouse: true
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Server")
            }

            TextField {
                id: hostField
                Layout.fillWidth: true
                text: preferences.host
                placeholderText: qsTr("syncplay.pl:8995")
                selectByMouse: true
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Display name")
            }

            TextField {
                id: nameField
                Layout.fillWidth: true
                text: preferences.displayName
                placeholderText: qsTr("Your name in the room")
                selectByMouse: true
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Syncplay executable (optional)")
            }

            RowLayout {
                Layout.fillWidth: true

                TextField {
                    id: syncplayPathField
                    Layout.fillWidth: true
                    text: preferences.syncplayPath
                    placeholderText: qsTr("Find Syncplay automatically")
                    selectByMouse: true
                }

                Button {
                    text: qsTr("Detect")
                    onClicked: {
                        const path = WatchTogether.detectedSyncplayPath();
                        if (path.length > 0)
                        {
                            syncplayPathField.text = path;
                            root.launchFailed = false;
                            statusLabel.text = qsTr("Syncplay found at %1").arg(path);
                        }
                        else
                        {
                            root.launchFailed = true;
                            statusLabel.text = qsTr("Syncplay was not found. Install it or enter its executable path.");
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: MementoPalette.placeholderText
                textFormat: Text.PlainText
                text: root.player.state.path.length > 0 ?
                    qsTr("Current media: %1. Syncplay will open it if it is a local file.")
                        .arg(root.player.state.title || root.player.state.path) :
                    qsTr("No media is open. Choose a file in Syncplay after it starts.")
            }

            Label {
                id: statusLabel
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: root.launchFailed ? "#c62828" : MementoPalette.text
            }

            RowLayout {
                Layout.fillWidth: true

                Button {
                    text: qsTr("Copy invite")
                    enabled: hostField.text.trim().length > 0 && root.roomCode.length > 0
                    onClicked: root.copyInvite()
                }

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Start in Syncplay")
                    enabled: hostField.text.trim().length > 0 &&
                             root.roomCode.length > 0 && nameField.text.trim().length > 0
                    onClicked: root.startSession()
                }
            }
        }
    }
}
