import QtQuick 2.0
import Sailfish.Silica 1.0

/*
 * The image box of an article row.
 *
 * Always occupies its space, image or not: section 9 requires a fixed delegate height,
 * and a box that appears only when an image exists would make rows change size as images
 * arrive. Until thumbnails land (milestone 5) it is always the placeholder.
 *
 * The placeholder is a source-coloured tile with the source's initial (section 8) --
 * deliberately not a broken-image icon and not a blank gap.
 */
Item {
    id: thumbnail

    property string source        //!< name of the publisher, for colour and initial
    property url imageUrl
    property bool dimmed

    /*!
     * Article whose picture this box shows. Setting it asks for the thumbnail -- which is
     * what makes the request lazy: it happens because a row became visible, not because
     * an article was ingested (section 8).
     */
    property var articleId
    onArticleIdChanged: if (articleId !== undefined) App.requestThumbnail(articleId)
    Component.onCompleted: if (articleId !== undefined) App.requestThumbnail(articleId)

    Rectangle {
        anchors.fill: parent
        radius: Theme.paddingSmall
        color: App.sourceColour(thumbnail.source)
        opacity: thumbnail.dimmed ? 0.4 : 0.85
        visible: !image.visible

        Label {
            anchors.centerIn: parent
            font.pixelSize: Theme.fontSizeLarge
            font.bold: true
            color: Theme.rgba("black", 0.55)
            text: thumbnail.source.length > 0 ? thumbnail.source.charAt(0).toUpperCase() : "?"
        }
    }

    Image {
        id: image
        anchors.fill: parent
        // Section 8: always asynchronous, always with an explicit sourceSize, so decoding
        // never happens on the GUI thread at the full size of the original.
        asynchronous: true
        sourceSize.width: width * 2
        sourceSize.height: height * 2

        // Not held in Qt's pixmap cache. A decoded thumbnail is around 290 kB of RGBA, so
        // a couple of hundred rows scrolled past would be tens of megabytes held for rows
        // nobody is looking at. The file on disk is a 360-pixel JPEG of some twenty
        // kilobytes; decoding it again when the row comes back is cheap, and this keeps
        // the app's footprint flat however far someone scrolls.
        cache: false
        fillMode: Image.PreserveAspectCrop
        clip: true
        opacity: thumbnail.dimmed ? 0.4 : 1.0
        source: thumbnail.imageUrl
        visible: status === Image.Ready
    }
}
