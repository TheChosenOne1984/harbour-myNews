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

    readonly property bool webViewReady: webViewLoader.status === Loader.Ready

    function showSources() {
        pageStack.push(Qt.resolvedUrl("ClusterSourcesPage.qml"), {
            clusterId: page.clusterId,
            currentLink: page.articleLink
        })
    }

    PageHeader {
        id: header
        title: page.articleTitle
        _titleItem.maximumLineCount: 2
        _titleItem.wrapMode: Text.Wrap
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
        anchors {
            top: header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        asynchronous: true
        source: "WebViewLoader.qml"
        onLoaded: item.pageUrl = page.articleLink
    }

    /*
     * The page's actions float over the article instead of hanging in a pulley menu.
     * A pulley needs a flickable, the web view is not one, and wrapping it in a
     * SilicaFlickable hands that flickable every vertical drag -- reading the article
     * became impossible because each swipe dragged the header down instead.
     *
     * They step aside while the article is in motion, so they never sit on top of the
     * text the reader is chasing.
     */
    Rectangle {
        id: actionBar
        anchors {
            top: webViewLoader.top
            right: parent.right
            rightMargin: Theme.paddingMedium
            topMargin: Theme.paddingMedium
        }
        width: actions.width + 2 * Theme.paddingMedium
        height: actions.height + 2 * Theme.paddingSmall
        radius: Theme.paddingSmall
        color: Theme.rgba(Theme.overlayBackgroundColor, 0.9)

        opacity: page.webViewReady
                 && !(webViewLoader.item && webViewLoader.item.moving) ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { FadeAnimation { } }

        Row {
            id: actions
            anchors.centerIn: parent
            spacing: Theme.paddingSmall

            IconButton {
                visible: page.memberCount > 1
                icon.source: "image://theme/icon-m-tabs"
                onClicked: page.showSources()

                // The count is what makes the icon mean something: two unlabelled
                // symbols say nothing about how many other outlets ran this story.
                Label {
                    anchors {
                        right: parent.right
                        rightMargin: Theme.paddingSmall
                        bottom: parent.bottom
                        bottomMargin: Theme.paddingSmall
                    }
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.highlightColor
                    text: page.memberCount
                }
            }
            IconButton {
                icon.source: "image://theme/icon-m-website"
                onClicked: Qt.openUrlExternally(page.articleLink)
            }
        }
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
            //: fallback for pages the built-in view cannot show, e.g. paywalled ones
            text: qsTr("Open in browser")
            onClicked: Qt.openUrlExternally(page.articleLink)
        }
        Button {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: page.memberCount > 1
            //: opens the list of other publishers that ran the same story
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
