import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * The article itself (plan section 9). No reader-mode extraction and no print-view
 * substitution: the publisher's page is shown as the publisher serves it (section 11).
 */
Page {
    id: page

    property var clusterId
    property string articleTitle
    property string articleLink
    property int memberCount: 1

    allowedOrientations: defaultAllowedOrientations

    /*
     * Header and pulley menu are components, not plain children, because they have two
     * possible homes. When the web view loads they mount inside its flickable -- a
     * PullDownMenu only reaches the reader's drag from there, and anywhere else it
     * swallows every downward swipe meant for the article. When the view is unavailable
     * the same header mounts on the Page above the fallback.
     */
    Component {
        id: headerComponent
        PageHeader {
            title: page.articleTitle
            _titleItem.maximumLineCount: 2
            _titleItem.wrapMode: Text.Wrap
        }
    }

    Component {
        id: menuComponent
        PullDownMenu {
            MenuItem {
                visible: page.memberCount > 1
                //: opens the list of other publishers that ran the same story
                text: qsTr("Other sources (%1)").arg(page.memberCount - 1)
                onClicked: page.showSources()
            }
            MenuItem {
                //: fallback for pages the built-in view cannot show, e.g. paywalled ones
                text: qsTr("Open in browser")
                onClicked: Qt.openUrlExternally(page.articleLink)
            }
        }
    }

    function showSources() {
        pageStack.push(Qt.resolvedUrl("ClusterSourcesPage.qml"), {
            clusterId: page.clusterId,
            currentLink: page.articleLink
        })
    }

    /*
     * Leaving the article gives the engine's memory back. Done on Inactive rather than on
     * destruction, because by then the loader's item may already be gone -- and Inactive
     * also covers pushing another page on top, where the article is equally out of sight.
     */
    onStatusChanged: {
        if (status === PageStatus.Inactive && webViewLoader.item)
            webViewLoader.item.releaseMemory()
    }

    Loader {
        id: webViewLoader
        anchors.fill: parent
        asynchronous: true
        source: "WebViewLoader.qml"
        onLoaded: {
            item.header = headerComponent
            item.menu = menuComponent
            item.pageUrl = page.articleLink
        }
    }

    // The web view brings its own header once it is up; until then, and if it never
    // arrives, the page supplies one.
    Loader {
        id: fallbackHeader
        width: parent.width
        sourceComponent: webViewLoader.status === Loader.Ready ? undefined : headerComponent
    }

    // Shown when the web view is unavailable -- if a device ever ships without it. Failing
    // to an explanation and working buttons beats a blank page.
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.horizontalPageMargin
        spacing: Theme.paddingLarge
        visible: webViewLoader.status === Loader.Error

        Label {
            width: parent.width
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.secondaryColor
            text: qsTr("The built-in browser is not available here.")
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Open in browser")
            onClicked: Qt.openUrlExternally(page.articleLink)
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.memberCount > 1
            text: qsTr("Other sources (%1)").arg(page.memberCount - 1)
            onClicked: page.showSources()
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        size: BusyIndicatorSize.Large
        running: webViewLoader.status === Loader.Loading
    }
}
