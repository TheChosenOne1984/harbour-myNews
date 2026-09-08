import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * The feeds the user has subscribed to.
 *
 * Section 5 requires persistent failure to be surfaced here rather than retried silently
 * forever, so each row carries its health. Section 7 asks for languages we only guessed
 * -- what an OPML import leaves behind -- to be reviewable in one place.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    // Mirrors SubscriptionModel::Health.
    function healthText(health, failureCount) {
        switch (health) {
        case 0: return ""                                     // Ok
        case 1: return qsTr("not fetched yet")                // Unknown
        case 2: return qsTr("last fetch failed")              // Failing
        case 3: return qsTr("failing since %n attempt(s)", "", failureCount)  // Broken
        case 4: return qsTr("switched off")                   // Disabled
        }
        return ""
    }

    function healthColour(health) {
        if (health === 3) return Theme.errorColor        // Broken
        if (health === 2) return Theme.highlightColor    // Failing
        return Theme.secondaryColor
    }

    SilicaListView {
        id: listView
        anchors.fill: parent
        model: App.subscriptions

        PullDownMenu {
            MenuItem {
                text: qsTr("Import subscriptions")
                onClicked: pageStack.push(Qt.resolvedUrl("ImportOpmlPage.qml"))
            }
            MenuItem {
                text: qsTr("Export subscriptions")
                onClicked: App.exportOpml(App.defaultOpmlExportPath())
            }
            MenuItem {
                text: qsTr("Add feed by URL")
                onClicked: pageStack.push(Qt.resolvedUrl("AddFeedPage.qml"))
            }
            MenuItem {
                text: qsTr("Add from catalogue")
                onClicked: pageStack.push(Qt.resolvedUrl("CatalogPage.qml"))
            }
        }

        header: PageHeader {
            title: qsTr("Subscriptions")
            description: qsTr("%n feed(s)", "", App.subscriptions.count)
        }

        delegate: ListItem {
            id: item
            contentHeight: content.height + Theme.paddingMedium

            menu: ContextMenu {
                MenuItem {
                    text: model.enabled ? qsTr("Disable") : qsTr("Enable")
                    onClicked: App.setFeedEnabled(model.feedId, !model.enabled)
                }
                MenuItem {
                    text: qsTr("Change language")
                    onClicked: pageStack.push(Qt.resolvedUrl("LanguagePickerPage.qml"), {
                        feedId: model.feedId,
                        currentLanguage: model.lang,
                        feedTitle: model.title
                    })
                }
                MenuItem {
                    text: qsTr("Unsubscribe")
                    onClicked: item.remorseAction(qsTr("Unsubscribing"), function() {
                        App.unsubscribe(model.feedId)
                    })
                }
            }

            Column {
                id: content
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                y: Theme.paddingSmall

                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: model.title
                    color: model.enabled
                        ? (item.highlighted ? Theme.highlightColor : Theme.primaryColor)
                        : Theme.secondaryColor
                }

                Row {
                    width: parent.width
                    spacing: Theme.paddingSmall

                    Label {
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: model.lang
                    }
                    Label {
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.highlightColor
                        visible: !model.languageIsConfirmed
                        //: marks a feed whose language was guessed rather than chosen
                        text: qsTr("· language unconfirmed")
                    }
                    Label {
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: page.healthColour(model.health)
                        visible: text !== ""
                        text: {
                            var t = page.healthText(model.health, model.failureCount)
                            return t === "" ? "" : "· " + t
                        }
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: App.subscriptions.count === 0
            text: qsTr("No subscriptions yet")
            hintText: qsTr("Pull down to add sources from the catalogue.")
        }

        VerticalScrollDecorator { }
    }
}
