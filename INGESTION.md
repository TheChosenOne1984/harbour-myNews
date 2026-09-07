# The ingestion core

Milestone 3 of [project_description.md](project_description.md): fetcher, three-format
parser, SQLite store, conditional GET — headless, testable, no UI.

Four pieces in [src/feed/](src/feed/), each usable and testable on its own:

| Piece | Responsibility |
| --- | --- |
| `FeedFetcher` | HTTP, and all of section 5's mandatory manners |
| `feed::parseFeed()` | RSS 2.0, Atom 1.0 and RSS 1.0/RDF into plain structs |
| `FeedStore` | SQLite: schema, dedup, clustering, retention, search |
| `FeedRefresher` | the only place that knows the order the other three go in |

`tools/ingest-cli` drives all four against real feeds without the UI.

## What the design commits to

**Nothing blocks.** `FeedFetcher` is signal-based; `FeedStore` owns one `QSqlDatabase`
connection and is explicitly *not* thread-safe, because SQLite connections cannot be
shared across threads — a worker thread constructs its own store against the same file.

**Backoff lives in the store, not the fetcher.** It has to survive a restart to mean
anything, so `feed.failure_count` and `feed.next_attempt` are columns. These are an
addition to the plan's schema in section 4; everything else follows it.

**Detection is by root element and namespace**, never by URL or `Content-Type`. In a
966-article sample, 166 articles arrived as RDF — from a publisher whose feed URL ends in
`index-rss.xml`.

**A malformed document still yields what was read.** A response cut off mid-transfer
returns the articles parsed so far *and* an error, rather than discarding the lot.

## Two things real feeds taught us

### Setting Accept-Encoding by hand breaks everything

`QNetworkAccessManager` sends `Accept-Encoding: gzip, deflate` itself and decompresses
transparently — but only while it owns that header. Setting it manually, which looks like
exactly the courtesy section 5 asks for, makes Qt hand back the raw gzip bytes instead.

The first end-to-end run failed on **14 of 15 feeds** with *"Encountered incorrectly
encoded content"*. Unit tests could not have caught it: the stub server was not
compressing. `tst_feedfetcher.cpp` now serves a gzipped body and asserts the fetcher
returns plaintext.

### Conditional GET is honoured by a minority

On a second run seconds after the first, **3 of 15 publishers answered 304**. The other
twelve re-sent the full document, and dedup by `(feed_id, guid)` absorbed it. Conditional
GET is still mandatory — it costs nothing and it is what keeps the app off block lists —
but the bandwidth saving is smaller than one might assume, and the dedup path is doing
most of the real work.

Also visible in the same run: feeds contain duplicate GUIDs *within a single document*
(MOPO served 71 items under 56 distinct GUIDs), and publishers silently rewrite headlines
after publication (16 titles changed between two runs minutes apart). Both are handled —
the second is precisely why a changed title triggers a re-hash and re-clustering.

## End-to-end result

15 real German feeds, first run:

```
968 articles inserted, 893 clusters created, 75 joins, 15/15 feeds ok
```

## Running it

```sh
# unit tests -- 78 of them, no network beyond a local stub server
cd tests
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk tools exec SailfishOS-5.1.0.11-aarch64 $PWD/tst_mynews

# the real thing, against real publishers
cd tools/ingest-cli
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk tools exec SailfishOS-5.1.0.11-aarch64 $PWD/ingest-cli \
    --db $HOME/mynews.db --feeds $PWD/../corpus-feeds.json
```

Further options: `--list` (print clusters), `--search TEXT` (FTS5), `--purge-days N`.

The database it writes contains publisher content and is gitignored, like the clustering
corpus (section 11).

## Not done yet

- **Language detection** (section 7). `feed.lang` is taken from the document or set by
  hand; the Cavnar-Trenkle fallback for feeds that declare nothing is not implemented.
- **Threading.** Everything runs on one thread today. The store is built for a worker
  thread to own it, but nothing moves it there yet — that lands with the UI, where it
  matters.
- **Retention on start.** `purgeArticlesOlderThan()` exists and is tested; nothing calls
  it automatically yet.
- **`src/feed/` is not in the app's `.pro`.** The app does not use it until milestone 4;
  it is compiled by the tests and by `ingest-cli`.
