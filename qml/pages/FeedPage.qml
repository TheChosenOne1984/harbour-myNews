import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Plan section 9: the main view is a SilicaListView of clusters, newest first, with
 * fixed-height delegates. The model arrives with milestone 3/4; for now the page
 * carries the navigation and the empty state so the shell is real and testable.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: null

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Subscriptions")
                enabled: false
                onClicked: {} // milestone 6
            }
            MenuItem {
                text: qsTr("Mark all as read")
                enabled: false
                onClicked: {} // milestone 3
            }
            MenuItem {
                text: qsTr("Refresh")
                enabled: false
                onClicked: {} // milestone 3
            }
        }

        header: PageHeader {
            //: application title shown above the article list
            title: qsTr("myNews")
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            text: qsTr("No articles yet")
            hintText: qsTr("Add feeds to get started")
        }

        VerticalScrollDecorator { }
    }
}
