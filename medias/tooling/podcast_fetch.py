#!/usr/bin/env python3
"""Fetch one episode of a public podcast feed onto the SD card.

For personal listening only. Radio France podcasts are free to download and
carry `copyright: Radio France` -- putting an episode on your own card for
your own child is the ordinary use of a podcast feed; redistributing it, or
committing it to a repository, is not. Nothing this script produces belongs
in git.

    python3 medias/tooling/podcast_fetch.py <url du flux>          # liste
    python3 medias/tooling/podcast_fetch.py <url du flux> 3 medias/dist/podcasts/odyssees
    python3 medias/tooling/podcast_fetch.py <url du flux> tout medias/dist/podcasts/odyssees
"""
import pathlib, re, subprocess, sys, unicodedata, urllib.request, xml.etree.ElementTree as ET

IT = "{http://www.itunes.com/dtds/podcast-1.0.dtd}"
FEEDS = {
    "odyssees": "https://radiofrance-podcast.net/podcast09/rss_20108.xml",
    "oli": "https://radiofrance-podcast.net/podcast09/rss_19721.xml",
    "bestioles": "https://radiofrance-podcast.net/podcast09/rss_22046.xml",
    "pomme": "https://feed.ausha.co/B6r8OclKP6gn",
    "encore": "https://feeds.acast.com/public/shows/670d1795df4dd6f896655670",
}

def slugify(title, limit=60):
    # The card is vfat and the box logs paths over serial: ASCII only.
    title = title.replace("’", "'")
    ascii_only = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode()
    slug = re.sub(r"[^a-zA-Z0-9]+", "-", ascii_only).strip("-").lower()
    # A rerun lands on the original's name, so it is skipped rather than kept twice.
    slug = re.sub(r"(^|-)rediff(?=-|$)", "", slug).strip("-")
    if len(slug) <= limit:
        return slug
    head = slug[:limit + 1]
    cut = head.rsplit("-", 1)[0] if "-" in head else ""
    return cut or slug[:limit]

def load(url):
    url = FEEDS.get(url, url)
    with urllib.request.urlopen(url, timeout=30) as r:
        return ET.fromstring(r.read()).find("channel")

def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__ + "\nRaccourcis : " + ", ".join(FEEDS))

    ch = load(sys.argv[1])
    items = ch.findall("item")
    print(f'{ch.findtext("title")} — {len(items)} episodes — {ch.findtext("copyright") or "droits non declares"}\n')

    if len(sys.argv) == 2:
        for i, it in enumerate(items[:40], 1):
            print(f'  {i:3d}. {(it.findtext("title") or "")[:70]:72s} {it.findtext(IT + "duration") or ""}')
        if len(items) > 40:
            print(f"  ... {len(items) - 40} de plus")
        return

    out = pathlib.Path(sys.argv[3])
    if sys.argv[2] == "tout":
        failed = []
        for n, it in enumerate(items, 1):
            print(f"[{n}/{len(items)}] ", end="")
            try:
                fetch(it, out)
            except Exception as e:
                print(f"  echec : {e}")
                failed.append(it.findtext("title"))
        print(f"\n{len(items) - len(failed)} episodes dans {out}" + "".join(f"\n  echec : {t}" for t in failed))
        return

    dst = fetch(items[int(sys.argv[2]) - 1], out)
    card = dst.as_posix().split("/dist/", 1)[-1]
    print(f"\n  bind <uid> /{card}")

def fetch(it, out):
    title = it.findtext("title") or "episode"
    out.mkdir(parents=True, exist_ok=True)
    slug = slugify(title)
    src, part, dst = out / f"{slug}.src", out / f"{slug}.part", out / f"{slug}.mp3"
    if dst.exists():
        print(f'"{title}" deja la')
        return dst

    try:
        print(f'"{title}"\n  telechargement...')
        urllib.request.urlretrieve(it.find("enclosure").get("url"), src)

        # Les flux servent du m4a.
        print("  transcodage en MP3 mono 22,05 kHz...")
        subprocess.run(
            ["gst-launch-1.0", "-q", "filesrc", f"location={src}", "!", "decodebin",
             "!", "audioconvert", "!", "audioresample", "!", "audio/x-raw,rate=22050,channels=1",
             "!", "lamemp3enc", "bitrate=64", "!", "filesink", f"location={part}"],
            check=True, capture_output=True)
        # Renamed last, so an interrupted run never leaves a .mp3 that looks done.
        part.rename(dst)
    finally:
        src.unlink(missing_ok=True)
        part.unlink(missing_ok=True)
    print(f"  {dst}  ({dst.stat().st_size // 1024} Ko)")
    return dst

if __name__ == "__main__":
    main()
