#!/usr/bin/env python3
"""Fetch one episode of a public podcast feed onto the SD card.

For personal listening only. Radio France podcasts are free to download and
carry `copyright: Radio France` -- putting an episode on your own card for
your own child is the ordinary use of a podcast feed; redistributing it, or
committing it to a repository, is not. Nothing this script produces belongs
in git.

    python3 medias/tooling/podcast_fetch.py <url du flux>          # liste
    python3 medias/tooling/podcast_fetch.py <url du flux> 3 medias/dist/podcasts/odyssees/tresor
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

def slugify(title):
    # The card is vfat and the box logs paths over serial: ASCII only.
    ascii_only = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode()
    return re.sub(r"[^a-zA-Z0-9]+", "-", ascii_only).strip("-").lower()[:40].strip("-")

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

    idx = int(sys.argv[2]) - 1
    out = pathlib.Path(sys.argv[3])
    it = items[idx]
    title = it.findtext("title") or f"episode{idx}"
    url = it.find("enclosure").get("url")

    out.mkdir(parents=True, exist_ok=True)
    slug = slugify(title)
    src, dst = out / f"{slug}.src", out / f"01-{slug}.mp3"

    print(f'"{title}"\n  telechargement...')
    urllib.request.urlretrieve(url, src)

    # Les flux servent du m4a.
    print("  transcodage en MP3 mono 22,05 kHz...")
    subprocess.run(
        ["gst-launch-1.0", "-q", "filesrc", f"location={src}", "!", "decodebin",
         "!", "audioconvert", "!", "audioresample", "!", "audio/x-raw,rate=22050,channels=1",
         "!", "lamemp3enc", "bitrate=64", "!", "filesink", f"location={dst}"],
        check=True, capture_output=True)
    src.unlink()
    print(f"  {dst}  ({dst.stat().st_size // 1024} Ko)")
    print(f"\n  bind <uid> {out}")

if __name__ == "__main__":
    main()
