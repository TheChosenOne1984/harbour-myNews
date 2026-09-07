import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

/*
 * The main view (plan section 9): clusters newest first, one row per story however many
 * publishers ran it.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    /*
     * Section 9: the delegate height is fixed. Variable heights combined with images
     * arriving asynchronously make the list jump under the reader's finger.
     *
     * "Fixed" has to mean the row reserves space for its *maximum* content -- two lines of
     * headline, two of teaser, one of sources -- rather than being sized from the
     * thumbnail and letting longer text spill into the row below. The line heights are
     * measured from real Labels rather than guessed from the pixel size, because the
     * factor between the two depends on the font.
     */
    Label { id: titleMetrics; visible: false; text: "Xg"; font.pixelSize: Theme.fontSizeSmall }
    Label { id: smallMetrics; visible: false; text: "Xg"; font.pixelSize: Theme.fontSizeExtraSmall }

    readonly property real titleHeight: titleMetrics.height * 2
    readonly property real teaserHeight: smallMetrics.height * 2
    readonly property real sourceRowHeight: smallMetrics.height + Theme.paddingSmall
    readonly property real textHeight: titleHeight + teaserHeight + sourceRowHeight + 2 * 2
    readonly property real delegateHeight: textHeight + 2 * Theme.paddingMedium
    readonly property real thumbnailSize: delegateHeight - 2 * Theme.paddingSmall

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: App.clusters
        cacheBuffer: page.delegateHeight * 6

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
                enabled: App.unreadCount > 0
                onClicked: App.markAllRead()
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
            description: App.unreadCount > 0 ? qsTr("%n unread", "", App.unreadCount)
                                             : App.statusMessage
        }

        delegate: ListItem {
            id: item
            contentHeight: page.delegateHeight

            // Captured so the pill delegates below do not have to reach into the outer
            // model context past the Repeater's own.
            readonly property bool isUnread: model.unread

            onClicked: {
                App.markClusterRead(model.clusterId)
                pageStack.push(Qt.resolvedUrl("ArticlePage.qml"), {
                    clusterId: model.clusterId,
                    articleTitle: model.title,
                    articleLink: model.link,
                    memberCount: model.memberCount
                })
            }

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Open in browser")
                    onClicked: Qt.openUrlExternally(model.link)
                }
                MenuItem {
                    visible: item.isUnread
                    text: qsTr("Mark as read")
                    onClicked: App.markClusterRead(model.clusterId)
                }
            }

            Thumbnail {
                id: thumb
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: page.thumbnailSize
                height: page.thumbnailSize
                source: model.sources.length > 0 ? model.sources[0] : ""
                articleId: model.leadArticleId
                // The local file, never the publisher's URL: the list must not make a
                // network request per visible row, and an already-downscaled JPEG is what
                // keeps scrolling smooth (section 8).
                imageUrl: model.thumbnailPath !== "" ? "file://" + model.thumbnailPath : ""
                dimmed: !item.isUnread
            }

            Column {
                anchors {
                    left: thumb.right
                    leftMargin: Theme.paddingMedium
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                spacing: 2
                clip: true

                Label {
                    width: parent.width
                    height: page.titleHeight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignTop
                    font.pixelSize: Theme.fontSizeSmall
                    color: item.isUnread
                        ? (item.highlighted ? Theme.highlightColor : Theme.primaryColor)
                        : Theme.secondaryColor
                    text: model.title
                }

                Label {
                    width: parent.width
                    height: page.teaserHeight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignTop
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: model.teaser
                }

                Row {
                    width: parent.width
                    height: page.sourceRowHeight
                    spacing: Theme.paddingSmall
                    clip: true

                    Repeater {
                        model: visibleSources
                        delegate: SourcePill {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData
                            dimmed: !item.isUnread
                        }
                    }

                    SourcePill {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: hiddenSourceCount > 0
                        dimmed: !item.isUnread
                        //: more contributing sources than fit on the row
                        text: qsTr("+%1").arg(hiddenSourceCount)
                    }

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: App.relativeTime(published)
                    }
                }
            }
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
}
