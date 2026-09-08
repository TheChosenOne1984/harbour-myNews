import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

/*
 * The other publishers that ran this story (plan section 9). The point of clustering made
 * usable: read the same event somewhere else, one tap away.
 */
Page {
    id: page

    property var clusterId
    property string currentLink

    property var members: []

    allowedOrientations: defaultAllowedOrientations

    Connections {
        target: App
        onClusterMembersLoaded: {
            if (clusterId === page.clusterId)
                page.members = members
        }
    }

    Component.onCompleted: App.loadClusterMembers(page.clusterId)

    SilicaListView {
        anchors.fill: parent
        model: page.members

        header: PageHeader {
            title: qsTr("Sources")
            description: qsTr("%n article(s) on this story", "", page.members.length)
        }

        delegate: ListItem {
            contentHeight: content.height + Theme.paddingMedium

            Column {
                id: content
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                y: Theme.paddingSmall

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeSmall
                    color: modelData.link === page.currentLink ? Theme.secondaryHighlightColor
                                                               : Theme.primaryColor
                    text: modelData.title
                }
                Row {
                    width: parent.width
                    spacing: Theme.paddingSmall

                    SourcePill {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.source
                        dimmed: modelData.read
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: modelData.link === page.currentLink
                            //: marks the article the reader came from
                            ? qsTr("currently open")
                            : App.relativeTime(modelData.published)
                    }
                }
            }

            onClicked: {
                if (Settings.openArticlesExternally) {
                    Qt.openUrlExternally(modelData.link)
                    return
                }
                /*
                 * pop() followed by push() silently did nothing: pop() starts an animated
                 * transition and PageStack refuses to push during one, so the reader was
                 * dropped back on the list instead of into the article.
                 *
                 * replaceAbove the feed page also keeps the stack flat. Reaching this page
                 * from an article and picking another would otherwise stack article on
                 * article, and each one holds a live web engine -- the thing that ran the
                 * app out of memory before. Going back always lands on the list, and the
                 * article's own pulley leads back to the other sources.
                 */
                var properties = {
                    clusterId: page.clusterId,
                    articleTitle: modelData.title,
                    articleLink: modelData.link,
                    memberCount: page.members.length
                }
                var feedPage = pageStack.find(function (candidate) {
                    return candidate.objectName === "feedPage"
                })
                if (feedPage)
                    pageStack.replaceAbove(feedPage, Qt.resolvedUrl("ArticlePage.qml"),
                                           properties)
                else
                    pageStack.replace(Qt.resolvedUrl("ArticlePage.qml"), properties)
            }
        }

        ViewPlaceholder {
            enabled: page.members.length === 0
            text: qsTr("Loading…")
        }

        VerticalScrollDecorator { }
    }
}
