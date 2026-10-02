"""python3 -m unittest discover -s medias/tooling"""
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

import story

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def write(folder, name, text):
    path = pathlib.Path(folder) / name
    path.write_text(text, encoding="utf-8")
    return path


class FirmwareLimits(unittest.TestCase):
    """The tools check against the firmware's caps, copied by hand."""

    def test_limits_match_story_h(self):
        header = (ROOT / "firmware/src/Story.h").read_text()
        caps = dict(re.findall(r"constexpr uint8_t (MAX_\w+) = (\d+);", header))
        self.assertEqual(int(caps["MAX_NODES"]), story.MAX_NODES)
        self.assertEqual(int(caps["MAX_FLAGS"]), story.MAX_FLAGS)

        checker = (HERE / "story_check.py").read_text()
        nodes, flags = re.search(r"MAX_NODES, MAX_FLAGS = (\d+), (\d+)", checker).groups()
        self.assertEqual((int(nodes), int(flags)), (story.MAX_NODES, story.MAX_FLAGS))


class SourceStories(unittest.TestCase):
    def check(self, text):
        with tempfile.TemporaryDirectory() as d:
            return story.check(story.parse(write(d, "s.src", text)))

    def test_parse(self):
        with tempfile.TemporaryDirectory() as d:
            nodes = story.parse(write(d, "s.src", "# comment\na | Il etait une fois | next=b set=cle\nb | Fin\n"))
        self.assertEqual([n.id for n in nodes], ["a", "b"])
        self.assertEqual(nodes[0].choices, {"next": (None, "b")})
        self.assertEqual(nodes[0].sets, "cle")
        self.assertEqual(nodes[0].manifest("a.mp3"), "a a.mp3 next=b set=cle")

    def test_conditional_choice(self):
        with tempfile.TemporaryDirectory() as d:
            nodes = story.parse(write(d, "s.src", "a | x | prev=cle?b\nb | y\n"))
        self.assertEqual(nodes[0].choices["prev"], ("cle", "b"))

    def test_line_without_text_raises(self):
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(ValueError):
                story.parse(write(d, "s.src", "lonely\n"))

    def test_sound_story_has_no_problem(self):
        self.assertEqual(self.check("a | x | prev=b next=c\nb | y | set=cle next=c\nc | z | play=cle?a\n"), [])

    def test_graph_faults(self):
        problems = self.check("a | x | next=nowhere prev=cle?a\nb | orphan\nb | dup\n")
        joined = "\n".join(problems)
        self.assertIn("nowhere", joined)
        self.assertIn("en double", joined)
        self.assertIn("inatteignable", joined)
        self.assertIn("que personne n'accorde", joined)

    def test_firmware_caps(self):
        many = "".join(f"n{i} | x | next=n{i + 1}\n" for i in range(story.MAX_NODES)) + f"n{story.MAX_NODES} | fin\n"
        self.assertTrue(any("noeuds" in p for p in self.check(many)))
        self.assertTrue(any("caracteres" in p for p in self.check("identifiant-bien-trop-long | x\n")))


class CardManifest(unittest.TestCase):
    """story_check.py on a folder as it lands on the card."""

    def run_check(self, manifest, files=()):
        with tempfile.TemporaryDirectory() as d:
            write(d, "histoire.txt", manifest)
            for f in files:
                write(d, f, "")
            return subprocess.run([sys.executable, str(HERE / "story_check.py"), d],
                                  capture_output=True, text=True)

    def test_valid_folder(self):
        r = self.run_check("a a.mp3 next=b\nb b.mp3\n", files=("a.mp3", "b.mp3"))
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_builtin_needs_no_file(self):
        r = self.run_check("a builtin:boot\n")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)

    def test_missing_audio_and_target(self):
        r = self.run_check("a a.mp3 next=nowhere\n")
        self.assertEqual(r.returncode, 1)
        self.assertIn("fichier absent", r.stdout)
        self.assertIn("nowhere", r.stdout)


class ShippedStories(unittest.TestCase):
    def test_every_source_story_would_load(self):
        # A flag granted but never tested is an authoring hint, not a fault:
        # the board plays the story all the same.
        for path in story.stories(ROOT / "medias/src/histoires"):
            with self.subTest(path.name):
                faults = [p for p in story.check(story.parse(path)) if "aucun choix ne le teste" not in p]
                self.assertEqual(faults, [])


if __name__ == "__main__":
    unittest.main()
