#!/usr/bin/env python3
"""Append the Artificial Analysis Intelligence Index to model names in opencode.json.

Source: the public leaderboard at https://artificialanalysis.ai/leaderboards/models
(its embedded `initialModels` data; the v2 API needs a key). A model gets a
" · AI <n>" suffix only when it maps to a leaderboard entry that has a score;
unscored or unknown models are left unsuffixed. Re-running refreshes scores.

Usage:
  scripts/aa_intelligence_index.py            # update ~/.config/opencode/opencode.json
  scripts/aa_intelligence_index.py --dry-run  # print the mapping only
  scripts/aa_intelligence_index.py --strip    # remove all suffixes
"""

import argparse
import json
import os
import re
import shutil
import sys
import time
import urllib.request

LEADERBOARD_URL = "https://artificialanalysis.ai/leaderboards/models"
SUFFIX_RE = re.compile(r"\s+· AI \d+$")

# Config ids whose leaderboard slug differs from the slugified id. Only add an
# entry when the leaderboard name clearly is the same model.
ALIASES = {
    "beam-free": "reflection-beam",                         # "Beam 1.0"
    "step-5-preview": "step-5",                             # "Step 5 Preview"
    "nemotron-3-ultra-550b-a55b": "nvidia-nemotron-3-ultra-550b-a55b",
}


def config_path() -> str:
    xdg = os.environ.get("XDG_CONFIG_HOME")
    base = xdg if xdg else os.path.expanduser("~/.config")
    return os.path.join(base, "opencode", "opencode.json")


def fetch_leaderboard() -> dict:
    req = urllib.request.Request(LEADERBOARD_URL, headers={"User-Agent": "Mozilla/5.0"})
    html = urllib.request.urlopen(req, timeout=30).read().decode("utf-8", "ignore")
    key = '\\"initialModels\\":'
    i = html.find(key)
    if i < 0:
        sys.exit("leaderboard page layout changed: initialModels not found")
    push = 'self.__next_f.push([1,"'
    start = html.rfind(push, 0, i) + len(push)
    end = html.find('"])</script>', i)
    chunk = json.loads('"' + html[start:end] + '"')
    j = chunk.find('"initialModels":') + len('"initialModels":')
    models, _ = json.JSONDecoder().raw_decode(chunk[j:])
    return {m["slug"]: m for m in models}


def slugify(s: str) -> str:
    return re.sub(r"-+", "-", re.sub(r"[^a-z0-9]", "-", s.lower())).strip("-")


def leaderboard_slug(model_id: str) -> str:
    bare = model_id.split("/")[-1].split(":")[0]        # openrouter vendor/id:free
    bare = re.sub(r"-thinking$", "", bare)              # thinking toggle, same model
    return ALIASES.get(bare, slugify(bare))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--config", default=config_path())
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--strip", action="store_true")
    args = ap.parse_args()

    with open(args.config) as f:
        cfg = json.load(f)
    board = {} if args.strip else fetch_leaderboard()

    scored = 0
    for pid, prov in cfg.get("provider", {}).items():
        for mid, model in prov.get("models", {}).items():
            base = SUFFIX_RE.sub("", model.get("name", mid))
            entry = board.get(leaderboard_slug(mid))
            score = entry.get("intelligenceIndex") if entry else None
            name = base if score is None else f"{base} · AI {round(score)}"
            if score is not None:
                scored += 1
            print(f"{pid:12} {mid:44} {name}")
            model["name"] = name

    print(f"\n{scored} models scored", file=sys.stderr)
    if args.dry_run:
        return
    backup = f"{args.config}.bak-aa-index-{time.strftime('%Y%m%d%H%M%S')}"
    shutil.copy2(args.config, backup)
    with open(args.config, "w") as f:
        json.dump(cfg, f, indent=4)
        f.write("\n")
    print(f"updated {args.config} (backup {backup})", file=sys.stderr)


if __name__ == "__main__":
    main()
