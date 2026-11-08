pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 800, 800)
    height: Math.min(parent ? parent.height - 48 : 650, 650)
    modal: true
    title: qsTr("Search AniList")
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    function openForCurrentMedia() {
        root.open();
        const query = mappingPane.targetTitle.trim();
        if (query.length >= 2) AniListClient.search(query);
    }

    contentItem: ScrollView {
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 12
            AniListMappingPane {
                id: mappingPane
                Layout.fillWidth: true
                targetKey: AniListClient.currentKey
                targetTitle: {
                    const key = AniListClient.currentKey;
                    const title = JimakuClient.suggestedTitle() || AniListClient.currentTitle;
                    return title.replace(/\s+(?:Season\s*|S)0*\d{1,2}(?:\b.*)?$/i, "").trim() || title;
                }
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
                text: qsTr("Search and link titles without signing in. Connect your account in Settings → AniList Integration to sync episode progress.")
                wrapMode: Text.Wrap
            }
        }
    }
}
