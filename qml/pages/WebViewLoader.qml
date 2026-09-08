import QtQuick 2.0
import Sailfish.Silica 1.0
import Sailfish.WebView 1.0
import Sailfish.WebEngine 1.0

/*
 * The web view, in a file of its own so the import is resolved only when this component is
 * actually loaded.
 *
 * Sailfish.WebView is Harbour-legal but absent from some images. Importing it from
 * ArticlePage would make that page fail to load; behind a Loader, a missing module is an
 * error status the page can answer with the external-browser fallback section 9 asks for.
 *
 * WebViewFlickable rather than a bare WebView: the web view scrolls itself, so a
 * PullDownMenu needs a flickable of its own to hang from, and it has to be the one wrapping
 * the view. Put the menu on the Page instead and every downward drag over the article opens
 * the menu -- the article then cannot be scrolled at all.
 */
WebViewFlickable {
    id: view

    property url pageUrl
    //! The page's pulley menu, mounted inside this flickable where it belongs.
    property alias menu: menuLoader.sourceComponent

    onPageUrlChanged: if (pageUrl != "") webView.url = pageUrl
    Component.onCompleted: if (pageUrl != "") webView.url = pageUrl

    Loader { id: menuLoader }

    /*!
     * Hands memory back to the system when the reader leaves the article.
     *
     * Destroying the view releases the page, but the engine keeps its caches: Gecko is
     * built to hold on to them until something tells it not to. "memory-pressure" with
     * "heap-minimize" is that something -- the same notification the platform browser
     * sends -- and it matters here because the engine lives in this app's process, so
     * whatever it holds is charged to us. The article view is what pushed the app over the
     * edge in a 1 GB emulator.
     */
    function releaseMemory() {
        // Stop first: a page still loading would keep allocating right through the
        // collection and undo it.
        webView.stop()
        webView.url = "about:blank"
        WebEngine.notifyObservers("memory-pressure", "heap-minimize")
    }
}
