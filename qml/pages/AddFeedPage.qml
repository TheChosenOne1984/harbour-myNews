import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * Adding a feed by URL.
 *
 * Section 7: the feed is fetched once first, so the language picker opens pre-filled with
 * whatever the document declares. The user confirms with one tap instead of answering a
 * question they may not be able to answer -- but the choice is theirs, and what they
 * choose is "manual" and wins over every later refresh.
 */
Dialog {
    id: dialog

    property string feedUrl
    property string feedTitle
    property string feedLanguage: Settings.uiLanguage !== "" ? Settings.uiLanguage : "en"
    property bool probed: false
    property string probeError

    canAccept: urlField.text.trim().length > 0 && feedLanguage !== ""
    acceptDestinationAction: PageStackAction.Pop

    Connections {
        target: App
        onFeedProbed: {
            if (url !== urlField.text.trim())
                return
            dialog.probed = true
            dialog.probeError = error
            if (error === "") {
                if (title !== "")
                    dialog.feedTitle = title
                // Pre-selection only: a declared language is a starting point, not a
                // decision. The user still confirms it.
                if (lang !== "")
                    dialog.feedLanguage = lang.substring(0, 2)
            }
        }
    }

    onAccepted: App.addFeed(urlField.text.trim(), dialog.feedLanguage, dialog.feedTitle)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader { acceptText: qsTr("Add") }

            TextField {
                id: urlField
                width: parent.width
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                label: qsTr("Feed address")
                placeholderText: qsTr("https://example.com/feed.xml")
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: dialog.checkFeed()
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Check feed")
                enabled: urlField.text.trim().length > 0 && !App.busy
                onClicked: dialog.checkFeed()
            }

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                visible: App.busy
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Checking…")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                visible: dialog.probed && dialog.probeError !== "" && !App.busy
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeSmall
                //: shown when the address could not be read as a feed; adding anyway is allowed
                text: qsTr("Could not read this feed: %1").arg(dialog.probeError)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                visible: dialog.probed && dialog.probeError === "" && dialog.feedTitle !== ""
                color: Theme.highlightColor
                text: dialog.feedTitle
            }

            SectionHeader { text: qsTr("Language") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //: explains why the language matters, on the add-feed page
                text: qsTr("Articles are only grouped with others in the same language.")
            }

            Repeater {
                model: App.suggestedLanguages()

                delegate: TextSwitch {
                    text: App.languageDisplayName(modelData)
                    automaticCheck: false
                    checked: dialog.feedLanguage === modelData
                    onClicked: dialog.feedLanguage = modelData
                }
            }
        }

        VerticalScrollDecorator { }
    }

    function checkFeed() {
        dialog.probed = false
        dialog.probeError = ""
        App.probeFeed(urlField.text.trim())
    }
}
