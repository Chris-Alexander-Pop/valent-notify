#!/usr/bin/env python3
"""Insert LD_PRELOAD for libvalent-notify into ~/.local/bin/valent."""
from __future__ import annotations

from pathlib import Path

WRAPPER = Path.home() / ".local/bin/valent"
MARKER = "libvalent-notify.so"
SNIPPET = """
# valent-notify: rewrite phone notifications (app name / icon / mute)
_VN_PRELOAD="${HOME}/.local/lib/libvalent-notify.so"
if [[ -f "$_VN_PRELOAD" ]]; then
  case ":${LD_PRELOAD:-}:" in
    *":${_VN_PRELOAD}:"*) ;;
    *) export LD_PRELOAD="${_VN_PRELOAD}${LD_PRELOAD:+:$LD_PRELOAD}" ;;
  esac
fi
"""


def main() -> None:
    if not WRAPPER.is_file():
        raise SystemExit(f"missing {WRAPPER}")
    text = WRAPPER.read_text()
    if MARKER in text:
        print("valent wrapper already loads valent-notify")
        return
    if "exec " not in text:
        raise SystemExit("valent wrapper has no exec line to patch")
    # Inject immediately before the final exec.
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    injected = False
    for i, line in enumerate(lines):
        if not injected and line.startswith("exec "):
            out.append(SNIPPET.lstrip("\n"))
            if not out[-1].endswith("\n"):
                out.append("\n")
            injected = True
        out.append(line)
    if not injected:
        raise SystemExit("could not find exec in valent wrapper")
    WRAPPER.write_text("".join(out))
    print(f"patched {WRAPPER}")


if __name__ == "__main__":
    main()
