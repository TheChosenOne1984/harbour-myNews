import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Corrects the language of an existing subscription. Anything chosen here is "manual"
 * and survives every later refresh (plan section 7).
 */
Page {
    id: page

    property var feedId
    property string currentLanguage
    property string feedTitle

    allowedOrientations: defaultAllowedOrientations

    SilicaListView {
        anchors.fill: parent
        model: App.suggestedLanguages()

        header: PageHeader {
            title: qsTr("Language")
            description: page.feedTitle
        }

        delegate: ListItem {
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                text: App.languageDisplayName(modelData)
                color: modelData === page.currentLanguage ? Theme.highlightColor
                                                          : Theme.primaryColor
            }
            onClicked: {
                App.setFeedLanguage(page.feedId, modelData)
                pageStack.pop()
            }
        }

        VerticalScrollDecorator { }
    }
}
