<p align="center"><img src="artwork/myNews_Logo.png" alt="myNews" width="200"></p>

# myNews

A news reader for Sailfish OS that groups the same story across sources. When NDR, MOPO
and the Abendblatt all report the same fire, the list shows one entry with the three
sources beneath it instead of three entries.

Feeds are fetched, parsed and grouped entirely on the device. There is no backend, no
account and no telemetry, and the app makes no requests on anyone's behalf but the
reader's.

## Features

- RSS 2.0, Atom and RSS 1.0/RDF feeds
- Stories grouped across sources (character n-gram SimHash, per language; see
  [CLUSTERING.md](CLUSTERING.md))
- Pictures: the one the feed gives, or the one the article page declares as its
  `og:image`. Fetched only for rows that come into view, downscaled and cached
- A curated starter catalogue of German national and Hamburg sources, plus any feed
  by URL, plus OPML import and export
- Full-text search over downloaded articles
- Filters by region, source, age and unread
- English and German interface

## For publishers

If you found this page through the User-Agent `myNews/<version>
(+https://github.com/TheChosenOne1984/harbour-myNews)` in your logs, this is what the app does:

- It fetches your feed when the reader opens the app, pulls down to refresh or uses the
  cover action. There is no background polling.
- Every request is a conditional GET (`If-None-Match` / `If-Modified-Since`). Requests to
  one host go one at a time, with a pause between them.
- Failing feeds back off exponentially, up to once a day. That includes 429 and 5xx.
- A response is capped at 5 MB. An article page is only fetched to read its `og:image`,
  only for an article whose feed gave no picture, at most once, and only its first
  200 kB are read.
- Nothing is re-hosted, proxied or shared. There is no reader mode, and paywalls are
  left alone.

To have your feed taken out of the starter catalogue, open an issue. The catalogue is
hosted separately from the app, so a removal takes effect without an app release.

## Permissions

| Permission | Why |
| --- | --- |
| Internet | Feeds, article pages and their pictures; the catalogue, only after the reader agrees |
| WebView | The built-in article view |
| Documents | OPML import and export in `~/Documents` |
| Audio | Sound for video inside an article; Sailfish mutes every app without it |

## Privacy

The app keeps everything it downloads on the device. The only request that does not go
to a publisher is the one fetching the starter catalogue. The app asks before making it
and names the address. Declining leaves adding feeds by URL fully working.

## Building

Sailfish SDK 3.13.5, target SailfishOS-5.1.0.11. Recipes for building, testing,
the emulator and deploying to a phone are in [BUILDING.md](BUILDING.md).

```sh
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk -c target=SailfishOS-5.1.0.11-aarch64 check
```

## Documents

| | |
| --- | --- |
| [project_description.md](project_description.md) | The plan and the decisions behind it |
| [task0-findings.md](task0-findings.md) | What the platform turned out to be: Qt 5.6, FTS5, WebView, Harbour |
| [CLUSTERING.md](CLUSTERING.md) | How the grouping parameters were measured |
| [INGESTION.md](INGESTION.md) | Fetcher, parser and store |
| [THUMBNAILS.md](THUMBNAILS.md) | How pictures are found and cached |
| [AUDIT.md](AUDIT.md), [PLAN-AUDIT.md](PLAN-AUDIT.md) | A code audit and what was done about it |

## Licence

BSD 3-Clause, see [LICENSE](LICENSE).
