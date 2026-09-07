import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Switching language reloads the root QML component (Qt 5.6 has no
 * QQmlEngine::retranslate()), so this page is popped and the user lands back on the
 * feed. Deliberate -- see LanguageManager.
 */
Page {
    id: page

    allowedOrientations: defaultAllowedOrientations

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: page.width

            PageHeader { title: qsTr("Settings") }

            SectionHeader { text: qsTr("Language") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //: explains that changing the language returns the user to the article list
                text: qsTr("Changing the language returns you to the article list.")
            }

            Item { width: 1; height: Theme.paddingLarge }

            Repeater {
                model: Languages.availableLanguages

                delegate: TextSwitch {
                    text: Languages.displayName(modelData)
                    automaticCheck: false
                    checked: Languages.currentLanguage === modelData
                    onClicked: Languages.setLanguage(modelData)
                }
            }
        }

        VerticalScrollDecorator { }
    }
}
