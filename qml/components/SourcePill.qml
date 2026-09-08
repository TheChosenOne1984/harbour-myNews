import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * One contributing source under a cluster headline (plan section 9). A publisher name as
 * a descriptive reference, never a logo -- a bundled logo would imply a partnership that
 * does not exist (section 10).
 */
Rectangle {
    id: pill

    property string text
    property bool dimmed
    property bool highlighted

    height: label.height + Theme.paddingSmall
    width: label.width + 2 * Theme.paddingSmall
    radius: height / 4
    color: highlighted ? Theme.rgba(Theme.highlightColor, 0.40)
                       : (dimmed ? Theme.rgba(Theme.secondaryColor, 0.10)
                                 : Theme.rgba(Theme.highlightColor, 0.15))

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Theme.fontSizeExtraSmall
        color: pill.dimmed ? Theme.secondaryColor : Theme.highlightColor
        text: pill.text
    }
}
