import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.VirtualKeyboard

Window {
    id: window
    width: 640
    height: 400
    visible: true
    title: qsTr("Hello World")

    RowLayout {
        width: parent.width

        TextField {
            Layout.fillWidth: true
            focus: true
        }

        Button {
            id: kbdButton
            checkable: true
            text: "kbd"
            Layout.alignment: Qt.AlignRight
        }
    }

    InputPanel {
        id: inputPanel
        z: 99
        x: 0
        y: window.height
        width: window.width
        active: kbdButton.checked

        states: State {
            name: "visible"
            when: inputPanel.active
            PropertyChanges {
                target: inputPanel
                y: window.height - inputPanel.height
            }
        }
        transitions: Transition {
            from: ""
            to: "visible"
            reversible: true
            ParallelAnimation {
                NumberAnimation {
                    properties: "y"
                    duration: 250
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
}
