# Thumbnails

Milestone 5 of [project_description.md](project_description.md), section 8 — the part that
decides whether this looks like a news reader or like a list of links.

Three pieces in [src/image/](src/image/):

| Piece | Responsibility |
| --- | --- |
| `extractPreviewImage()` | the picture a publisher declared for an article |
| `ThumbnailCache` | downscale on write, bounded on disk, least-recently-used eviction |
| `ThumbnailService` | decides what to fetch, and refuses to fetch what nobody asked for |

## Lazy is the whole design

A request happens because a **row became visible**, never because an article was ingested.
The QML `Thumbnail` asks for its picture when the delegate is created; nothing else does.

That is not an optimisation, it is the difference between a well-behaved client and a
burst of requests at twenty publishers for pictures nobody has scrolled to. On a real
refresh the app ingests around 1200 articles; it fetches perhaps a dozen images, because a
screen holds a dozen rows. A test asserts that ingesting fetches nothing at all.

Each article is attempted **at most once**. Success and failure both go to
`article.image_state`, so a picture that is not there is looked for once rather than on
every scroll past the row, and a restart does not start over.

## Where the pictures come from

Most of the time, from the feed. Measured on the 21 catalogue feeds: **1187 of 1213
articles already carried an image URL** in `media:content`, `media:thumbnail` or an
`<enclosure>`. German publishers are good about this, and the fallback matters less than
one might assume.

For the remaining articles the service fetches the article page and reads the picture the
publisher declared for it — `og:image`, then `twitter:image`, then `<link rel="image_src">`
— and remembers the result so a later run fetches the image directly instead of the page
again.

It deliberately does **not** walk the document looking for the largest `<img>`. That is
reader-mode extraction by another name and section 11 rules it out. A test asserts that an
`<img>` in the body is ignored.

Only the first 200 kB of a page is read: these tags live in `<head>`, and reading further
costs the publisher bandwidth for something that will not be there. `file:` and `data:`
URLs are refused, so a page cannot talk the app into reading a local file.

## Downscale on write

Section 8 is specific, and the reason is scrolling: decoding a 1200-pixel original for a
180-pixel box on every scroll is the fastest way to make a list stutter.

Images are scaled **during decode** where the format allows it, so the full-size image is
never materialised at all, then re-encoded as JPEG at quality 80 with a longest edge of 360
pixels — roughly twice the delegate's logical width, which covers the display's pixel
ratio. The original is not kept.

A source larger than 8000 pixels on a side is refused outright rather than decoded, as a
guard against a decompression bomb.

## The cache

Lives under `QStandardPaths::CacheLocation`, so the system may delete it and the app has to
cope — which it does: a missing file is simply fetched again.

Bounded by total size, **100 MB by default and adjustable between 10 and 500 MB**, with
least-recently-used eviction. "Recently used" means recently *displayed*: reading a
thumbnail touches it, so the articles a reader keeps returning to are evicted last.

Retention takes thumbnails with it. When articles are purged on start, the cache drops
every file whose article is gone, or it would slowly fill with pictures for stories that no
longer exist.

## Decoded thumbnails are not kept

`Image.cache` is off. A decoded 360-pixel thumbnail is around 290 kB of RGBA, and Qt's
pixmap cache holds them past the life of the delegate — so scrolling a few hundred rows
quietly accumulates tens of megabytes for rows nobody is looking at.

Measured in the emulator, scrolling through 300 rows:

| | peak resident memory |
| --- | --- |
| with Qt's pixmap cache | 261 MB |
| without | 173 MB |

A third of the footprint, and the file on disk is a twenty-kilobyte JPEG that decodes
again in a couple of milliseconds when the row comes back. On a phone sharing memory with
a browser engine in the same process, that headroom is the difference between running and
being killed.

## Qt 5.6 note

`touch()` uses `utime()` rather than `QFileDevice::setFileTime`, which arrived in Qt 5.10.
Opening and closing a file does not update its modification time, and the eviction order is
built on exactly that timestamp — so the first implementation silently did nothing, and the
test that caught it asserts a just-used thumbnail survives eviction.

## Not done

- **No re-hosting, ever.** An image is fetched from the publisher's own server and kept
  locally for offline reading, which is what any reader does (section 11). Nothing is
  proxied, mirrored or bundled.
- The service fetches at most three articles' images at a time; it does not yet prioritise
  what is on screen over what has scrolled past.
