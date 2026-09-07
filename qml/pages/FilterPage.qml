import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Narrowing the article list.
 *
 * Twenty feeds produce well over a thousand articles a day, which is more than anyone
 * reads top to bottom. Region, source, age and unread are the four axes that actually
 * correspond to how someone looks for something.
 *
 * Applied immediately rather than behind an "apply" button: the reader sees the count
 * change and can adjust, which is how a filter is judged.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    readonly property var ageOptions: [
        { hours: 0,   label: qsTr("Any age") },
        { hours: 6,   label: qsTr("Last 6 hours") },
        { hours: 24,  label: qsTr("Last day") },
        { hours: 72,  label: qsTr("Last 3 days") },
        { hours: 168, label: qsTr("Last week") }
    ]

    function ageLabel(hours) {
        for (var i = 0; i < ageOptions.length; ++i) {
            if (ageOptions[i].hours === hours)
                return ageOptions[i].label
        }
        return ageOptions[0].label
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Clear filter")
                enabled: App.filterActive
                onClicked: App.clearFilter()
            }
        }

        Column {
            id: column
            width: page.width

            PageHeader {
                title: qsTr("Filter")
                description: qsTr("%n article group(s) shown", "", App.clusters.count)
            }

            TextSwitch {
                checked: App.filterUnreadOnly
                text: qsTr("Unread only")
                onClicked: App.filterUnreadOnly = !App.filterUnreadOnly
            }

            ComboBox {
                width: parent.width
                label: qsTr("Region")
                //: shown when no region filter is set
                value: App.filterCategory === "" ? qsTr("All regions")
                                                 : App.catalog.categoryName(App.filterCategory)
                // Only regions the user is actually subscribed to, so the list cannot
                // offer something that filters to nothing.
                enabled: App.subscribedCategories.length > 0

                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("All regions")
                        onClicked: App.filterCategory = ""
                    }
                    Repeater {
                        model: App.subscribedCategories
                        MenuItem {
                            text: App.catalog.categoryName(modelData)
                            onClicked: App.filterCategory = modelData
                        }
                    }
                }
            }

            ComboBox {
                width: parent.width
                label: qsTr("Source")
                value: {
                    if (App.filterFeedId < 0)
                        return qsTr("All sources")
                    var row = App.subscriptions.indexOfFeed(App.filterFeedId)
                    return row < 0 ? qsTr("All sources")
                                   : App.subscriptions.data(App.subscriptions.index(row, 0),
                                                            Qt.UserRole + 3)
                }

                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("All sources")
                        onClicked: App.filterFeedId = -1
                    }
                    Repeater {
                        model: App.subscriptions
                        MenuItem {
                            text: model.title
                            onClicked: App.filterFeedId = model.feedId
                        }
                    }
                }
            }

            ComboBox {
                width: parent.width
                label: qsTr("Age")
                value: page.ageLabel(App.filterMaxAgeHours)

                menu: ContextMenu {
                    Repeater {
                        model: page.ageOptions
                        MenuItem {
                            text: modelData.label
                            onClicked: App.filterMaxAgeHours = modelData.hours
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                enabled: App.filterActive
                text: qsTr("Show everything again")
                onClicked: App.clearFilter()
            }
        }

        VerticalScrollDecorator { }
    }
}
