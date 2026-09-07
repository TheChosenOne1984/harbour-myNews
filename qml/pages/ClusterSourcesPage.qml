import QtQuick 2.0
import Sailfish.Silica 1.0

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
                Label {
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: modelData.link === page.currentLink
                        //: marks the article the reader came from
                        ? qsTr("currently open")
                        : App.relativeTime(modelData.published)
                }
            }

            onClicked: {
                pageStack.pop()
                pageStack.push(Qt.resolvedUrl("ArticlePage.qml"), {
                    clusterId: page.clusterId,
                    articleTitle: modelData.title,
                    articleLink: modelData.link,
                    memberCount: page.members.length
                })
            }
        }

        ViewPlaceholder {
            enabled: page.members.length === 0
            text: qsTr("Loading…")
        }

        VerticalScrollDecorator { }
    }
}
