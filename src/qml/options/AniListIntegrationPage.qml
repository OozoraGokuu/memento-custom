import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Page {
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 14
            Label {
                Layout.margins: 16
                text: qsTr("AniList Integration")
                font.pixelSize: 22
                font.bold: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 12
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Use your own AniList application. Create it in AniList Developer Settings, and set its redirect URL to http://127.0.0.1:47832/callback. Enter the Client ID below; no client secret is needed.")
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        Layout.fillWidth: true
                        text: "http://127.0.0.1:47832/callback"
                        readOnly: true
                        selectByMouse: true
                    }
                    Button {
                        text: qsTr("Copy redirect URL")
                        onClicked: redirectClipboard.setText("http://127.0.0.1:47832/callback")
                    }
                    Clipboard { id: redirectClipboard }
                }
                Button {
                    text: qsTr("Open AniList Developer Settings")
                    onClicked: Qt.openUrlExternally("https://anilist.co/settings/developer")
                }
                RowLayout {
                    Layout.fillWidth: true
                    TextField {
                        id: clientIdField
                        Layout.fillWidth: true
                        text: AniListClient.clientId
                        placeholderText: qsTr("Your AniList OAuth Client ID")
                        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,12}/ }
                        enabled: !AniListClient.busy
                        onEditingFinished: AniListClient.clientId = text
                    }
                    Button {
                        text: qsTr("Connect in browser")
                        enabled: clientIdField.text.length > 0 && !AniListClient.busy
                        onClicked: { AniListClient.clientId = clientIdField.text; AniListClient.connectAccount(); }
                    }
                    Button {
                        text: qsTr("Disconnect")
                        enabled: AniListClient.username.length > 0 || AniListClient.connected
                        onClicked: AniListClient.disconnectAccount()
                    }
                }
                Label {
                    text: AniListClient.connected ? qsTr("Connected as %1").arg(AniListClient.username) : qsTr("Not connected")
                    textFormat: Text.PlainText
                }
                RowLayout {
                    Switch {
                        text: qsTr("Automatically sync episode progress")
                        checked: AniListClient.enabled
                        onClicked: AniListClient.enabled = checked
                    }
                    Label { text: qsTr("At") }
                    SpinBox {
                        from: 50; to: 100; editable: true
                        value: AniListClient.threshold
                        onValueModified: AniListClient.threshold = value
                    }
                    Label { text: "%" }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Settings save immediately. Existing AniList progress is never reduced. The final episode marks a title completed. A seek past the threshold counts as reaching it.")
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: AniListClient.status
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Label { text: qsTr("Pending updates: %1").arg(AniListClient.pendingCount) }
                    Button { text: qsTr("Retry / check connection"); enabled: !AniListClient.busy; onClicked: AniListClient.retryPending() }
                    Button { text: qsTr("Clear pending"); enabled: !AniListClient.busy && AniListClient.pendingCount > 0; onClicked: AniListClient.clearPending() }
                    BusyIndicator { running: AniListClient.busy; visible: running; Layout.preferredWidth: 28; Layout.preferredHeight: 28 }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: MementoPalette.border }
                Label { text: qsTr("Title matching"); font.bold: true }
                CheckBox { id: playing; text: qsTr("Use the currently playing video"); checked: true }
                ComboBox {
                    id: librarySelection
                    visible: !playing.checked
                    Layout.fillWidth: true
                    model: EpisodeLibrary.library
                    textRole: "title"
                    valueRole: "id"
                }
                AniListMappingPane {
                    Layout.fillWidth: true
                    targetKey: playing.checked ? AniListClient.currentKey : (librarySelection.currentValue || "")
                    targetTitle: playing.checked ? AniListClient.currentTitle : librarySelection.currentText
                }
            }
        }
    }
}
