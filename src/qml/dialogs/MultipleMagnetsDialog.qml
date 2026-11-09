import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Ripose.Memento

Dialog {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(600, parent.width - 32)
    height: Math.min(460, parent.height - 32)
    modal: true
    title: qsTr("Add Multiple Magnets")
    property string errorText: ""

    onOpened: {
        errorText = "";
        collectionName.forceActiveFocus();
    }

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            Layout.fillWidth: true
            text: qsTr("Combine several magnets into one named library entry. Existing individual entries are merged with their watched progress.")
            wrapMode: Text.WordWrap
        }
        TextField {
            id: collectionName
            objectName: "magnetCollectionName"
            Layout.fillWidth: true
            placeholderText: qsTr("Library title (for example, a series or season)")
            maximumLength: 200
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            TextArea {
                id: magnets
                objectName: "multipleMagnetLinks"
                placeholderText: qsTr("Paste one magnet link per line (2–100 links)")
                wrapMode: TextEdit.WrapAnywhere
                selectByMouse: true
            }
        }
        Label {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
    }
    footer: DialogButtonBox {
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.reject()
        }
        Button {
            text: qsTr("Add to Library")
            enabled: collectionName.text.trim().length > 0 && magnets.text.trim().length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (EpisodeLibrary.addMultipleMagnets(collectionName.text, magnets.text) < 0) {
                    root.errorText = EpisodeLibrary.lastError;
                    return;
                }
                MementoSettings.windowLibrary = true;
                collectionName.clear();
                magnets.clear();
                root.accept();
            }
        }
    }
}
