/* Sigeon OS Calamares slideshow. */
import QtQuick 2.0;
import calamares.slideshow 1.0;

Presentation
{
    id: presentation

    function nextSlide() {
        console.log("Sigeon OS slideshow next slide");
        presentation.goToNextSlide();
    }

    Timer {
        id: advanceTimer
        interval: 5000
        running: presentation.activatedInCalamares
        repeat: true
        onTriggered: nextSlide()
    }

    Slide {

    anchors.fill: parent

    Image {
        id: background
        source: "wallpaper.png"
        width: parent.width; height: parent.height
        fillMode: Image.PreserveAspectCrop
        anchors.fill: parent
    }

    Image {
        id: logo
        source: "logo.png"
        width: 220
        height: 220
        anchors.centerIn: parent
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: logo.bottom
        anchors.topMargin: 24
        text: "Sigeon OS"
        font.pixelSize: 36
        color: "#ffffff"
        horizontalAlignment: Text.Center
    }

    }

    function onActivate() {
        console.log("Sigeon OS slideshow activated");
        presentation.currentSlide = 0;
    }

    function onLeave() {
        console.log("Sigeon OS slideshow deactivated");
    }

}
