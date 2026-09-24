import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Page {
    id: root

    property int preferredWidth: 640
    property int groupSpacing: 12

    background: Rectangle {
        color: MementoPalette.window
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: root.groupSpacing

            Label {
                Layout.preferredWidth: root.preferredWidth
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 22
                text: qsTr("Jimaku Subtitles")
                font.pixelSize: 22
                font.bold: true
            }

            Label {
                Layout.preferredWidth: root.preferredWidth
                Layout.alignment: Qt.AlignHCenter
                wrapMode: Text.Wrap
                color: MementoPalette.placeholderText
                text: qsTr("Memento identifies the current show and episode, downloads the best Japanese subtitle from Jimaku, and attaches it to the player immediately.")
            }

            SettingsBox {
                Layout.preferredWidth: root.preferredWidth
                Layout.alignment: Qt.AlignHCenter
                title: qsTr("Jimaku account")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: root.groupSpacing

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        textFormat: Text.RichText
                        text: qsTr("Create an API key on the <a href=\"https://jimaku.cc/account\">Jimaku account page</a>, then paste it below.")
                        onLinkActivated: function(link) {
                            Qt.openUrlExternally(link);
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true

                        TextField {
                            id: apiKeyField
                            Layout.fillWidth: true
                            text: JimakuClient.apiKey
                            placeholderText: qsTr("Jimaku API key")
                            echoMode: revealKey.checked ? TextInput.Normal : TextInput.Password
                            selectByMouse: true
                            enabled: !JimakuClient.busy
                            onEditingFinished: JimakuClient.apiKey = text
                            onAccepted: {
                                JimakuClient.apiKey = text;
                                JimakuClient.testConnection();
                            }
                        }

                        ToolButton {
                            id: revealKey
                            checkable: true
                            text: checked ? qsTr("Hide") : qsTr("Show")
                        }

                        Button {
                            text: JimakuClient.busy ? qsTr("Testing…") : qsTr("Test")
                            enabled: !JimakuClient.busy && apiKeyField.text.trim().length > 0
                            onClicked: {
                                JimakuClient.apiKey = apiKeyField.text;
                                JimakuClient.testConnection();
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: JimakuClient.status.length > 0
                        wrapMode: Text.Wrap
                        textFormat: Text.PlainText
                        color: MementoPalette.placeholderText
                        text: JimakuClient.status
                    }
                }
            }

            SettingsBox {
                Layout.preferredWidth: root.preferredWidth
                Layout.alignment: Qt.AlignHCenter
                title: qsTr("Playback")

                ColumnLayout {
                    anchors.fill: parent
                    spacing: root.groupSpacing

                    RowLayout {
                        Layout.fillWidth: true

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Label {
                                text: qsTr("Attach subtitles automatically")
                            }
                            Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                color: MementoPalette.placeholderText
                                text: qsTr("Fetch from Jimaku as soon as a local or torrent episode starts.")
                            }
                        }

                        Switch {
                            checked: JimakuClient.autoFetch
                            onClicked: JimakuClient.autoFetch = checked
                        }
                    }

                    SettingsBoxSeparator {
                        Layout.fillWidth: true
                    }

                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: MementoPalette.placeholderText
                        text: qsTr("You can always fetch manually from Subtitle → Fetch from Jimaku, or press Ctrl+Shift+J. Downloaded files are cached for instant reuse.")
                    }
                }
            }

            Item {
                Layout.preferredHeight: 20
            }
        }
    }
}
