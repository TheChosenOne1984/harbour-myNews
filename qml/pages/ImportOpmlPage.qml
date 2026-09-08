import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Choosing an OPML file to import.
 *
 * A list rather than a file picker: Sailfish.Pickers exists only on a device image, not
 * in the build target, so a page importing it can be neither compiled against nor
 * validated here. The candidates come from AppController, which looks in the documents
 * folder -- the same folder the export writes to, so a round trip through this app needs
 * no explanation at all.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    property var candidates: []

    function reload() {
        candidates = App.opmlCandidates()
    }

    Component.onCompleted: reload()
    onStatusChanged: if (status === PageStatus.Activating) reload()

    SilicaListView {
        anchors.fill: parent
        model: page.candidates

        header: PageHeader {
            title: qsTr("Import subscriptions")
            description: qsTr("%n file(s) found", "", page.candidates.length)
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
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.primaryColor
                    text: modelData.substring(modelData.lastIndexOf('/') + 1)
                }
                Label {
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    //: reassures the reader that importing adds to what is already there
                    text: qsTr("Adds these feeds to your subscriptions")
                }
            }

            onClicked: {
                App.importOpml(modelData)
                pageStack.pop()
            }
        }

        ViewPlaceholder {
            enabled: page.candidates.length === 0
            text: qsTr("No OPML files found")
            //: %1 is a folder path
            hintText: qsTr("Copy a subscriptions file into %1 and come back.")
                          .arg(App.opmlFolder())
        }

        VerticalScrollDecorator { }
    }
}
