import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

/*
 * The main view (plan section 9): clusters newest first, one row per story however many
 * publishers ran it.
 */
Page {
    id: page
    // Named so pages further up the stack can unwind back to it (ClusterSourcesPage).
    objectName: "feedPage"

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

    /*
     * The pulley menu is only reachable at the top of the list, so after scrolling past
     * three hundred articles, filtering meant flicking all the way back up first.
     *
     * This bar appears exactly when that problem exists -- once the list has scrolled far
     * enough that the pulley is gone -- and floats over the list rather than displacing
     * it. Keeping the list filling the page matters: with the list anchored below a
     * permanent bar, Silica draws the pulley's backdrop in the exposed strip and it sits
     * there as a solid orange band.
     */
    Item {
        id: filterBar
        z: 2
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Theme.itemSizeExtraSmall
        opacity: listView.contentY > listView.originY + 2 * page.delegateHeight ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { FadeAnimation { duration: 150 } }

        Rectangle {
            anchors.fill: parent
            // A dark scrim: the bar floats over article rows and over the ambience
            // wallpaper, and has to stay legible against both.
            color: Qt.rgba(0, 0, 0, 0.82)
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: Theme.rgba(Theme.primaryColor, 0.2)
        }

        BackgroundItem {
            anchors { left: parent.left; right: toTop.left; top: parent.top; bottom: parent.bottom }
            onClicked: pageStack.push(Qt.resolvedUrl("FilterPage.qml"))

            Label {
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: parent.right
                    rightMargin: Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: App.filterActive ? Theme.highlightColor : Theme.primaryColor
                text: App.filterActive
                    ? qsTr("Filter: %1").arg(App.filterSummary)
                    : qsTr("Filter · %n article group(s)", "", listView.count)
            }
        }

        IconButton {
            id: toTop
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium
                verticalCenter: parent.verticalCenter
            }
            width: Theme.itemSizeExtraSmall
            height: parent.height
            icon.source: "image://theme/icon-m-up"
            // Going back to the top also puts the pulley menu back within reach, which is
            // the other half of the same problem.
            onClicked: listView.scrollToTop()
        }
    }

    // One for the page rather than one per row: the share sheet is modal anyway.
    ShareLink { id: shareLink }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: App.clusters
        cacheBuffer: page.delegateHeight * 8

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
                //: opens full-text search over downloaded articles
                text: qsTr("Search")
                onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
            }
            MenuItem {
                //: opens the page that narrows the article list
                text: App.filterActive ? qsTr("Filter: %1").arg(App.filterSummary)
                                       : qsTr("Filter")
                onClicked: pageStack.push(Qt.resolvedUrl("FilterPage.qml"))

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

        // Older stories arrive as the reader gets near the end, rather than the list
        // stopping at an arbitrary number. Triggered a little before the end so the pause
        // falls where a flick is already slowing down.
        onContentYChanged: {
            prefetchTimer.restart()
            if (App.hasMoreArticles && !App.busy
                    && contentY + height > contentHeight - 8 * page.delegateHeight) {
                App.loadMoreArticles()
            }
        }

        PushUpMenu {
            visible: App.hasMoreArticles
            MenuItem {
                text: qsTr("Load older articles")
                onClicked: App.loadMoreArticles()
            }
        }

        header: PageHeader {
            //: application title shown above the article list
            title: qsTr("myNews")
            // A filtered list that does not say so looks like a list with articles
            // missing, so the header always names the filter when one is set.
            description: App.filterActive
                ? qsTr("Filtered: %1").arg(App.filterSummary)
                : (App.unreadCount > 0 ? qsTr("%n unread", "", App.unreadCount)
                                       : App.statusMessage)
        }

        delegate: ListItem {
            id: item
            contentHeight: page.delegateHeight

            // Captured so the pill delegates below do not have to reach into the outer
            // model context past the Repeater's own.
            readonly property bool isUnread: model.unread

            // Links come from feeds and are not trusted; App.canOpen is the single rule.
            readonly property bool linkUsable: App.canOpen(model.link)

            onClicked: {
                App.markClusterRead(model.clusterId)
                if (!linkUsable) {
                    if (model.memberCount > 1)
                        item.showSources()
                    return
                }
                if (Settings.openArticlesExternally) {
                    Qt.openUrlExternally(model.link)
                    return
                }
                pageStack.push(Qt.resolvedUrl("ArticlePage.qml"), {
                    clusterId: model.clusterId,
                    articleTitle: model.title,
                    articleLink: model.link,
                    memberCount: model.memberCount
                })
            }

            function showSources() {
                pageStack.push(Qt.resolvedUrl("ClusterSourcesPage.qml"), {
                    clusterId: model.clusterId,
                    currentLink: ""
                })
            }

            menu: ContextMenu {
                MenuItem {
                    visible: model.memberCount > 1
                    //: only offered for clustered stories, so never singular
                    text: qsTr("All %1 sources").arg(model.memberCount)
                    onClicked: item.showSources()
                }
                MenuItem {
                    visible: item.linkUsable
                    text: qsTr("Open in browser")
                    onClicked: Qt.openUrlExternally(model.link)
                }
                MenuItem {
                    visible: item.linkUsable
                    //: shares the article's address through the system share sheet
                    text: qsTr("Share")
                    onClicked: shareLink.share(model.title, model.link)
                }
                MenuItem {
                    text: item.isUnread ? qsTr("Mark as read") : qsTr("Mark as unread")
                    onClicked: App.markClusterRead(model.clusterId, item.isUnread)
                }
            }

            Thumbnail {
                id: thumb
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: page.thumbnailSize
                height: page.thumbnailSize

                // Tapping the picture marks the story read or unread without opening it.
                // Sailfish reserves horizontal swipes for the page stack, so a swipe here
                // would fight the back gesture; a tap on a target this size does not.
                MouseArea {
                    anchors.fill: parent
                    onClicked: App.markClusterRead(model.clusterId, item.isUnread)
                }
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

                Item {
                    width: parent.width
                    height: page.sourceRowHeight

                    /*
                     * A single source is just a name; several are the point of the app.
                     * Pills are therefore reserved for stories more than one publisher
                     * ran, so a cluster is recognisable at a glance instead of by counting
                     * labels -- and the pill row is the way in to the full list, because
                     * the thing that signals the cluster should also be the thing you tap.
                     *
                     * The age anchors right and the names give way to it: a truncated
                     * publisher still reads, a missing timestamp leaves the row undated.
                     */
                    Label {
                        id: age
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: App.relativeTime(published)
                    }

                    Label {
                        anchors {
                            left: parent.left
                            right: age.left
                            rightMargin: Theme.paddingMedium
                            verticalCenter: parent.verticalCenter
                        }
                        visible: model.memberCount <= 1
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: model.sources.length > 0 ? model.sources[0] : ""
                    }

                    MouseArea {
                        id: sourcesArea
                        anchors {
                            left: parent.left
                            right: age.left
                            rightMargin: Theme.paddingMedium
                            top: parent.top
                            bottom: parent.bottom
                        }
                        visible: model.memberCount > 1
                        clip: true
                        onClicked: item.showSources()

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.paddingSmall

                            Repeater {
                                model: visibleSources
                                delegate: SourcePill {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData
                                    dimmed: !item.isUnread
                                    highlighted: sourcesArea.pressed
                                }
                            }

                            SourcePill {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: hiddenSourceCount > 0
                                dimmed: !item.isUnread
                                highlighted: sourcesArea.pressed
                                //: more contributing sources than fit on the row
                                text: qsTr("+%1").arg(hiddenSourceCount)
                            }
                        }
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: listView.count === 0
            text: App.subscriptionCount === 0
                ? qsTr("No sources yet")
                : (App.filterActive ? qsTr("Nothing matches the filter")
                                    : qsTr("No articles yet"))
            // An empty list under a filter needs a way out, or it looks like data loss.
            hintText: App.subscriptionCount === 0
                ? qsTr("Pull down to open subscriptions and add some sources.")
                : (App.filterActive ? qsTr("Pull down to change or clear the filter.")
                                    : qsTr("Pull down to refresh."))
        }


        VerticalScrollDecorator { }

        /*
         * Ask for the pictures of the rows about to arrive, not only the ones on screen.
         * Requesting only what is visible loses every image on a fast flick: the rows are
         * gone before their fetches come back.
         *
         * Throttled, because contentY changes on every frame and a request per frame would
         * be pointless work.
         */
        Timer {
            id: prefetchTimer
            interval: 120
            onTriggered: {
                if (listView.count === 0)
                    return
                var first = Math.max(0, Math.floor((listView.contentY - listView.originY)
                                                   / page.delegateHeight))
                App.prefetchThumbnails(first, App.thumbnailPrefetchRows)
            }
        }

        Component.onCompleted: prefetchTimer.restart()

        Connections {
            target: App.clusters
            onCountChanged: prefetchTimer.restart()
        }
    }

    /*
     * Coming back from an article used to land the reader at the top of the list. The
     * model no longer resets for an in-place change, which was the main cause; this keeps
     * the position across the page transition itself, which is cheap insurance.
     */
    property real savedContentY: 0

    onStatusChanged: {
        if (status === PageStatus.Inactive) {
            savedContentY = listView.contentY
        } else if (status === PageStatus.Active && savedContentY > 0) {
            var maximum = Math.max(0, listView.contentHeight - listView.height)
            listView.contentY = Math.min(savedContentY, maximum)
        }
    }
}
