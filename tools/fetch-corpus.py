#!/usr/bin/env python3
"""Build a clustering corpus from real feeds.

The clustering harness reads a JSON dump of articles (project_description.md section 6).
This produces that dump. It is a development tool, not part of the app -- the app has its
own C++ fetcher and parser (milestone 3). Keeping the two separate means the corpus can be
rebuilt before the parser exists, and the harness can be tuned against frozen data.

Handles the three formats the plan requires: RSS 2.0, Atom 1.0 and RSS 1.0 / RDF. NDR really
does serve RDF, so this is not hypothetical.

Usage:
    tools/fetch-corpus.py [-o corpus.json] [--feeds tools/corpus-feeds.json]
    tools/fetch-corpus.py --merge corpus.json   # accumulate over several runs
"""

import argparse
import gzip
import html
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from xml.etree import ElementTree as ET

# Section 5 of the plan: honest, identifiable User-Agent. Same string the app will send.
USER_AGENT = "myNews/0.1 (+https://github.com/kaiwiechers/myNews)"
CONNECT_TIMEOUT = 30
MAX_BYTES = 5 * 1024 * 1024
PER_HOST_DELAY = 1.0

NS = {
    "atom": "http://www.w3.org/2005/Atom",
    "dc": "http://purl.org/dc/elements/1.1/",
    "content": "http://purl.org/rss/1.0/modules/content/",
    "media": "http://search.yahoo.com/mrss/",
    "rss1": "http://purl.org/rss/1.0/",
}

TAG_RE = re.compile(r"<[^>]+>")
WS_RE = re.compile(r"\s+")
IMG_RE = re.compile(r'<img[^>]+src=["\']([^"\']+)["\']', re.I)


def strip_html(text):
    if not text:
        return ""
    text = TAG_RE.sub(" ", text)
    text = html.unescape(text)
    return WS_RE.sub(" ", text).strip()


def parse_date(value):
    """RFC 822 and ISO 8601, as section 5 requires. Returns unix seconds or None."""
    if not value:
        return None
    value = value.strip()
    try:
        dt = parsedate_to_datetime(value)
        if dt is not None:
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=timezone.utc)
            return int(dt.timestamp())
    except (TypeError, ValueError, IndexError):
        pass
    iso = value.replace("Z", "+00:00")
    for candidate in (iso, iso[:19]):
        try:
            dt = datetime.fromisoformat(candidate)
            if dt.tzinfo is None:
                dt = dt.replace(tzinfo=timezone.utc)
            return int(dt.timestamp())
        except ValueError:
            continue
    return None


def fetch(url):
    req = urllib.request.Request(url, headers={
        "User-Agent": USER_AGENT,
        "Accept": "application/rss+xml, application/atom+xml, application/xml;q=0.9, */*;q=0.8",
        "Accept-Encoding": "gzip",
    })
    with urllib.request.urlopen(req, timeout=CONNECT_TIMEOUT) as resp:
        raw = resp.read(MAX_BYTES + 1)
        if len(raw) > MAX_BYTES:
            raise ValueError("response exceeds %d bytes" % MAX_BYTES)
        if resp.headers.get("Content-Encoding") == "gzip":
            raw = gzip.GzipFile(fileobj=io.BytesIO(raw)).read()
        return raw


def local(tag):
    return tag.rsplit("}", 1)[-1] if "}" in tag else tag


def first_text(elem, *paths):
    for path in paths:
        found = elem.find(path, NS)
        if found is not None and (found.text or "").strip():
            return found.text.strip()
    return ""


def extract_image(elem, body_html):
    for path in ("media:content", "media:thumbnail"):
        node = elem.find(path, NS)
        if node is not None and node.get("url"):
            return node.get("url")
    for enc in elem.findall("enclosure"):
        if (enc.get("type") or "").startswith("image/") and enc.get("url"):
            return enc.get("url")
    if body_html:
        m = IMG_RE.search(body_html)
        if m:
            return m.group(1)
    return ""


def parse_feed(raw, feed):
    root = ET.fromstring(raw)
    root_tag = local(root.tag)
    articles = []

    if root_tag == "feed":            # Atom 1.0
        entries = root.findall("atom:entry", NS)
        fmt = "atom"
    elif root_tag == "RDF":           # RSS 1.0 / RDF
        entries = root.findall("rss1:item", NS)
        fmt = "rdf"
    else:                             # RSS 2.0
        channel = root.find("channel")
        entries = channel.findall("item") if channel is not None else root.findall("item")
        fmt = "rss"

    for entry in entries:
        if fmt == "atom":
            title = first_text(entry, "atom:title")
            link_node = entry.find("atom:link[@rel='alternate']", NS) or entry.find("atom:link", NS)
            link = link_node.get("href") if link_node is not None else ""
            guid = first_text(entry, "atom:id") or link
            body = first_text(entry, "atom:content", "atom:summary")
            published = parse_date(first_text(entry, "atom:published", "atom:updated"))
        elif fmt == "rdf":
            title = first_text(entry, "rss1:title")
            link = first_text(entry, "rss1:link")
            guid = entry.get("{http://www.w3.org/1999/02/22-rdf-syntax-ns#}about") or link
            body = first_text(entry, "rss1:description", "content:encoded")
            published = parse_date(first_text(entry, "dc:date"))
        else:
            title = first_text(entry, "title")
            link = first_text(entry, "link")
            guid = first_text(entry, "guid") or link
            body = first_text(entry, "description", "content:encoded")
            published = parse_date(first_text(entry, "pubDate", "dc:date"))

        title = strip_html(title)
        if not title:
            continue

        articles.append({
            "source": feed["source"],
            "feed_url": feed["url"],
            "lang": feed.get("lang", "de"),
            "region": feed.get("region", ""),
            "format": fmt,
            "guid": guid or link or title,
            "link": link,
            "title": title,
            # Section 5: teaser truncated to ~300 chars for storage.
            "teaser": strip_html(body)[:300],
            "published": published,
            "image_url": extract_image(entry, body),
        })
    return articles


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-o", "--output", default="corpus.json")
    ap.add_argument("--feeds", default=os.path.join(os.path.dirname(__file__), "corpus-feeds.json"))
    ap.add_argument("--merge", action="store_true",
                    help="merge into an existing output file instead of replacing it")
    args = ap.parse_args()

    feeds = json.load(open(args.feeds, encoding="utf-8"))["feeds"]

    existing = {}
    if args.merge and os.path.exists(args.output):
        for a in json.load(open(args.output, encoding="utf-8"))["articles"]:
            existing[(a["feed_url"], a["guid"])] = a
        print("merging into %d existing articles" % len(existing), file=sys.stderr)

    now = int(time.time())
    stats = []
    for feed in feeds:
        try:
            raw = fetch(feed["url"])
            articles = parse_feed(raw, feed)
        except (urllib.error.URLError, ET.ParseError, ValueError, OSError) as exc:
            print("  %-16s FAILED: %s" % (feed["source"], exc), file=sys.stderr)
            stats.append((feed["source"], "-", 0, 0))
            continue

        added = 0
        for a in articles:
            # Section 5: fall back to first-seen when the feed gives no usable date.
            if a["published"] is None:
                a["published"] = now
                a["published_source"] = "first_seen"
            else:
                a["published_source"] = "feed"
            key = (a["feed_url"], a["guid"])
            if key not in existing:
                added += 1
            existing[key] = a

        fmt = articles[0]["format"] if articles else "-"
        stats.append((feed["source"], fmt, len(articles), added))
        print("  %-16s %-5s %3d items, %3d new" % (feed["source"], fmt, len(articles), added),
              file=sys.stderr)
        time.sleep(PER_HOST_DELAY)

    out = sorted(existing.values(), key=lambda a: a["published"], reverse=True)
    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump({"fetched_at": now, "articles": out}, fh, ensure_ascii=False, indent=1)

    fmts = {}
    for a in out:
        fmts[a["format"]] = fmts.get(a["format"], 0) + 1
    print("\n%d articles from %d feeds -> %s" % (len(out), len(feeds), args.output), file=sys.stderr)
    print("formats: %s" % ", ".join("%s=%d" % kv for kv in sorted(fmts.items())), file=sys.stderr)


if __name__ == "__main__":
    main()
