import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * The curated starter list. The user picks from it -- subscribing to everything is not
 * the default (plan section 10). Nothing is bundled, so this page is also where a first
 * start without a reachable host has to explain itself rather than just look broken.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: App.catalog

        PullDownMenu {
            MenuItem {
                text: qsTr("Add feed by URL")
                onClicked: pageStack.push(Qt.resolvedUrl("AddFeedPage.qml"))
            }
            MenuItem {
                text: qsTr("Reload catalogue")
                onClicked: App.refreshCatalog(true)
            }
        }

        header: Column {
            width: listView.width

            PageHeader {
                //: title of the page listing feeds offered by the catalogue
                title: qsTr("Add sources")
                description: App.catalog.count === App.catalog.totalCount
                    ? qsTr("%n source(s) available", "", App.catalog.totalCount)
                    : qsTr("%1 of %2 shown").arg(App.catalog.count).arg(App.catalog.totalCount)
            }

            ComboBox {
                width: parent.width
                label: qsTr("Category")
                visible: App.catalog.totalCount > 0

                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("All")
                        onClicked: App.catalog.categoryFilter = ""
                    }
                    Repeater {
                        model: App.catalog.categoryIds()
                        MenuItem {
                            text: App.catalog.categoryName(modelData)
                            onClicked: App.catalog.categoryFilter = modelData
                        }
                    }
                }
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: content.height + Theme.paddingMedium

            Column {
                id: content
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                y: Theme.paddingSmall

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: model.title
                    color: model.subscribed ? Theme.secondaryHighlightColor
                                            : (item.highlighted ? Theme.highlightColor
                                                                : Theme.primaryColor)
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: model.subscribed
                        //: shown on a catalogue entry the user already subscribes to
                        ? qsTr("Added · %1").arg(model.categoryName)
                        : model.categoryName + " · " + model.lang
                }
            }

            onClicked: {
                if (model.subscribed) {
                    App.unsubscribeUrl(model.url)
                } else {
                    App.subscribeFromCatalog(index)
                }
            }
        }

        ViewPlaceholder {
            enabled: App.catalog.totalCount === 0
            text: App.catalogError !== ""
                ? qsTr("Catalogue unavailable")
                : qsTr("Loading catalogue…")
            // Section 10: not bundled, so the first start needs to say *why* it is empty.
            hintText: App.catalogError !== ""
                ? App.catalogError + "\n" + qsTr("Pull down to retry, or add a feed by URL.")
                : ""
        }

        VerticalScrollDecorator { }
    }
}
