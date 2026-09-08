# Implementation plan for the audit findings

Derived from [AUDIT.md](AUDIT.md) at commit `5c980e5`. Ordered by urgency, then by what
depends on what.

> **Status: all fifteen packages done**, `eb4e254`..`9409140`. The four questions in
> stage 4 were answered: OPML finished and permitted, search shipped, ConnectTimeoutMs
> deleted, consent prompt reworded rather than re-asked. Outcomes and the two deviations
> from this plan are recorded at the end.

Stages 1–3 need no decisions from you and can run back to back. Stage 4 is blocked on
four questions, listed at the end — they are asked there and not earlier so the work
before them is not held up.

| # | Work | Finding | Depends on | Size |
|---|------|---------|-----------|------|
| **Stage 1 — stop the crash, close the input holes** ||||
| W1 | Depth limit in the feed parser | R1 | — | S |
| W2 | URL scheme allow-list | S1 | — | M |
| W3 | Image guard fails closed | S3 | — | S |
| **Stage 2 — correctness** ||||
| W4 | Scheme check in fetcher and catalogue | S2 | W2 | S |
| W5 | `&shy;` entity | C1 | — | XS |
| W6 | Thumbnail byte accounting | C2 | — | S |
| W7 | Backend destruction order | R2 | — | S |
| W8 | Reclaim per-host bookkeeping | R3 | — | XS |
| **Stage 3 — hygiene, no behaviour change** ||||
| W9 | Drop unused Qt module, dedupe spec | P2 | — | XS |
| W10 | Remove never-referenced symbols | D1 | — | S |
| W11 | Stale comments | §6 | — | XS |
| **Stage 4 — needs a decision first** ||||
| W12 | OPML: make it work or remove it | P1, D4 | Q1 | M |
| W13 | Search: ship the UI or remove the subsystem | D3 | Q2 | L or S |
| W14 | Connect timeout | C3 | Q3 | S or XS |
| W15 | Consent on catalogue host change | S4 | Q4 | S |

Size: XS ≈ minutes, S ≈ under an hour, M ≈ half a day, L ≈ Milestone 7 in its own right.

---

## Stage 1 — stop the crash, close the input holes

Everything here is remotely triggerable. It should go in before anything else, and each
item can be committed on its own.

### W1 · Depth limit in the feed parser
**Finding R1.** `src/feed/feedparser.cpp:61`.

Give `readNode()` a depth argument. Beyond a limit, stop descending and let the item fail
rather than the process.

- Limit: 200 is far past anything a real feed contains and far below the ~35 000 that
  overflows the stack. Worth measuring the deepest item in the existing corpus first
  (`tools/fetch-corpus.py` output) so the number is chosen rather than picked.
- Behaviour on breach: abandon the item and set `feed.error`. The existing backoff in
  `FeedStore` then treats the feed like any other broken document — no new machinery.
- Do **not** raise `MaxResponseBytes` or lean on it: the crash reproduces at ~245 KB
  against a 5 MB cap, so size limits are the wrong lever here.

*Tests:* a `tst_feedparser` case building a document nested past the limit, asserting the
parser returns an error instead of crashing, plus one just under the limit that still
parses. Both are cheap and run in-process — the existing suite would have caught this.

*Verification:* the throwaway probe from the audit can be rebuilt against the fixed
parser to confirm the depths that previously crashed now return cleanly.

### W2 · URL scheme allow-list
**Finding S1.** The largest of the three, and the one to think about before typing.

Add one predicate — `feed::isFetchableUrl(const QUrl &)`, http/https with a non-empty
host — and apply it in three places:

1. **At ingest** (`FeedStore::ingest`): if an article's `link` does not pass, store the
   article with an empty link rather than dropping the article. Dropping it would remove
   a cluster member and silently change clustering results; an article without a link is
   still worth showing and still contributes to the cluster.
2. **At the use sites**: `ArticlePage.qml:54`, `:110`, `:134`, `FeedPage.qml:188`, `:215`,
   `ClusterSourcesPage.qml:80`. A row with no usable link should not offer "Open in
   browser" and should not push the article page.
3. **At the fetch site**: `ThumbnailService::startPageFetch` uses the article link to
   fetch the publisher's page. `FeedFetcher` already requires a host, which blocks
   `file://` by accident — W4 makes that deliberate.

Belt and braces on purpose: ingest-time filtering fixes new rows, use-site checks cover
the rows already in every existing database. **No migration is needed** if the use sites
check, which is the reason to do both rather than pick one.

Expose the predicate to QML through `AppController` (e.g. `Q_INVOKABLE bool
canOpen(const QString &link)`) so the QML conditions do not re-implement it.

*Tests:* unit tests for the predicate (http, https, uppercase scheme, `file://`, `tel:`,
`javascript:`, scheme-relative, empty, whitespace); a `tst_feedstore` case that a
`file://` link ingests with an empty link and the article still joins its cluster.

*Note on verification:* the QML side cannot be exercised in the emulator without tap
injection. Keep the logic in C++ where the tests can reach it, and leave QML with a plain
`visible:`/`enabled:` binding.

### W3 · Image guard fails closed
**Finding S3.** `src/image/thumbnailcache.cpp:87`.

Move the `MaxSourceDimension` check out of the `if (sourceSize.isValid())` branch and
refuse when the size is unknown. One line moved, one added.

*Tests:* `tst_thumbnails` case with a truncated or headerless image asserting `store()`
returns empty and writes nothing.

---

## Stage 2 — correctness

None of these are reachable by an attacker; all are small and independent except W4.

### W4 · Scheme check in fetcher and catalogue
**Finding S2. Depends on W2** for the shared predicate.

`FeedFetcher::enqueue` (`:50`) and `CatalogService::refresh` (`:87`) currently test only
"valid, has a host". Replace both with the W2 predicate. The catalogue URL is user-entered
so the rejection must reach the UI — `CatalogService::refresh` already emits
`refreshed(false, …)` with a message, so extend that string rather than adding a path.

*Tests:* extend `tst_feedfetcher` and `tst_catalog` with rejected schemes.

### W5 · `&shy;` entity
**Finding C1.** `src/feed/feedparser.cpp:241`. Change the sentinel so "expands to nothing"
is distinguishable from "not found" — `QStringLiteral("")` is enough, since
`QString("")` is empty but not null.

*Tests:* one assertion in `tst_feedparser` that `&shy;` disappears and an unknown entity
survives verbatim. Add both — the second is what makes the first meaningful.

### W6 · Thumbnail byte accounting
**Finding C2.** `src/image/thumbnailcache.cpp:111`. Subtract the previous file's size when
overwriting, before adding the new one.

*Tests:* `tst_thumbnails` case storing twice for the same article id and asserting
`usedBytes()` matches the on-disk total rather than the sum of both writes.

### W7 · Backend destruction order
**Finding R2.** `src/app/backend.cpp:38`.

Destroy the children that hold the store before the store: either delete them explicitly
in the destructor body, ahead of `delete m_store`, or make the store a child too and let
the ordering be structural. The second is tidier but changes ownership; the first is two
lines and obvious.

*Tests:* hard to assert directly. A `tst_clusterview`-style case that constructs a
`Backend`, starts a refresh against a local server, and destroys it mid-flight would at
least run the path under whatever sanitiser the SDK offers. Worth attempting; not worth
blocking the fix on.

### W8 · Reclaim per-host bookkeeping
**Finding R3.** `src/feed/feedfetcher.cpp:239`. Delete the cooldown timer when it fires
and its queue is empty; erase empty queues from `m_queues` in `pumpQueues`.

---

## Stage 3 — hygiene

No behaviour change; safe to fold into one commit.

- **W9** — remove `concurrent` from `harbour-mynews.pro:23`; delete the duplicate
  `pkgconfig(Qt5Concurrent)` at `rpm/harbour-mynews.spec:20`. Rebuild both targets to
  confirm nothing was relying on it.
- **W10** — remove the symbols referenced nowhere at all: `FeedFetcher::setUserAgent`/
  `userAgent`, `FeedRefresher::isRunning`, `FeedRefresher::refreshOne`,
  `ParsedFeed::formatName`, the `Settings::catalogConsent` property pair. The last is the
  one that matters — an unused **writable** property on a consent gate.
  Leave the test-only and tool-only surface alone; it earns its place.
- **W11** — delete the superseded comment block at `qml/pages/FeedPage.qml:37-45`; fix
  `qml/components/Thumbnail.qml:9`.

Model roles nobody reads (`category`, `imageState`, `langSource`, `lastFetch`,
`lastStatus`, `siteUrl`) are deliberately **not** in this stage: they cost nothing, and
several are the natural data for the subscriptions detail the UI may still grow. Removing
them is churn without a benefit.

---

## Stage 4 — blocked on decisions

### Q1 → W12 · OPML
Export writes to `~/Documents`, which Sailjail blacklists, so it cannot work on a device.
Import is fully implemented but no QML calls it. Three coherent outcomes:

- **(a) Make both work.** Add `Documents` to `Permissions=` in the `.desktop`, and add an
  import entry to `SubscriptionsPage`. Note the permission set changing means Sailjail
  re-asks the user on next start (documented in `BUILDING.md`), and Harbour review pays
  attention to permissions — `Documents` for an export button is defensible but needs the
  description to say so.
- **(b) Keep export, move it inside the sandbox.** Write to the app's own data directory
  and tell the user the path. No new permission, no re-prompt; a less useful feature.
- **(c) Remove both** until there is a reason for them.

*Recommendation:* **(a)**, and wire import at the same time. Half the feature is already
written and tested; shipping an export that cannot write is worse than either finishing
or removing it.

### Q2 → W13 · Full-text search
FTS5 index, triggers, rebuild path, `FeedStore::search()` and six tests exist. The app has
no search UI, so none of it runs on a device. This is Milestone 7 work.

- **(a) Ship the search UI** — a search field on `FeedPage` or its own page, wired through
  `Backend`/`AppController` like every other query. Size L; it is a milestone, not a fix.
- **(b) Remove the subsystem** — drops the FTS5 table, triggers and tests, and shrinks the
  database.

*Recommendation:* **(a)**, but scheduled as its own piece of work rather than smuggled
into an audit follow-up. Until then it is knowingly-carried code, not an oversight — worth
a line in the README so the next reader knows it is deliberate.

### Q3 → W14 · Connect timeout
`ConnectTimeoutMs` is declared, documented as one of section 5's manners, and unused.

- **(a) Implement it** — a second timer armed until the first `downloadProgress` or
  `metaDataChanged`, then cancelled. Qt 5.6 has nothing built in; this is the portable
  shape.
- **(b) Delete the constant** and fix the comment, leaving the 30-second total timeout as
  the only guard.

*Recommendation:* **(b)**. The total timeout already bounds the slot; a separate connect
timeout buys little on mobile networks where a slow connect and a slow transfer are the
same problem. Cheaper to be honest about it than to build it.

### Q4 → W15 · Consent when the catalogue host changes
Consent is granted for a prompt that names Cloudflare; changing `catalogUrl` afterwards
carries the grant to the new host silently.

- **(a) Re-ask** when the host changes — store the host alongside the consent and reset to
  `Unasked` on mismatch.
- **(b) Leave it**, and reword the prompt so it grants "the configured catalogue address"
  rather than naming a provider.

*Recommendation:* **(b)**. The user typed the new host themselves, which is a clearer
signal of intent than a second dialogue; the honest fix is the wording, not another
prompt. (a) is defensible if you would rather be strict.

---

## Sequencing and verification

Each work package ends the same way, and none of it is optional:

1. New or extended tests **first** where the finding is testable — W1, W2, W3, W5, W6 all
   are, and each is a case the current 191 would not have caught.
2. Full suite green on aarch64 under sb2.
3. `sfdk check` clean on aarch64 (0 errors).
4. Clean between targets before any emulator build — the in-source build otherwise ships
   aarch64 objects in an i486 package, as it did during this session.
5. One commit per work package, so a regression can be bisected to a single finding.

Stage 1 is worth deploying to the emulator as a whole for a smoke test; Stages 2 and 3
change nothing a screenshot would show.

## Interaction with the open device question

The article view's scroll defect (`BUILDING.md`, unresolved without hardware) touches W2:
if the built-in reader turns out to be unusable on a device and the app defaults to the
system browser, the `file://`-into-Gecko half of S1 loses its teeth — but the
`Qt.openUrlExternally` half does not, and that is the half that reaches the dialler and
the file manager. **W2 should not wait for the device.**

Nothing else in this plan depends on hardware.


---

## Outcome

Fifteen packages, thirteen commits, `eb4e254`..`9409140`. 191 tests at the start, 214 at
the end; Harbour clean throughout.

Two places where the work departed from this plan, both deliberate:

**W1 keeps the over-deep item instead of abandoning it.** The plan said to drop the item
and report. Skipping only the subtree removes the stack risk just as completely, and an
item's title, link and description sit two levels down where nothing was skipped — so
dropping the item would have lost data for no gain. The document is still reported as not
fully read.

**W12 lists files rather than using a picker.** `Sailfish.Pickers` turned out to be a
runtime-only module: present on a device image, absent from the build target, so a page
importing it can be neither compiled against nor validated here — exactly the trap
`Sailfish.WebView` set. Listing the candidates from `AppController` needs no module and
puts the logic where tests can reach it.

### Found while implementing, not from the audit

**Text inside nested markup in a `<description>` is dropped.** A `Node` keeps only the
character data of its own level, so `<description><b>x</b></description>` yields an empty
teaser. Escaped HTML and CDATA — what publishers actually send — arrive as characters on
the description itself and are unaffected, so this only bites on real XHTML content in an
Atom `<content>`. Noted in `tst_feedparser::keepsShallowNestingIntact` rather than fixed:
widening it would change teaser extraction for every feed, and teasers feed the
clusterer.

### Verified how

- **R1** — a throwaway aarch64 binary linking `feedparser.cpp`, run under sb2/qemu, before
  and after. 40 000 levels segfaulted; now they parse.
- **P1** — on the emulator with the sandbox enforcing: the import page lists a file placed
  in `~/Documents`, and export writes 3.7 kB of OPML there.
- **W13** — on the emulator against the seeded database; "hamburg" returns the expected
  articles with sources and ages.
- **S3** — by inspection. No fixture built from Qt's own plugins reaches the bypass,
  because the formats that hide their size also fail to decode. Said so in the commit.

### Still open, unchanged by this work

The article view's scroll defect (`BUILDING.md`) — untestable without hardware, and
untouched here.
