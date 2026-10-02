#!/usr/bin/env python3
"""Synthesise the boot jingle and write it as firmware/src/sounds/BootMp3.h.

Built from sines, not sampled: no licence to carry, and the same script gives
the same sound. Needs numpy and gst-launch-1.0 (lamemp3enc).

    python3 medias/tooling/jingle.py           # writes the header
    python3 medias/tooling/jingle.py --play    # listen on this PC first
"""
import pathlib, subprocess, sys, tempfile, wave
import numpy as np

SR = 22050
# Under the button tones (0.37): a greeting, not an alarm.
PEAK = 0.2
ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "firmware" / "src" / "sounds" / "BootMp3.h"


def music_box(f, dur=0.9):
    t = np.arange(int(SR * dur)) / SR
    phase = 2 * np.pi * f * t
    # A tine: fundamental, a fast-dying octave and a metallic inharmonic partial.
    out = (np.sin(phase) * np.exp(-t * 5)
           + 0.25 * np.sin(2.0 * phase) * np.exp(-t * 12.5)
           + 0.12 * np.sin(5.4 * phase) * np.exp(-t * 30))
    return out * np.minimum(1, t / 0.004)


def jingle():
    out = np.zeros(int(SR * 1.5))

    def place(sig, at):
        i = int(at * SR)
        n = min(len(out) - i, len(sig))
        out[i:i + n] += sig[:n]

    # Rising C major arpeggio, G5 to G6, then a C7 sparkle left to ring.
    for at, f in ((0.00, 784), (0.11, 1047), (0.22, 1319), (0.33, 1568)):
        place(music_box(f), at)
    place(0.6 * music_box(2093, 1.0), 0.48)

    # A few early reflections: a box, not an anechoic chamber.
    dry = out.copy()
    for delay, gain in ((0.031, 0.5), (0.047, 0.4), (0.071, 0.3), (0.109, 0.2)):
        n = int(delay * SR)
        out[n:] += dry[:-n] * gain * 0.54

    fade = int(0.05 * SR)
    out[-fade:] *= np.linspace(1, 0, fade)
    return out / np.max(np.abs(out)) * PEAK


def write_wav(path, x):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes((x * 32767).astype(np.int16).tobytes())


def main():
    with tempfile.TemporaryDirectory() as tmp:
        wav, mp3 = pathlib.Path(tmp) / "boot.wav", pathlib.Path(tmp) / "boot.mp3"
        write_wav(wav, jingle())
        if "--play" in sys.argv:
            subprocess.run(["pw-play", str(wav)], check=True)
            return

        subprocess.run(
            ["gst-launch-1.0", "-q", "filesrc", f"location={wav}", "!", "wavparse",
             "!", "audioconvert", "!", "lamemp3enc", "bitrate=48", "!",
             "filesink", f"location={mp3}"],
            check=True, capture_output=True)
        data = mp3.read_bytes()

    rows = ",\n".join("\t" + ",".join(f"0x{b:02x}" for b in data[i:i + 16])
                      for i in range(0, len(data), 16))
    HEADER.write_text(
        "#pragma once\n#include <Arduino.h>\n\n"
        "// Boot jingle, 1.5 s: a music-box arpeggio, the box's signature.\n"
        "// Synthesised by medias/tooling/jingle.py -- no third-party audio.\n"
        "// Mono 22.05 kHz 48 kbps MP3. Generated, do not edit.\n\n"
        f"constexpr size_t BOOT_MP3_LEN = {len(data)};\n"
        f"const uint8_t BOOT_MP3[] = {{\n{rows}\n}};\n")
    print(f"{HEADER.relative_to(ROOT)}  {len(data)} bytes")


if __name__ == "__main__":
    main()
