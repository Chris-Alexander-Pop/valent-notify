#!/usr/bin/env python3
"""Make Valent start with valent-notify LD_PRELOAD (D-Bus + systemd user)."""
from __future__ import annotations

import subprocess
from pathlib import Path

NAME = "ca.andyholmes.Valent"
HOME = Path.home()
WRAPPER = HOME / ".local/bin/valent"
SO = HOME / ".local/lib/libvalent-notify.so"
DBUS_SERVICE = HOME / ".local/share/dbus-1/services" / f"{NAME}.service"
USER_UNIT = HOME / ".config/systemd/user" / f"{NAME}.service"
DESKTOP = HOME / ".local/share/applications" / f"{NAME}.desktop"


def write_if_changed(path: Path, body: str, label: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text() == body:
        print(f"{label} already current ({path})")
        return
    path.write_text(body)
    print(f"wrote {path}")


def main() -> None:
    if not WRAPPER.is_file():
        raise SystemExit(f"missing {WRAPPER}")

    write_if_changed(
        DBUS_SERVICE,
        (
            "[D-BUS Service]\n"
            f"Name={NAME}\n"
            f"Exec={WRAPPER} --gapplication-service\n"
            f"SystemdService={NAME}.service\n"
        ),
        "dbus service",
    )

    write_if_changed(
        USER_UNIT,
        (
            "[Unit]\n"
            "Description=Valent device sync (valent-notify preload)\n"
            "PartOf=graphical-session.target\n"
            "After=graphical-session.target\n"
            "\n"
            "[Service]\n"
            "Type=dbus\n"
            f"BusName={NAME}\n"
            f"ExecStart={WRAPPER} --gapplication-service\n"
            f"Environment=LD_PRELOAD={SO}\n"
            "Environment=GNOTIFICATION_BACKEND=freedesktop\n"
            "Restart=on-failure\n"
            "\n"
            "[Install]\n"
            "WantedBy=graphical-session.target\n"
        ),
        "systemd user unit",
    )

    if DESKTOP.is_file():
        lines = DESKTOP.read_text().splitlines(keepends=True)
        out: list[str] = []
        changed = False
        for line in lines:
            if line.startswith("Exec=") and str(WRAPPER) not in line:
                out.append(f"Exec={WRAPPER} %U\n")
                changed = True
            else:
                out.append(line)
        if changed:
            DESKTOP.write_text("".join(out))
            print(f"patched Exec in {DESKTOP}")
        else:
            print("desktop Exec already uses wrapper")

    subprocess.run(["systemctl", "--user", "daemon-reload"], check=False)


if __name__ == "__main__":
    main()
