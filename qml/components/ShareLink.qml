import QtQuick 2.0
import Sailfish.Share 1.0

/*
 * Hands an article's address to the system share sheet: mail, messages, other apps.
 *
 * Only the link and the headline are shared, never the article itself: the reader
 * passes on where to find it, and the publisher's page is what the recipient opens
 * (section 11).
 *
 * The resource has the shape Jolla's own Gallery uses for a URL it shares.
 */
ShareAction {
    mimeType: "text/x-url"
    //: title of the system share sheet
    title: qsTr("Share article")

    function share(headline, link) {
        resources = [{ "type": "text/x-url", "status": link, "linkTitle": headline }]
        trigger()
    }
}
