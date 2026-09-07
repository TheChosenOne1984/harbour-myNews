# SailfishOS News Reader — Implementation Handoff

**Status:** planning complete, implementation not started.
**Audience:** coding agent picking up implementation.
**Author of plan:** Kai — decisions below are his; open items are marked as such.

---

## 1. Product goal

A native SailfishOS news reader that aggregates RSS/Atom feeds **entirely on-device** and
presents them as a visually appealing, image-forward feed — explicitly *not* a plain feed reader.

The differentiating feature is **cross-source clustering**: when NDR, MOPO and Abendblatt all
report the same incident, the app shows one entry with the sources listed beneath it.

No backend. No server component. No account. The app talks only to publishers' own feed
endpoints and CDNs.

### Non-goals

- No translation of any kind (explicitly dropped).
- No cross-language clustering (accepted limitation, see §6).
- No paywall circumvention.
- No self-hosted mirroring or re-serving of article content or images.
- No own aggregation backend or API (an earlier plan built on a third-party aggregator API was
  rejected for legal reasons; do not reintroduce it).

---

## 2. Target platform

| Item | Value |
| --- | --- |
| Device | Jolla Phone (2026) |
| OS | Sailfish OS 5.2 (Finlayson series) |
| Arch | aarch64 |
| Display | 6.36" FHD+ AMOLED, 20:9, ~390 ppi |
| UI toolkit | Sailfish Silica (QML) |
| Distribution | Harbour-first (see §11, open) |

### Task 0 — verify before writing code

These could not be verified from documentation and change fundamental choices. Check against the
installed SDK target, do not assume:

1. **Qt version in the SFOS 5.2 SDK target.** Sailfish was on Qt 5.6 as of 5.0; 5.2 status
   unknown. Determines C++ standard, available Qt classes (e.g. `QRandomGenerator` absent in 5.6),
   and QML syntax. Build a trivial app and print `QT_VERSION_STR`.
2. **SQLite FTS5 availability** in the bundled SQLite. If FTS5 is not compiled in, fall back to
   `LIKE`-based search over a normalised title column and drop the search feature from MVP.
3. **Sailfish.WebView on the target.** Confirmed to exist as the official API, but verify it
   builds and runs on this device.

Report findings before proceeding to §3.

---

## 3. Architecture

C++ backend, QML/Silica frontend. Business logic in C++; QML strictly presentation.

```
harbour-<name>/
  src/
    main.cpp
    feed/       FeedFetcher, FeedParser, FeedStore
    cluster/    SimHash, Clusterer, LangDetect
    image/      ThumbnailCache
    model/      ArticleListModel, SubscriptionModel, ClusterModel
  qml/
    harbour-<name>.qml
    cover/CoverPage.qml
    pages/      FeedPage, ArticlePage, SubscriptionsPage, CatalogPage, SettingsPage
    components/ ArticleDelegate, SourcePill, Thumbnail
  rpm/          .spec
```

Rules:

- All feed fetching, parsing, clustering and image decoding happens off the GUI thread. Use
  `QThread` workers or `QtConcurrent`; never block the QML thread.
- Models are `QAbstractListModel` subclasses backed by SQLite queries, exposed to QML via
  `qmlRegisterType`. Do not use QML `XmlListModel` or `LocalStorage`.
- No network access from QML. All HTTP via `QNetworkAccessManager` in C++.

---

## 4. Storage

SQLite via `QSqlDatabase`. Schema outline:

```sql
CREATE TABLE feed (
  id INTEGER PRIMARY KEY,
  url TEXT NOT NULL UNIQUE,
  title TEXT,
  site_url TEXT,
  lang TEXT,              -- BCP-47, may be user-overridden
  lang_source TEXT,       -- 'declared' | 'detected' | 'manual'
  etag TEXT,
  last_modified TEXT,
  last_fetch INTEGER,
  last_status INTEGER,
  enabled INTEGER DEFAULT 1
);

CREATE TABLE article (
  id INTEGER PRIMARY KEY,
  feed_id INTEGER NOT NULL REFERENCES feed(id) ON DELETE CASCADE,
  guid TEXT NOT NULL,          -- feed <guid>/<id>, fallback: link
  link TEXT NOT NULL,
  title TEXT NOT NULL,
  teaser TEXT,                 -- plain text, HTML stripped
  published INTEGER,           -- unix ts, fallback: first-seen
  first_seen INTEGER NOT NULL,
  image_url TEXT,
  image_state TEXT,            -- 'none'|'pending'|'cached'|'failed'
  lang TEXT,
  simhash INTEGER,             -- 64-bit, stored as INTEGER
  cluster_id INTEGER,
  read INTEGER DEFAULT 0,
  UNIQUE(feed_id, guid)
);

CREATE INDEX idx_article_published ON article(published DESC);
CREATE INDEX idx_article_cluster ON article(cluster_id);
CREATE INDEX idx_article_lang_pub ON article(lang, published DESC);

CREATE TABLE cluster (
  id INTEGER PRIMARY KEY,
  lead_article_id INTEGER,     -- article whose title/image represents the cluster
  lang TEXT,
  published INTEGER,           -- max(published) of members, for ordering
  member_count INTEGER
);
```

Search (if FTS5 available): external-content FTS5 table over `article(title, teaser)` with
`tokenize='trigram'` — language-independent, unlike `unicode61` with stemming.

Retention: purge articles older than N days (default 14, configurable) plus their orphaned
thumbnails, on app start in a background transaction.

---

## 5. Feed ingestion

Parse with `QXmlStreamReader`. **Three formats must be supported** — the target sources do not
agree on one:

- RSS 2.0
- Atom 1.0
- RSS 1.0 / RDF

Detect by root element and namespace, not by feed URL or content-type.

### Field extraction, in priority order

| Field | Sources |
| --- | --- |
| id | `<guid>` (RSS), `<id>` (Atom), else `<link>` |
| title | `<title>` — unescape entities, strip tags |
| teaser | `<description>`, `<summary>`, `<content:encoded>` — strip HTML to plain text, truncate to ~300 chars for storage |
| published | `<pubDate>`, `<updated>`, `<published>`, `<dc:date>` — parse RFC 822 *and* ISO 8601; on failure use first-seen |
| image | `<media:content url>`, `<media:thumbnail url>`, `<enclosure type="image/*">`, first `<img src>` in HTML content; else `og:image` (see §7) |
| lang | `<language>` (RSS), `xml:lang` (Atom) — often absent or wrong |

### HTTP behaviour — mandatory

- Conditional GET on every refresh: send `If-None-Match` / `If-Modified-Since` from stored
  `etag` / `last_modified`. Handle `304` as success-with-no-changes.
- Honest, identifiable `User-Agent` including app name, version and project URL.
- Accept gzip.
- Per-host serialisation with a small delay between requests; do not fan out 30 parallel requests.
- Exponential backoff per feed on `429`, `5xx` and network errors. Surface persistent failure in
  the subscriptions UI rather than retrying silently forever.
- Timeout per request (10s connect, 30s total), hard cap on response size (~5 MB).

Deduplicate within a feed by `(feed_id, guid)`; an article already stored is updated, not
re-inserted, and does not get re-clustered unless its title changed.

---

## 6. Clustering — the core feature

Group articles reporting the same story across different feeds.

### Algorithm

1. **Normalise** `title + " " + teaser`: Unicode NFKC, casefold, strip punctuation, collapse
   whitespace. No stopword removal, no stemming — must stay language-agnostic.
2. **Feature extraction:** character 4-grams over the normalised string. Deliberately *not*
   word tokenisation — that would require per-language stopword lists and breaks entirely on
   languages without word boundaries (CJK).
3. **SimHash**, 64-bit, over the 4-gram set, weighted by 4-gram frequency.
4. **Candidate selection** for a new article: existing articles where
   - `lang` is equal, AND
   - `published` within the **time window** (default 48h, configurable), AND
   - Hamming distance of simhash <= **threshold** (default 12 of 64, configurable)
5. **Assignment:** join the cluster of the closest match below threshold; otherwise create a new
   single-member cluster.
6. **Lead article** for a cluster: prefer the member with a cached image; tie-break by source
   priority (user's subscription order), then by earliest `published`.

Both **time window and Hamming threshold must be exposed as tunable settings** (developer/advanced
settings are fine) — the defaults are starting points, not validated values.

### Why the time window matters

Without it, anniversary pieces and follow-ups cluster onto the original story. 48h is the
starting default.

### Language partitioning

Cross-language clustering is explicitly out of scope and accepted as a limitation. Because
candidate selection filters on `lang`, this requires no special handling — it is a predicate,
not a feature. Do not attempt translation or multilingual embeddings to work around it.

### Recommended build order

Build the clusterer as a **standalone desktop harness first** (plain C++ binary, reads a JSON
dump of articles, prints clusters). Tune threshold and window against real Hamburg feed data
before wiring it into the app. This is much faster to iterate than on-device.

---

## 7. Language detection

Per feed, resolved once and cached in `feed.lang`:

1. Declared value from the feed (`<language>` / `xml:lang`) if present and parseable.
2. Otherwise detect from concatenated titles of the first fetch using character trigram profiles
   (Cavnar-Trenkle). A few KB of embedded model data per supported language; no external
   dependency, no network call.
3. User override in the subscription's settings, stored with `lang_source='manual'`.

Article `lang` inherits from its feed. Per-article detection is not needed for MVP.

---

## 8. Thumbnails

This is where the app earns its "not a boring RSS reader" claim, and it is the most
implementation-heavy part.

- Feed-provided image URL is used when present (see §5).
- When absent: fetch the article page and extract `og:image`. **Lazy only** — triggered when the
  delegate becomes visible, never for the whole batch. Persist the result (including failure) in
  `article.image_state` so it is attempted at most once.
- **Downscale on write.** Decode once, scale to the target delegate size (roughly 2x the logical
  thumbnail width to cover DPR), re-encode as JPEG q80, store on disk under the app's cache dir.
  Do not keep full-resolution originals — decoding 1200px JPEGs for 180px thumbnails is the
  fastest way to make scrolling stutter.
- Disk cache with a size cap (default 100 MB) and LRU eviction; cache dir must be
  `QStandardPaths::CacheLocation`.
- In QML: `Image` with `asynchronous: true` and explicit `sourceSize`. Always.
- Placeholder for missing images: source-coloured tile with the source initial, not a broken-image
  icon and not a blank gap.

Legal: fetching an image from the publisher's own server and caching it locally for offline
reading is normal reader behaviour. Do not re-host, proxy or bundle publisher images or logos.

---

## 9. UI

Silica throughout. Follow Sailfish conventions (`PullDownMenu`, `RemorseItem`, cover page).

### Feed page (main view)

`SilicaListView` of clusters, newest first.

Delegate contents:

- Thumbnail, left, fixed size
- Headline, up to 2 lines, elided
- Teaser, 2 lines, elided, `Theme.secondaryColor`
- Source pills row: one small label per contributing source,
  `Theme.fontSizeExtraSmall`. With more than 3 sources, show the first two plus `+N`.
- Relative timestamp

**Delegate height must be fixed.** Variable heights combined with asynchronously loading images
produce visible jumping during scroll. Reserve the thumbnail box whether or not an image exists.

`PullDownMenu`: refresh, mark all read, subscriptions, settings.

### Article detail

`Sailfish.WebView` (Gecko-based) in a `WebViewPage`, opening `article.link`.

Build requirements — both files:

- `.pro`: `PKGCONFIG += qt5embedwidget` (plus `CONFIG += sailfishapp` or `link_pkgconfig`)
- `.spec`: `BuildRequires: pkgconfig(qt5embedwidget)`

Known quirks: Qt Creator reports "QML module not found" for the `Sailfish.WebView` import — this
is harmless. The WebView does **not** work in the Sailfish emulator; test on device.

Fallback: menu item to open in the external browser, for paywalled or failing pages.

For a cluster with multiple members, the detail view offers the other sources' links (simple list,
pushed page or menu) so the user can read the same story elsewhere.

### Cover page

Count of unread articles plus the current lead headline. Cover action: refresh.

---

## 10. Feed catalogue

Users get a curated starter list, extensible with arbitrary feed URLs and OPML import.

- **Scope for MVP:** Hamburg local plus German national. International only via manual add or
  OPML import — do not attempt to ship a global catalogue.
- **Host the catalogue remotely** as a JSON or OPML file (static hosting is enough). Do not compile
  it into the binary. Rationale: if a publisher asks to be removed, that must be a commit, not an
  app store release cycle.
- Ship a bundled copy as offline fallback for first run; refresh from remote on a slow cadence
  (weekly) with `If-Modified-Since`.
- **Compile the list yourself** from publishers' own feed pages. Do not bulk-import a third
  party's curated feed directory — such directories can be protected as database works
  (§ 87a UrhG).
- **No publisher logos in the app package.** Use the favicon fetched at runtime from the feed's
  own domain, or a text label with an accent colour. Publisher *names* as descriptive references
  are fine; bundled logos imply a partnership that does not exist.
- OPML import and export, both.

---

## 11. Constraints that are not negotiable

These follow from the legal analysis that shaped the whole design. Do not "optimise" them away.

1. **No re-distribution of content.** The app is a client. It fetches from publishers, stores
   locally, displays locally. It never uploads, syncs, mirrors or shares article content.
2. **No paywall circumvention.** No reader-mode extraction that renders subscriber-only content
   readable, no cookie tricks, no AMP/print-view substitution to bypass metering.
3. **Conditional GET and rate limiting** as specified in §5 — this is both a courtesy and what
   keeps the app off publishers' block lists.
4. **Honest User-Agent.** No masquerading as a desktop browser to obtain different content.
5. **No telemetry.** No analytics, no crash reporting phoning home, no unique identifiers. The
   platform's users chose it specifically for this.

---

## 12. Open decisions

| # | Decision | Status |
| --- | --- | --- |
| 1 | **Distribution target: Harbour vs OpenRepos.** Determines whether background refresh can ship as a systemd user service. Harbour forbids bundled daemons. | **Unresolved.** Assume Harbour-first for MVP: refresh on app start, on cover action, and on pull-down only. Structure the fetch layer so a timer-driven service can be added later without redesign. |
| 2 | Qt version, FTS5, WebView on target | See Task 0, §2 |
| 3 | Notifications for new articles | Depends on #1. Out of MVP scope. |
| 4 | Prior art review: **Tidings**, **Kaktus** (existing Sailfish feed readers) | Not done. Worth 30 minutes before starting — less to decide whether to build, more to see what their delegate layouts and feed handling got right. The clustering feature is the intended differentiator either way. |
| 5 | App name and Harbour package name (`harbour-*`) | Not chosen |

---

## 13. Suggested milestones

1. **Task 0** — platform verification (§2). Report back.
2. **Clustering harness** — standalone C++ binary, real feed data, tuned threshold and window (§6).
3. **Ingestion core** — fetcher, three-format parser, SQLite store, conditional GET. Headless,
   testable, no UI.
4. **Minimal UI** — feed page with fixed-height delegates, no images yet, source pills, WebView
   detail page.
5. **Thumbnails** — extraction, lazy `og:image`, downscaling disk cache (§8).
6. **Subscriptions and catalogue** — add/remove, OPML import/export, remote catalogue (§10).
7. **Polish** — cover page, search (if FTS5), retention purge, settings.

Milestones 2 and 3 are independent and can be built in either order.
