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

    // Consent gate. The catalogue is the only address the app contacts that does not
    // belong to a publisher, so nothing is fetched before the user has been told which
    // host that is and has agreed. Shown here rather than at startup: someone who only
    // adds feeds by URL is never asked at all.
    //
    // One sentence, and it names no provider: the host is shown from the setting, so the
    // prompt stays true if the address is changed. The buttons below say what the
    // alternatives are; spelling them out again in prose only buries the question.
    SilicaFlickable {
        anchors.fill: parent
        visible: App.needsCatalogConsent || App.catalogDeclined
        contentHeight: consentColumn.height + Theme.paddingLarge

        Column {
            id: consentColumn
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("Add sources") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.primaryColor
                text: App.catalogDeclined
                    ? qsTr("The feed catalogue is not being downloaded.")
                    //: consent prompt: the whole of it, deliberately one sentence
                    : qsTr("The suggested sources are downloaded from a server on the "
                           + "internet:")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                visible: !App.catalogDeclined
                color: Theme.highlightColor
                font.family: Theme.fontFamilyHeading
                text: Settings.catalogHost
            }

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: App.catalogDeclined ? qsTr("Download the catalogue")
                                          : qsTr("Download it")
                onClicked: App.setCatalogConsent(true)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !App.catalogDeclined
                text: qsTr("No, thank you")
                onClicked: App.setCatalogConsent(false)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Add feed by URL")
                onClicked: pageStack.push(Qt.resolvedUrl("AddFeedPage.qml"))
            }
        }

        VerticalScrollDecorator { }
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        visible: !App.needsCatalogConsent && !App.catalogDeclined
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
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    visible: model.description !== ""
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: model.description
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: model.subscribed ? Theme.secondaryHighlightColor
                                            : Theme.secondaryColor
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

    // The catalogue is fetched when this page is opened, not at startup: someone who only
    // adds feeds by URL never causes the request at all. refreshCatalog(false) honours the
    // weekly cadence, so opening the page repeatedly costs nothing.
    Component.onCompleted: {
        if (!App.needsCatalogConsent && !App.catalogDeclined)
            App.refreshCatalog(false)
    }
}
