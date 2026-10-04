"""python3 -m unittest discover -s medias/tooling"""
import unittest

from podcast_fetch import slugify


class Slugify(unittest.TestCase):
    def test_ascii_only(self):
        self.assertEqual(slugify("Noël magique à Perpète"), "noel-magique-a-perpete")

    def test_curly_apostrophe_splits_words(self):
        self.assertEqual(slugify("c’est le roi"), "c-est-le-roi")

    def test_cuts_between_words(self):
        title = "Le taon des pluies : une petite dame vampire obstinée (en audio 3D)"
        self.assertEqual(slugify(title), "le-taon-des-pluies-une-petite-dame-vampire-obstinee-en-audio")
        self.assertEqual(slugify("un-deux-trois", limit=9), "un-deux")

    def test_unbroken_word_is_cut_hard(self):
        self.assertEqual(slugify("a" * 80), "a" * 60)

    def test_rerun_shares_the_original_name(self):
        self.assertEqual(slugify("Cendrillon - REDIFF"), slugify("Cendrillon"))
        self.assertEqual(slugify("Qui pour remplacer le Père Noël ? (REDIFF)"),
                         "qui-pour-remplacer-le-pere-noel")
        self.assertEqual(slugify("Rediffusion spéciale"), "rediffusion-speciale")


if __name__ == "__main__":
    unittest.main()
