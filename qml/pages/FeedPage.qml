import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Plan section 9: the main view is a SilicaListView of clusters, newest first, with
 * fixed-height delegates. The cluster model arrives with milestone 4; for now the page
 * carries the navigation, the refresh action and an empty state that explains itself.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: null

        PullDownMenu {
            busy: App.busy

            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Subscriptions")
                onClicked: pageStack.push(Qt.resolvedUrl("SubscriptionsPage.qml"))
            }
            MenuItem {
                text: qsTr("Mark all as read")
                enabled: false
                onClicked: {} // milestone 4
            }
            MenuItem {
                text: qsTr("Refresh")
                enabled: App.subscriptionCount > 0 && !App.busy
                onClicked: App.refresh()
            }
        }

        header: PageHeader {
            //: application title shown above the article list
            title: qsTr("myNews")
            description: App.statusMessage
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            text: App.subscriptionCount === 0
                ? qsTr("No sources yet")
                : qsTr("No articles yet")
            hintText: App.subscriptionCount === 0
                ? qsTr("Pull down to open subscriptions and add some sources.")
                : qsTr("Pull down to refresh.")
        }

        VerticalScrollDecorator { }
    }

    // Deliberately no catalogue fetch here. Nothing is contacted until the user opens the
    // catalogue page and agrees to it (plan section 10); refreshCatalog() would be a no-op
    // before then anyway, but starting one from the first screen would be the wrong shape.
}
