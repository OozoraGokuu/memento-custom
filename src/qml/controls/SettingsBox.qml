import QtQuick
import QtQuick.Controls
import Ripose.Memento

GroupBox {
    id: root

    Binding {
        target: root.background
        property: "visible"
        value: false
        when: Features.isUnix && root.background !== null
    }

    Binding {
        target: root.label
        property: "font.bold"
        value: true
        when: Features.isUnix && root.label !== null
    }

    Binding {
        target: root.label
        property: "verticalAlignment"
        value: Text.AlignTop
        when: Features.isUnix && root.label !== null
    }

    resources: Rectangle {
        parent: Features.isUnix ? root : null
        visible: Features.isUnix
        z: -1
        y: root.topPadding - root.bottomPadding
        width: root.width
        height: root.height - root.topPadding + root.bottomPadding
        color: Qt.styleHints.colorScheme === Qt.ColorScheme.Dark ?
                   MementoPalette.mid : MementoPalette.midlight
        border.color: MementoPalette.border
        radius: 10
    }
}
