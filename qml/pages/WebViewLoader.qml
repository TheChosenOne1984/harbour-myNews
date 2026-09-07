import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.WebView 1.0

/*
 * The WebView, in a file of its own so the import is resolved only when this component is
 * actually loaded.
 *
 * Sailfish.WebView is Harbour-legal and present on a device, but it is absent from the
 * emulator image and the plan notes it does not work there anyway. Importing it from
 * ArticlePage would make that page fail to load; behind a Loader, a missing module is an
 * error status the page can answer with the external-browser fallback section 9 asks for.
 */
WebView {
    id: webView
    property url pageUrl

    onPageUrlChanged: if (pageUrl != "") url = pageUrl
    Component.onCompleted: if (pageUrl != "") url = pageUrl
}
