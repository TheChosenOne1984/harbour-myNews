import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

/*
 * Full-text search over everything already downloaded (plan section 4).
 *
 * Articles, not clusters: a reader searching for a word wants the piece that used it, and
 * which cluster it landed in is a different question -- one the article page still
 * answers through "other sources".
 *
 * The trigram index matches substrings, so there is no stemming and no word boundary to
 * get wrong; two characters is where a query starts to mean something.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    property string query
    property var hits: []
    property bool searching: false

    readonly property int minimumQueryLength: 2

    function runSearch() {
        var text = query.trim()
        if (text.length < page.minimumQueryLength) {
            hits = []
            searching = false
            return
        }
        searching = true
        App.search(text)
    }

    Connections {
        target: App
        onSearchFinished: {
            // A reply to something already typed past is stale; the newer query is on
            // its way and will answer for itself.
            if (text !== page.query.trim())
                return
            page.hits = hits
            page.searching = false
        }
    }

    // Typing is throttled rather than searched per keystroke: the query crosses to the
    // worker thread and back, and the reader is still typing.
    Timer {
        id: debounce
        interval: 250
        onTriggered: page.runSearch()
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: page.hits

        header: Column {
            width: listView.width

            PageHeader { title: qsTr("Search") }

            SearchField {
                id: field
                width: parent.width
                //: placeholder in the search field
                placeholderText: qsTr("Search headlines and teasers")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onTextChanged: {
                    page.query = text
                    debounce.restart()
                }
                Component.onCompleted: forceActiveFocus()
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: content.height + Theme.paddingMedium

            readonly property bool linkUsable: App.canOpen(modelData.link)

            Column {
                id: content
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                y: Theme.paddingSmall
                spacing: 2

                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeSmall
                    color: modelData.read ? Theme.secondaryColor : Theme.primaryColor
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
                        text: App.relativeTime(modelData.published)
                    }
                }
            }

            onClicked: {
                if (!linkUsable)
                    return
                App.markClusterRead(modelData.clusterId)
                if (Settings.openArticlesExternally) {
                    Qt.openUrlExternally(modelData.link)
                    return
                }
                pageStack.push(Qt.resolvedUrl("ArticlePage.qml"), {
                    clusterId: modelData.clusterId,
                    articleTitle: modelData.title,
                    articleLink: modelData.link,
                    memberCount: 1
                })
            }
        }

        ViewPlaceholder {
            enabled: page.hits.length === 0 && !page.searching
            text: page.query.trim().length < page.minimumQueryLength
                ? qsTr("Search your articles")
                : qsTr("Nothing found")
            hintText: page.query.trim().length < page.minimumQueryLength
                //: %1 is a small number of characters
                ? qsTr("Type at least %1 characters.").arg(page.minimumQueryLength)
                //: reminds the reader that search covers only downloaded articles
                : qsTr("Only articles already downloaded are searched.")
        }

        VerticalScrollDecorator { }
    }

    BusyIndicator {
        anchors.centerIn: parent
        size: BusyIndicatorSize.Medium
        running: page.searching && page.hits.length === 0
    }
}
