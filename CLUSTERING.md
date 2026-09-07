# Clustering: how the parameters were chosen

Milestone 2 of [project_description.md](project_description.md): tune the Hamming
threshold and time window against real feed data before wiring the clusterer into the
app. This records what was measured, what changed against the plan's starting values,
and what remains uncertain.

The algorithm lives in [src/cluster/](src/cluster/) and is compiled into *both* the app
and the tuning harness, so a threshold tuned here means the same thing in the app.

## The corpus

`tools/fetch-corpus.py` pulls a snapshot from 15 German feeds listed in
`tools/corpus-feeds.json` — Hamburg local (NDR Hamburg, Hamburger Abendblatt, MOPO) plus
national (tagesschau, SPIEGEL, ZEIT, WELT, n-tv, FAZ, SZ, stern, taz, Tagesspiegel). The
list was compiled by hand from each publisher's own feed page, as section 10 requires.

The snapshot used here: **966 articles**, 750 of them within 48 hours. Formats came out
at 800 RSS 2.0 and **166 RSS 1.0 / RDF** — NDR really does serve RDF, so the plan's
insistence on three parsers in section 5 is confirmed by the data rather than assumed.

The corpus is **not committed**: section 11 rules out redistributing article content, and
a JSON of titles and teasers in a public repository would be exactly that. `corpus*.json`
is in `.gitignore`; regenerate it with `tools/fetch-corpus.py`.

## How quality was measured

There is no hand-labelled ground truth, so the harness uses two proxies that can be
computed automatically:

- **Recall proxy** — two articles whose *normalised titles are identical* are the same
  story beyond argument (wire copy republished verbatim; the snapshot contains 46 such
  pairs). The fraction that ends up in one cluster is a floor on recall.
- **Precision proxy** — for every pair the clusterer grouped, the word-level Jaccard
  overlap of the two titles. Genuine same-story pairs share content words. A grouped pair
  sharing under 10% is counted as *weak* and is almost certainly a false positive.

Both proxies are title-based, which structurally favours hashing the title alone. That
bias is real and is the reason the title-versus-teaser decision below was settled by
reading the clusters rather than by the numbers alone.

The weak-pair rate also **under-detects one specific failure**: articles sharing a topic
kicker share those words, so kicker-driven false positives score above 10% and slip
through. Mean Jaccard is the more sensitive signal there.

## What the plan's starting values do

Section 6 proposes 4-grams, frequency weighting, title + teaser, threshold 12, window
48h. On this corpus that yields **19.4% weak pairs** — roughly one grouped pair in five
is wrong. Examples from that run:

```
Hamburger Buchhandlungspreise vergeben: Wolfram Weimer wird sich ärgern
Russischer Angriffskrieg: Ukraine-Krieg: Will Putin nach Duma-Wahl verhandeln?
```

```
Konferenz in Monte-Carlo: Hannover Rück spürt intensiven Wettbewerb
Boney M. vs. Boney M.: Liz Mitchell von Auftritt bei Rechtspopulisten entsetzt
```

These are not random hash collisions — at 966 articles the expected number of chance
collisions below Hamming 12 is about 0.07. They are systematic.

## Three changes, each measured

### 1. Five-grams instead of four

Common German character sequences dominate a frequency-weighted SimHash and pull
unrelated documents of the same language toward each other. Wider n-grams are rarer and
more discriminative. Across the configuration grid, 3-grams collapse (up to 86.9% weak),
4-grams are borderline (19.4% weak with the teaser), and 5-grams and wider stay at 0.0%.

Weighting turned out to matter far less than expected; `Binary` (ignore repetition) is
the default because it is the least sensitive to a repeated phrase, but `Raw` and `Log`
score within noise of it.

### 2. Strip the kicker

The dominant failure mode was not the hash. German headlines carry a topic label before a
separator, and when one story owns the news cycle dozens of *different* articles share
it:

```
Wahl in Sachsen-Anhalt: Sieg Unheil
Wahl in Sachsen-Anhalt: Von Siegmund angezogen, von Merz abgestoßen
Wahl in Sachsen-Anhalt: Siegmund will stabile Mehrheit suchen
```

On a short headline that shared prefix is most of the n-grams, so these clustered
together although they report different things.

`cluster::stripKicker()` removes it. It keys on punctuation, not vocabulary, so it stays
language-agnostic, and it is conservative: the prefix must be at most 45 characters and
at least 20 characters must survive.

**Handling only the colon is worse than handling nothing.** Publishers disagree on the
separator — `:`, `–`, `!`, `|` all appear for the same purpose — and stripping one
publisher's headline but not another's creates an asymmetry that breaks matches which
previously worked. All of them are handled.

Effect at threshold 14: mean Jaccard rises from 0.73 to 0.81 while the weak rate stays at
0.0%. Since Jaccard is computed over the *full* titles, a rise means the surviving pairs
share real content words rather than just the kicker. Reading the output confirms it —
the Sachsen-Anhalt junk clusters disappear, and genuine clusters grow: the Finnish border
fence goes from 2 sources to 4, the St. Petersburg consulate from 3 to 4.

### 3. Title only, not title + teaser

Including the teaser costs recall: it drops from 100% to 91.3% on pairs that are certain
duplicates. The cause is visible in the data — an article whose teaser is empty stops
matching an identically-titled copy that has one, and empty or boilerplate teasers are
common.

This is the decision the biased metrics cannot settle on their own, so it was checked by
reading clusters. Including the teaser did not recover any genuine cluster that the
title-only configuration missed, and it lost certain ones.

## Recommended values, now the defaults in the code

| Parameter | Plan | Measured | Where |
| --- | --- | --- | --- |
| n-gram width | 4 | **5** | `cluster::DefaultNGramSize` |
| weighting | frequency | **binary** | `cluster::DefaultWeighting` |
| hashed text | title + teaser | **title, kicker stripped** | `cluster::clusterSubject()` |
| Hamming threshold | 12 | **13** | `cluster::Params::hammingThreshold` |
| time window | 48h | **48h** (unchanged) | `cluster::Params::windowSeconds` |

At these values, on this corpus:

```
966 articles -> 895 clusters, 60 multi-article, 56 spanning more than one source
exact-duplicate pairs 46/46 grouped (100.0%)
grouped pairs 86: strong 81, weak 0 (0.0%), mean Jaccard 0.81
largest cluster 4 articles, 22 ms
```

### Why 13, and where the edge is

A second corpus, gathered a day after the first, moved this number. On corpus 1 threshold
14 produced no false groupings at all; on corpus 2 it produced two. Both are shown, because
the disagreement is the finding:

| threshold | multi-clusters (2) | weak pairs (1) | weak pairs (2) | mean Jaccard (2) |
| --- | --- | --- | --- | --- |
| 8 | 56 | 0.0% | 0.0% | 0.93 |
| 10 | 57 | 0.0% | 0.0% | 0.91 |
| 12 | 59 | 0.0% | 0.0% | 0.87 |
| **13** | **59** | **0.0%** | **0.0%** | **0.86** |
| 14 | 62 | 0.0% | 1.2% | 0.80 |
| 15 | 68 | — | 4.9% | 0.70 |
| 16 | 79 | 16.1% | 17.9% | 0.56 |
| 17 | 109 | — | 33.2% | 0.42 |

Reading what threshold 14 adds over 13 on corpus 2 shows the trade honestly: seven correct
merges (the Naidoo proceedings across two more publishers, a stabbing in Ahrensburg, a CDU
leadership bid, three Hamburg local stories carried by two editions of the same paper) and
two wrong ones — *"Wann es zur Neuwahl in Sachsen-Anhalt käme"* pulled into *"Reaktionen
aus Hamburg"*, and an election map merged with a protest report. Both wrong pairs are
election coverage: articles that share a topic vocabulary without being the same story.

**13 is the highest value that was clean on both corpora**, and it is the default. The
gain from 13 to 14 is three clusters; the cost is entering the range where false merges
begin. This supersedes the single-corpus recommendation of 14 in the first version of this
document — one news cycle was not enough to settle it, exactly as the caveat at the end
warned.

### The range the settings offer

The threshold is a setting, but not a free one: **8 to 14**, clamped in `Settings` as well
as bounded in the UI, so a hand-edited configuration file cannot get around it either.

Past the top of that range the clusterer stops grouping stories and starts merging
unrelated ones — 18% wrong pairs at 16, a third at 17 — and a reader who slid there would
experience it as the app being broken rather than as a setting they chose. Below 8 nothing
measurable changes: 6 and 8 differ by two clusters.

The **time window stays widely adjustable (6h to 168h)**, because no value of it breaks
anything: a short window simply groups less. Restricting the knob that can do damage and
leaving alone the one that cannot is the whole of the policy.

### The time window is a safety rail, not a knob

At the recommended threshold, varying the window from 12h to 96h changes the result by
two clusters (59 / 60 / 60 / 61). Near-duplicate articles are published close together
anyway, so the window rarely binds. It earns its keep only at looser thresholds, where it
limits how far a runaway cluster can reach. Keeping 48h as the default costs nothing.

Both remain user-settable, as section 6 requires.

## Online versus batch

Section 6 specifies greedy online assignment: each article joins the closest match below
threshold, existing clusters never merge, and the result depends on arrival order.
`clusterBatch()` implements the transitive-closure alternative purely so the cost could
be quantified.

It is negligible — 57 versus 56 multi-clusters at the plan's original settings, with the
batch variant producing a slightly *larger* maximum cluster because transitivity chains
articles together. **The online simplification is the right call** and no redesign is
needed.

Runtime is 22 ms for the full 966-article corpus with a plain linear candidate scan. At a
48-hour window the candidate set is a few hundred rows. No LSH, no banding — do not
optimise this.

## What this does not establish

- **Two snapshots, one news cycle each.** Both corpora were dominated by the same story
  (the Sachsen-Anhalt election), which over-represents topic-vocabulary collisions. The
  second corpus already moved the recommended threshold from 14 to 13; a third might move
  it again. `tools/fetch-corpus.py --merge` accumulates across runs, and the value should
  be re-checked against a corpus spanning several days before it is treated as settled.
- **German only.** Every source is German. The algorithm is language-agnostic by
  construction, but the kicker convention and the 5-gram width have only been validated
  against one language.
- **Proxy metrics, not ground truth.** Both measures are automatic approximations with
  the biases described above.
- **No unit tests yet.** `normalize()`, `stripKicker()` and `simhash()` are pure functions
  and should have them.

## Running the harness

```sh
tools/fetch-corpus.py -o corpus.json          # or --merge to accumulate
cd tools/cluster-harness
sfdk -c target=SailfishOS-5.1.0.11-aarch64 build
sfdk tools exec SailfishOS-5.1.0.11-aarch64 \
    $PWD/cluster-harness $PWD/../../corpus.json
```

With no flags it reproduces exactly what the app does. Useful options:

| Option | Purpose |
| --- | --- |
| `-t N` / `-w HOURS` | threshold and window |
| `--sweep` | threshold × window grid with the evaluation metrics |
| `--grid` | compare n-gram width, weighting, field and kicker handling |
| `--mode compare` | online versus batch |
| `--keep-kicker`, `--field title+teaser`, `--ngram N`, `--weight W` | revert individual decisions above |
| `--min-size N`, `--limit N`, `--quiet` | control the listing |

The harness runs on the aarch64 target under scratchbox2's qemu, so it exercises the same
Qt and the same libraries as the device, without needing a device.
