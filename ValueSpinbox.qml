import QtQuick

Rectangle {
    id: spinbox
    border.color: "cyan"
    radius: 3
    width: 50; height: 40

    property real value: 0;
    property real increment: 1;

    Text {
        anchors.centerIn: parent
        text: spinbox.value.toFixed(1)
    }

    WheelHandler {
        property: "value"
        // acceptedButtons: Qt.NoButton
        // acceptedDevices: PointerDevice.AllDevices
        // acceptedPointerTypes: PointerDevice.AllPointerTypes
    }
}
