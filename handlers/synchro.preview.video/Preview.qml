import QtQuick
import QtMultimedia
import Synchro.Handler 1.0
import Synchro.Theme 1.0

HandlerSurface {
    id: root

    implicitWidth: 720
    implicitHeight: 480

    Video {
        id: player
        anchors.fill: parent
        source: root.file
        fillMode: VideoOutput.PreserveAspectFit
        muted: true
        volume: 0
        autoPlay: true
        loops: MediaPlayer.Infinite
        focus: false
        endOfStreamPolicy: VideoOutput.KeepLastFrame
    }

    Text {
        visible: player.error !== MediaPlayer.NoError
        anchors.centerIn: parent
        width: parent.width - Theme.space(24)
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: player.errorString.length > 0 ? player.errorString
                                            : "Could not play video"
        color: Theme.muted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontBody
    }

    Component.onDestruction: player.stop()
    Keys.onEscapePressed: if (root.host) root.host.close()
}
