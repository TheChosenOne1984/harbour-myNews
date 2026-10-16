# Code audit — myNews

Full read of `src/`, `qml/`, `tools/`, packaging and build files at commit `8f1cfa6`.
Nothing in this report has been applied; every item is a proposal.

Where a finding could be settled by running something rather than by reading, it was.
Those are marked **verified** and say how. The rest are marked **by inspection**, and I
say plainly where a claim would still need a device or an experiment to confirm.

---

## 1. Security

### S1 — Article links reach the browser engine and the system with no scheme check
**Severity: high.** `qml/pages/ArticlePage.qml:54` (`item.pageUrl = page.articleLink`),
`:110`, `:134`, `qml/pages/FeedPage.qml:188`, `:215`,
`qml/pages/ClusterSourcesPage.qml:80`.

`link` is whatever the publisher put in `<link>`, an Atom `href`, or an RDF `rdf:about`
(`src/feed/feedparser.cpp:168`, `:190`, `:210`). It is stored verbatim and then handed
to two places that act on it: the in-process Gecko view, and `Qt.openUrlExternally`.

Neither validates the scheme. A `file://` link renders local content inside a scriptable
engine that lives in this process; `tel:`, `sms:` and any other registered handler can be
triggered from a feed through `openUrlExternally`.

What makes this worth fixing rather than arguing about is that the codebase already knows
the rule and writes it down — `src/image/ogimage.cpp:103`:

```cpp
// Only over TLS, and only http(s): a feed must not talk us into fetching a file:// URL.
```

That guard exists for preview images and nowhere else. The same sentence applies to the
link, which is the more dangerous of the two.

*Proposal:* one predicate (`isFetchableUrl`) applied at ingest, so nothing unusable is
stored, and again at each use site as a belt-and-braces check.

### S2 — Feed and catalogue URLs are only checked for "has a host"
**Severity: medium.** `src/feed/feedfetcher.cpp:50`, `src/catalog/catalogservice.cpp:87`.

Both accept any scheme as long as `QUrl::host()` is non-empty. That rules out `file://`
by accident rather than by intent, and still admits e.g. `ftp://`. The catalogue is
remote and the manual-add field is free text, so neither input is under our control.

### S3 — The decompression-bomb guard is skipped when the header carries no size
**Severity: medium.** `src/image/thumbnailcache.cpp:87`.

```cpp
const QSize sourceSize = reader.size();
if (sourceSize.isValid()) {
    if (sourceSize.width() > MaxSourceDimension || ...) return QString();
    ...
}
QImage decoded = reader.read();
```

`MaxSourceDimension` lives *inside* the `isValid()` branch. An image whose format or
truncated header does not report dimensions bypasses the check and is decoded at full
size. Qt 5.6 has no `QImageReader::setAllocationLimit` (that arrived in 5.14), so there
is no second line of defence. The bytes come from a URL a feed supplied.

*Proposal:* fail closed — refuse when the size is unknown.

### S4 — Consent outlives the host it was given for
**Severity: low.** `src/app/settings.cpp:setCatalogUrl`, `src/app/settings.h:30`.

The consent prompt names Cloudflare specifically. Changing `catalogUrl` afterwards does
not reset consent, so the grant silently transfers to whatever host is entered next. The
user typed that host themselves, so this is mild — but the prompt's wording no longer
matches what was agreed to.

Related: `Settings.catalogConsent` is exposed to QML as a **writable** `int` property and
nothing reads or writes it. A consent gate should not have an unused write path.

---

## 2. Robustness

### R1 — Unbounded recursion in the feed parser crashes the app
**Severity: high. Verified.** `src/feed/feedparser.cpp:61`.

`readNode()` calls itself for every nested start element with no depth limit. One item
containing deeply nested markup exhausts the stack.

Verified with a throwaway aarch64 binary linking `feedparser.cpp`, run under the SDK's
scratchbox2/qemu (built outside the repo and deleted afterwards):

| nesting depth | document size | result |
|---:|---:|---|
| 30 000 | ~185 KB | parses |
| 40 000 | ~245 KB | `signal 11 (core dumped)` |

The threshold on real hardware will differ with the thread stack, but the shape does not.
Note the size: **~245 KB**, against a `MaxResponseBytes` cap of 5 MB. The size cap does
not protect against this at all.

Reachable from any subscribed feed, and from a network attacker on any feed served over
plain `http`. It fires on every refresh, so the app would not start usefully again until
the feed is removed.

*Proposal:* a depth parameter on `readNode`; beyond a limit (a few hundred is generous
for real feeds), skip the item and record it as a parse error, which the existing backoff
already handles.

### R2 — The backend deletes its store while objects holding a pointer to it are alive
**Severity: medium. By inspection.** `src/app/backend.cpp:38`.

```cpp
Backend::~Backend()
{
    delete m_store;
    delete m_thumbnails;
}
```

`m_fetcher`, `m_refresher`, `m_catalog` and `m_thumbnailService` are QObject *children*,
so they are destroyed after this body has run. `~FeedFetcher` calls `abortAll()`, which
aborts replies and emits `finished` **synchronously** — into consumers that dereference
`m_store` (`FeedRefresher::onFetchFinished`, `ThumbnailService::onFetchFinished`).

It does not crash today, and the reason is worth stating because it is not design:
`abortAll()` removes the entry before aborting, so the emitted `Result` carries the
default `id = -1`, which every consumer rejects on its first line. Change that default,
or add a consumer that does not check, and this becomes a use-after-free.

*Proposal:* destroy the children first (or null the pointers), rather than relying on an
id sentinel to keep them away from freed memory.

### R3 — Per-host bookkeeping is never reclaimed
**Severity: low.** `src/feed/feedfetcher.cpp:239` and `m_queues`.

An empty `QQueue` stays in `m_queues` for every host ever seen, and `m_hostCooldowns`
keeps one `QTimer` per host for the life of the process. Bounded by the number of
distinct subscription hosts, so it is untidy rather than dangerous.

---

## 3. Correctness

### C1 — `&shy;` is never stripped
`src/feed/feedparser.cpp:241`.

```cpp
{QStringLiteral("shy"), QString()},
```

The stored value is a *null* `QString`, and the caller decides success with
`resolved = !replacement.isNull()`. So the one entry meant to expand to nothing is the
one entry that never resolves, and `&shy;` survives into the title as literal text. Every
other entry in the table works.

*Proposal:* `QStringLiteral("")` for the intentional-empty case, or a sentinel that
distinguishes "not found" from "expands to nothing".

### C2 — Thumbnail cache size drifts upward when a file is replaced
`src/image/thumbnailcache.cpp:111`.

```cpp
m_usedBytes += QFileInfo(path).size();
```

`pathFor(articleId)` is deterministic, so re-storing an article's thumbnail overwrites
the file — but the old size is never subtracted, and it was already counted. The counter
only resyncs at `open()`. The visible effect is eviction firing earlier than the
configured cap.

### C3 — `ConnectTimeoutMs` is documented but unused
`src/feed/feedfetcher.h:76`. Declared under a comment promising "timeouts … from
section 5", referenced nowhere. There is no connect-phase timeout; only the 30-second
total timer exists. Either wire it up or drop it, but the constant currently claims a
behaviour the app does not have.

---

## 4. Packaging and build

### P1 — OPML export writes into a directory Sailjail blacklists
**Severity: medium. Verified on the emulator image.**

`AppController::defaultOpmlExportPath()` (`src/app/appcontroller.cpp:530`) targets
`QStandardPaths::DocumentsLocation`. `harbour-mynews.desktop` declares
`Permissions=Internet;WebView`. On the device image:

```
/etc/sailjail/permissions/Base.permission:210:  blacklist ${HOME}/Documents
/etc/sailjail/permissions/Documents.permission: noblacklist ${HOME}/Documents
```

Without the `Documents` permission the path is blacklisted, so "Export subscriptions"
cannot succeed under an enforcing sandbox. The failure is reported through
`opmlExported`, so it surfaces as an error rather than silently — but the feature does
not work.

*Proposal:* add `Documents` to the permission set (it re-prompts, per the note in
`BUILDING.md`), or export somewhere inside the sandbox and say where.

### P2 — Unused Qt module and a duplicated BuildRequires
`harbour-mynews.pro:23` has `QT += sql network concurrent`; QtConcurrent is used nowhere
in `src/` or `tools/`. `rpm/harbour-mynews.spec:19-20` lists
`pkgconfig(Qt5Concurrent)` twice.

---

## 5. Dead code

Nothing here is a defect; it is surface that has to be read, maintained and trusted
without earning it.

**Referenced nowhere at all — not app, not tools, not tests**
- `FeedFetcher::setUserAgent()` and `userAgent()`
- `FeedRefresher::isRunning()`
- `FeedRefresher::refreshOne()`
- `ParsedFeed::formatName()`
- the `Settings::catalogConsent` property pair (`catalogConsentInt` / `setCatalogConsentInt`)

**Exercised only by tests, no production consumer**
- `FeedFetcher::allFinished` (emitted, never connected outside tests)
- `ThumbnailService::cancelPending()`, `ThumbnailService::thumbnailFailed`

**Reachable only from the development tools**
- `FeedStore::search()` — `ingest-cli` only. The FTS5 index, its triggers, the rebuild
  path and six tests all exist; **the app has no search UI**, so none of it runs on a
  device. This is the largest single block of untriggered production code.
- `cluster::clusterBatch()`, `cluster::normalizeArticle()` — the harness
- `FeedStore::seen()`

**Complete but unreachable from the UI**
- `AppController::importOpml()` is `Q_INVOKABLE`, reads the file, hands it to the backend,
  and the backend parses and subscribes — but no QML calls it. `SubscriptionsPage.qml:42`
  offers export only. OPML import cannot be performed by a user.

**Model roles no QML ever reads**
`category`, `imageState`, `langSource`, `lastFetch`, `lastStatus`, `siteUrl`.

---

## 6. Comments that no longer match the code

- `qml/pages/FeedPage.qml:37-52` — two consecutive comment blocks for the filter bar. The
  first describes "a docked bar"; the second describes the floating overlay that replaced
  it, and explains why docking was wrong. The first should have gone with the change.
- `qml/components/Thumbnail.qml:9` — "Until thumbnails land (milestone 5) it is always the
  placeholder." Milestone 5 shipped.

---

## 7. Checked and found sound

Recorded so a later audit need not redo it.

- **SQL.** Every statement carrying external data is prepared and bound. The three places
  that interpolate (`feedstore.cpp:262`, `:277`, the cluster-id `IN` list at `:894`)
  interpolate a hardcoded table name from a literal list, or integers just read out of
  the database — and the last one says so in a comment.
- **FTS5 query construction.** User text is quoted and internal quotes doubled
  (`feedstore.cpp:1105`), so FTS operators in a search term cannot become syntax.
- **Thread discipline.** Every call into `Backend` goes through
  `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`; there is no direct member access
  from the GUI thread anywhere in `appcontroller.cpp`. SQLite is only touched on the
  worker thread.
- **Consent enforcement.** Gated in C++ (`appcontroller.cpp:284`), not only in QML, so it
  cannot be bypassed by reaching the page a different way.
- **Path construction for cached files.** Thumbnail filenames derive from an integer id;
  no external string reaches a path.
- **XML entity handling.** `QXmlStreamReader` does not resolve external entities, so there
  is no XXE. (Internal entity-expansion limits in Qt 5.6 I did not verify — if you want
  that closed, it is a separate experiment like R1's.)
- **Gzip.** The `Accept-Encoding` trap is documented at the point of temptation and has a
  regression test.
- **Translations.** No user-visible string escapes `qsTr`; the only bare literals are two
  invisible measurement labels. No `type="unfinished"` entries remain in the German
  catalogue.
- **Leftovers.** No `TODO`, `FIXME`, `XXX`, `console.log` or debug scaffolding anywhere in
  `src/` or `qml/`.
- **Ignore rules.** `git status` is clean with a full in-source build present; every
  artefact is covered.
- **Tests.** 191 pass; one `QSKIP`, correctly conditional on FTS5 being unavailable.

---

## Suggested order

1. **R1** — the only finding that is remotely triggerable, verified, and fatal.
2. **S1** — small change, and the rule is already written down in the codebase.
3. **P1** — a shipped feature that cannot work on a device.
4. **S3**, **C1**, **C2** — small, contained.
5. **R2** — correctness of shutdown; no symptom today.
6. Dead code and stale comments, whenever the files are open anyway. **D-search** deserves
   a decision rather than a deletion: either the search UI lands, or a well-tested
   subsystem is carried for nothing.
