#!/usr/bin/env python3
"""Read and check a .src story. One parser for every tool that touches one.

The limits checked here are the firmware's, not taste: Story.h caps the tree at
32 nodes and 16 flags, and Node.id is a char[16]. A story that breaks them boots
into an error on the board, which is the one failure the card cannot report.

    from story import parse, check, BUTTONS
"""
import pathlib, re

# Button order is the firmware's: BTN_PREV, BTN_PLAY, BTN_NEXT. The colours are
# the physical ones, shared with mock.py so both pages speak of the same box.
BUTTONS = ("prev", "play", "next")
COLORS = {"prev": "#FF1818", "play": "#18C838", "next": "#2060FF"}
NAMES = {"prev": "rouge", "play": "vert", "next": "bleu"}

MAX_NODES = 32          # Story.h:7
MAX_FLAGS = 16          # Story.h:8
MAX_ID = 15             # Node.id is char[16]
CHARS_PER_SECOND = 14.0 # story_build.py's estimate, kept identical


class Node:
    __slots__ = ("id", "text", "sets", "choices", "line", "opts")

    def __init__(self, nid, text, opts, line):
        self.id, self.text, self.opts, self.line = nid, text, opts, line
        self.sets = None
        self.choices = {}       # button -> (flag or None, target id)
        for tok in opts.split():
            key, _, value = tok.partition("=")
            if key == "set":
                self.sets = value
            elif key in BUTTONS:
                flag, _, target = value.rpartition("?")
                self.choices[key] = (flag or None, target)

    @property
    def words(self):
        return len(self.text.split())

    @property
    def seconds(self):
        return len(self.text) / CHARS_PER_SECOND

    def manifest(self, audio):
        """The line histoire.txt carries: "id audio.mp3 prev=x play=y next=z"."""
        return f"{self.id} {audio}" + (f" {self.opts}" if self.opts else "")


def parse(path):
    """Nodes in file order. Malformed lines raise; graph faults are check()'s job."""
    nodes = []
    for line, raw in enumerate(pathlib.Path(path).read_text(encoding="utf-8").splitlines(), 1):
        stripped = raw.split("#", 1)[0].strip()
        if not stripped:
            continue
        parts = [p.strip() for p in stripped.split("|")]
        if len(parts) < 2:
            raise ValueError(f"ligne {line} : il faut au moins un identifiant et un texte")
        nodes.append(Node(parts[0], parts[1], parts[2] if len(parts) > 2 else "", line))
    return nodes


def check(nodes):
    """Everything the board can only report as a boot-time error, plus the
    authoring traps: a flag granted but never tested, a node nobody reaches."""
    problems = []
    by_id = {}
    for n in nodes:
        if n.id in by_id:
            problems.append(f"{n.id} : identifiant en double (lignes {by_id[n.id].line} et {n.line})")
        by_id[n.id] = n

    if len(nodes) > MAX_NODES:
        problems.append(f"{len(nodes)} noeuds, le firmware en accepte {MAX_NODES}")

    flags_set = {n.sets for n in nodes if n.sets}
    if len(flags_set) > MAX_FLAGS:
        problems.append(f"{len(flags_set)} drapeaux, le firmware en accepte {MAX_FLAGS}")

    for n in nodes:
        if len(n.id) > MAX_ID:
            problems.append(f"{n.id} : identifiant de plus de {MAX_ID} caracteres")
        if not re.fullmatch(r"[A-Za-z0-9._-]+", n.id):
            problems.append(f"{n.id} : l'identifiant sert de nom de fichier, ASCII sans espace")
        if n.sets and len(n.sets) > MAX_ID:
            problems.append(f"{n.id} : drapeau {n.sets} de plus de {MAX_ID} caracteres")
        for btn, (flag, target) in n.choices.items():
            if target not in by_id:
                problems.append(f"{n.id} : {btn} pointe sur {target}, qui n'existe pas")
            if flag and flag not in flags_set:
                problems.append(f"{n.id} : {btn} attend le drapeau {flag}, que personne n'accorde")

    if nodes:
        seen, stack = set(), [nodes[0].id]
        while stack:
            cur = stack.pop()
            if cur in seen or cur not in by_id:
                continue
            seen.add(cur)
            stack += [t for _, t in by_id[cur].choices.values()]
        for n in nodes:
            if n.id not in seen:
                problems.append(f"{n.id} : inatteignable depuis {nodes[0].id}")

    tested = {flag for n in nodes for flag, _ in n.choices.values() if flag}
    for flag in sorted(flags_set - tested):
        problems.append(f"drapeau {flag} : accorde, mais aucun choix ne le teste")

    return problems


def stories(folder):
    """The .src files of a folder, in name order."""
    return sorted(pathlib.Path(folder).glob("*.src"))
