import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Plan section 9: unread count plus the current lead headline, refresh as cover action.
 * The counts and headline are placeholders until the store lands (milestone 3).
 */
CoverBackground {
    id: cover

    readonly property int unreadCount: App.unreadCount
    readonly property string leadHeadline: App.leadHeadline

    Column {
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            margins: Theme.paddingLarge
        }
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeHuge
            color: Theme.primaryColor
            text: cover.unreadCount
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            //: cover label under the number of unread articles
            text: qsTr("%n unread", "", cover.unreadCount)
        }

        Label {
            width: parent.width
            visible: cover.leadHeadline !== ""
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.highlightColor
            text: cover.leadHeadline
        }
    }

    CoverActionList {
        // CoverAction itself carries only an icon and a signal; the list is what can be
        // switched off. Assigning enabled to the action left the button live and logged an
        // error on every cover load.
        enabled: App.subscriptionCount > 0 && !App.busy

        CoverAction {
            iconSource: "image://theme/icon-cover-refresh"
            onTriggered: App.refresh()
        }
    }
}
