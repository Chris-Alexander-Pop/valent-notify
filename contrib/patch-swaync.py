#!/usr/bin/env python3
"""Make swaync ignore muted valent-notify sink notifications."""
from __future__ import annotations

import json
from pathlib import Path

CONFIG = Path.home() / ".config/swaync/config.json"
KEY = "valent-notify-muted"
RULE = {
    "state": "ignored",
    "app-name": "^__valent-notify-muted__$",
}


def main() -> None:
    if not CONFIG.is_file():
        print("no swaync config; skip")
        return
    data = json.loads(CONFIG.read_text())
    vis = data.setdefault("notification-visibility", {})
    if vis.get(KEY) == RULE:
        print("swaync already ignores muted valent-notify sink")
        return
    vis[KEY] = RULE
    CONFIG.write_text(json.dumps(data, indent=2) + "\n")
    print(f"patched {CONFIG}")


if __name__ == "__main__":
    main()
