#!/usr/bin/env python3
"""Validate a story folder before copying it to the SD card.

Catches what the box can only report as a boot-time error: a choice pointing
at a node that does not exist, a missing audio file, an unreachable branch.

    python3 medias/tooling/story_check.py /chemin/vers/histoire
"""
import sys, pathlib

MAX_NODES, MAX_FLAGS = 32, 16
KEYS = ("prev", "play", "next")

def parse(path):
    nodes, order = {}, []
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) < 2:
            yield_err(f"ligne {lineno} : il faut au moins un identifiant et un fichier")
        nid, audio, choices, grants = parts[0], parts[1], {}, None
        for tok in parts[2:]:
            if "=" not in tok:
                yield_err(f"ligne {lineno} : \"{tok}\" n'est pas cle=valeur")
                continue
            k, v = tok.split("=", 1)
            if k == "set":
                grants = v
                continue
            if k not in KEYS:
                yield_err(f"ligne {lineno} : cle inconnue \"{k}\" (attendu set/{'/'.join(KEYS)})")
                continue
            # "drapeau?cible" : la branche n'existe qu'une fois le drapeau acquis
            cond, target = (v.split("?", 1) if "?" in v else (None, v))
            choices[k] = (target, cond)
        if nid in nodes:
            yield_err(f"ligne {lineno} : identifiant \"{nid}\" en double")
        if len(nid) > 15:
            yield_err(f"ligne {lineno} : identifiant \"{nid}\" depasse 15 caracteres")
        nodes[nid] = (audio, choices, lineno, grants)
        order.append(nid)
    return nodes, order

ERRORS = []
def yield_err(msg):
    ERRORS.append(msg)

def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    folder = pathlib.Path(sys.argv[1])
    manifest = folder / "histoire.txt"
    if not manifest.is_file():
        sys.exit(f"{manifest} introuvable")

    nodes, order = parse(manifest)
    if not nodes:
        sys.exit("aucun noeud")
    if len(nodes) > MAX_NODES:
        yield_err(f"{len(nodes)} noeuds, le firmware en accepte {MAX_NODES}")

    granted = {g for (_, _, _, g) in nodes.values() if g}
    if len(granted) > MAX_FLAGS:
        yield_err(f"{len(granted)} drapeaux, le firmware en accepte {MAX_FLAGS}")

    for audio, choices, lineno, _ in nodes.values():
        if not audio.startswith("builtin:") and not (folder / audio).is_file():
            yield_err(f"ligne {lineno} : fichier absent \"{audio}\"")
        for k, (target, cond) in choices.items():
            if target not in nodes:
                yield_err(f"ligne {lineno} : {k}= pointe vers \"{target}\", qui n'existe pas")
            # Un drapeau qu'aucun noeud n'accorde rend la branche inatteignable.
            if cond and cond not in granted:
                yield_err(f"ligne {lineno} : {k}= exige \"{cond}\", qu'aucun noeud n'accorde")

    seen, stack = set(), [order[0]]
    while stack:
        nid = stack.pop()
        if nid in seen:
            continue
        seen.add(nid)
        stack.extend(t for (t, _) in nodes[nid][1].values() if t in nodes)
    for nid in order:
        if nid not in seen:
            yield_err(f"noeud \"{nid}\" inatteignable depuis \"{order[0]}\"")

    endings = [n for n in order if not nodes[n][1]]
    flags = f", {len(granted)} drapeau(x)" if granted else ""
    if not endings:
        yield_err("aucune fin : toutes les branches bouclent")

    if ERRORS:
        print(f"{len(ERRORS)} probleme(s) :")
        for e in ERRORS:
            print("  -", e)
        sys.exit(1)

    print(f"OK : {len(nodes)} noeuds, depart \"{order[0]}\", {len(endings)} fin(s){flags}")

if __name__ == "__main__":
    main()
