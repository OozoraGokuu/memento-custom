import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

ColumnLayout {
    id: root
    property string targetKey: ""
    property string targetTitle: ""
    property int selectedResult: -1
    readonly property var link: {
        const revision = AniListClient.revision;
        return AniListClient.mapping(targetKey);
    }
    spacing: 10
    onTargetKeyChanged: { selectedResult = -1; searchField.text = targetTitle; }

    Label {
        Layout.fillWidth: true
        text: root.targetTitle.length ? root.targetTitle : qsTr("Open a video or select a library entry first.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font.bold: true
    }
    Label {
        Layout.fillWidth: true
        text: root.link.id ? qsTr("Linked: %1 · AniList #%2 · episode offset %3").arg(root.link.title).arg(root.link.id).arg(root.link.offset) : qsTr("No title linked. Choose the exact anime and season below.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: searchField
            Layout.fillWidth: true
            text: root.targetTitle
            placeholderText: qsTr("Search AniList titles")
            onAccepted: { root.selectedResult = -1; AniListClient.search(text); }
        }
        Button {
            text: qsTr("Search")
            enabled: root.targetKey.length > 0 && searchField.text.trim().length >= 2
            onClicked: { root.selectedResult = -1; AniListClient.search(searchField.text); }
        }
    }
    ListView {
        Layout.fillWidth: true
        Layout.preferredHeight: 190
        clip: true
        model: AniListClient.results
        onModelChanged: root.selectedResult = -1
        ScrollBar.vertical: ScrollBar { }
        delegate: ItemDelegate {
            required property var modelData
            required property int index
            width: ListView.view.width
            highlighted: root.selectedResult === index
            text: modelData.title + " · " + (modelData.year || "?") + " · " + modelData.format + " · " + (modelData.episodes || "?") + qsTr(" episodes")
            onClicked: root.selectedResult = index
        }
    }
    RowLayout {
        Label { text: qsTr("Episode offset") }
        SpinBox {
            id: offset
            from: -9999; to: 9999; editable: true
            value: root.link.offset || 0
        }
        Button {
            text: qsTr("Link selected title")
            enabled: root.targetKey.length > 0 && root.selectedResult >= 0 && !AniListClient.busy
            onClicked: AniListClient.linkTitle(root.targetKey, root.selectedResult, offset.value)
        }
        Button {
            text: qsTr("Unlink")
            enabled: !!root.link.id && !AniListClient.busy
            onClicked: AniListClient.unlinkTitle(root.targetKey)
        }
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("AniList episode = filename episode + offset. For example, episode 13 with offset −12 becomes episode 1. Link folders containing one AniList season.")
        wrapMode: Text.Wrap
    }
    RowLayout {
        visible: root.targetKey.length > 0 && root.targetKey === AniListClient.currentKey
        Label { text: qsTr("Playing episode") }
        SpinBox {
            from: 0; to: 9999; editable: true
            value: AniListClient.currentEpisode
            onValueModified: AniListClient.setCurrentEpisode(value)
        }
        Button {
            text: qsTr("Sync now")
            enabled: AniListClient.connected && AniListClient.enabled && !!root.link.id && AniListClient.currentEpisode > 0 && !AniListClient.busy
            onClicked: AniListClient.syncNow()
        }
    }
    Label {
        visible: root.targetKey === AniListClient.currentKey && AniListClient.currentEpisode === 0
        Layout.fillWidth: true
        text: qsTr("Episode number is unknown. Set it above before syncing this video.")
        wrapMode: Text.Wrap
    }
}
