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

            Item { width: 1; height: Theme.paddingMedium }

            Repeater {
                model: Languages.availableLanguages

                delegate: TextSwitch {
                    text: Languages.displayName(modelData)
                    automaticCheck: false
                    checked: Languages.currentLanguage === modelData
                    onClicked: {
                        Settings.uiLanguage = modelData
                        Languages.setLanguage(modelData)
                    }
                }
            }

            SectionHeader { text: qsTr("Feed catalogue") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //: says where the default catalogue is hosted and that it can be changed
                text: qsTr("The default catalogue is served from the Cloudflare CDN. You "
                           + "can point this at a source of your own instead.")
            }

            TextSwitch {
                checked: Settings.catalogEnabled
                //: label of the switch that permits contacting the catalogue host
                text: qsTr("Download the catalogue from %1").arg(Settings.catalogHost)
                description: Settings.catalogConsent === 0
                    ? qsTr("Not decided yet. Nothing has been downloaded.")
                    : (Settings.catalogEnabled ? qsTr("Allowed.")
                                               : qsTr("Declined. Add feeds by URL instead."))
                onClicked: App.setCatalogConsent(!Settings.catalogEnabled)
            }

            TextField {
                id: catalogField
                width: parent.width
                enabled: Settings.catalogEnabled
                //: label of the field holding the catalogue address
                label: qsTr("Catalogue address")
                text: Settings.catalogUrl
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                //: hint under the catalogue address field
                placeholderText: qsTr("Leave empty for the default")
                //: explains that a self-hosted catalogue is expected to work
                description: qsTr("Any address serving a catalogue in the same JSON format "
                                  + "works — your own list, or someone else's.")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    Settings.catalogUrl = text
                    focus = false
                    App.refreshCatalog(true)
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium

                Button {
                    enabled: Settings.catalogEnabled && !App.busy
                    text: qsTr("Reload now")
                    onClicked: {
                        if (catalogField.text !== Settings.catalogUrl)
                            Settings.catalogUrl = catalogField.text
                        App.refreshCatalog(true)
                    }
                }

                Button {
                    //: puts the catalogue address back to the one that ships with the app
                    text: qsTr("Use default")
                    enabled: Settings.catalogUrl !== Settings.defaultCatalogUrl
                    onClicked: {
                        Settings.catalogUrl = ""
                        catalogField.text = Settings.catalogUrl
                        App.refreshCatalog(true)
                    }
                }
            }

            // A button that does something has to say what it did: without this, a manual
            // reload looked the same whether it succeeded, was already current, or failed.
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: App.catalogError !== "" ? Theme.errorColor : Theme.highlightColor
                visible: !App.busy && App.catalogStatus !== ""
                text: App.catalogStatus
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                visible: App.busy
                text: qsTr("Loading…")
            }

            SectionHeader { text: qsTr("Storage") }

            Slider {
                width: parent.width
                minimumValue: 1
                maximumValue: 90
                stepSize: 1
                value: Settings.retentionDays
                valueText: qsTr("%n day(s)", "", value)
                label: qsTr("Keep articles for")
                onReleased: Settings.retentionDays = value
            }

            Slider {
                width: parent.width
                minimumValue: 10
                maximumValue: 500
                stepSize: 10
                value: Settings.thumbnailCacheMegabytes
                //: cap on the space used by cached article images
                valueText: qsTr("%1 MB").arg(value)
                label: qsTr("Space for images")
                onReleased: Settings.thumbnailCacheMegabytes = value
            }

            SectionHeader { text: qsTr("Grouping") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                //: explains that the grouping sliders only fine-tune a measured default
                text: qsTr("These were measured against real feeds. The range is limited "
                           + "to values that still work: a higher similarity would start "
                           + "merging unrelated stories.")
            }

            Slider {
                width: parent.width
                // Deliberately narrow. The measured cliff is just above the maximum, and
                // a reader who slid past it would experience unrelated stories being
                // merged as the app being broken, not as a setting they chose.
                minimumValue: App.clusterThresholdMinimum
                maximumValue: App.clusterThresholdMaximum
                stepSize: 1
                value: Settings.clusterThreshold
                valueText: value === App.clusterThresholdMaximum
                    //: marks the loosest grouping the app allows
                    ? qsTr("%1 · loosest").arg(value)
                    : (value === App.clusterThresholdMinimum
                       //: marks the strictest grouping the app allows
                       ? qsTr("%1 · strictest").arg(value)
                       : String(value))
                label: qsTr("Group articles that are this similar")
                onReleased: Settings.clusterThreshold = value
            }

            Slider {
                width: parent.width
                // No value of the window breaks anything -- a short one simply groups
                // less -- so this one stays wide.
                minimumValue: App.clusterWindowMinimum
                maximumValue: App.clusterWindowMaximum
                stepSize: 6
                value: Settings.clusterWindowHours
                valueText: qsTr("%n hour(s)", "", value)
                label: qsTr("Group articles published within")
                onReleased: Settings.clusterWindowHours = value
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Reset to defaults")
                onClicked: Settings.resetToDefaults()
            }
        }

        VerticalScrollDecorator { }
    }
}
