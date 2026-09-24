import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Ripose.Memento

ScrollView {
    id: root
    contentWidth: availableWidth
    FolderDialog {
        id: folderDialog
        title: qsTr("Choose Migaku Export Folder")
        currentFolder: MigakuClient.exportFolder
        onAccepted: MigakuClient.exportFolder = selectedFolder
    }
    ColumnLayout {
        width: root.availableWidth
        spacing: 18
        Label {
            Layout.margins: 20
            text: qsTr("Migaku media export")
            font.pixelSize: 24
            font.bold: true
        }
        CheckBox {
            Layout.leftMargin: 20
            text: qsTr("Enable Migaku integration")
            checked: MigakuClient.enabled
            onClicked: MigakuClient.enabled = checked
        }
        Label { Layout.leftMargin: 20; text: qsTr("Export folder") }
        RowLayout {
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.fillWidth: true
            TextField {
                Layout.fillWidth: true
                readOnly: true
                selectByMouse: true
                text: MigakuClient.exportFolderPath
                placeholderText: qsTr("Choose where images and audio will be saved")
            }
            Button { text: qsTr("Choose folder…"); onClicked: folderDialog.open() }
            Button {
                text: qsTr("Open folder")
                enabled: MigakuClient.exportFolderPath.length > 0
                onClicked: MigakuClient.openExportFolder()
            }
        }
        Label {
            Layout.margins: 20
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Pause on a subtitle and press Ctrl+Shift+M, choose Export Image " +
                "and Sentence Audio from the player menu, or click Migaku in a dictionary result.\n\n" +
                "Memento saves two separate files in this folder: a JPG screenshot and " +
                "an MP3 of the current sentence. Both use the episode name and playback " +
                "timestamp. Repeated captures receive a numbered suffix.\n\n" +
                "Example: Episode 27 - 00-12-34-500.jpg and Episode 27 - 00-12-34-500.mp3\n\n" +
                "In Migaku’s Card Creator, attach the JPG under Images and the MP3 " +
                "under Sentence audio. Folder and enable settings save immediately.")
        }
        Label {
            Layout.margins: 20
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            visible: MigakuClient.lastExport.length > 0
            text: qsTr("Last saved files:\n%1").arg(MigakuClient.lastExport)
        }
        Item { Layout.fillHeight: true }
    }
}
