# valent-notify

Valent forwards every phone notification as **Valent** (`ca.andyholmes.Valent`) with no Android app name, no FDO category, and (on GLib 2.88) no PNG icon. SwayNC then groups them as one uncategorized stream and you cannot mute “just YouTube”.

This is a tiny **LD_PRELOAD** middleman. It sits inside Valent, reads the KDE Connect `appName` / `packageName` from the packet, and rewrites the `Notify` call before swaync sees it:

- `app_name` becomes the Android app (“Discord”, “Gmail”, …)
- `desktop-entry` / `app_icon` come from a catalog or a generated `.desktop`
- `category` is set (`im.received`, `email.arrived`, …)
- phone PNG icons GLib would drop are dumped and passed as `image-path`
- `~/.config/valent-notify/config.json` can **mute** by app, package, title, or body

## Install

```bash
make
make install
valent-notify restart
```

`install` copies `~/.local/lib/libvalent-notify.so`, patches `~/.local/bin/valent` to export `LD_PRELOAD`, and tells swaync to ignore the internal mute sink.

## Config

Edit with `valent-notify config` (or the JSON file directly). Changes apply on the next notification (mtime reload). No Valent restart needed for config edits.

```json
{
  "enabled": true,
  "log_unknown": true,
  "mute": [
    "YouTube",
    { "app": "Android System" },
    { "pkg": "com.google.android.gms" },
    { "summary": "(?i)is charging" },
    { "body": "(?i)backup complete" }
  ],
  "apps": [
    {
      "match": "MyBank",
      "name": "MyBank",
      "desktop_entry": "mybank",
      "icon": "accessories-calculator",
      "category": "device"
    }
  ]
}
```

Mute entries:

- a string → Android **app name**, case-insensitive exact match
- `{ "app": "…" }` same, or a regex if it contains `.*+?[](){}|^$\`
- `{ "pkg": "com.example.app" }` Android package name
- `{ "summary": "regex" }` / `{ "body": "regex" }` against title/body
- several fields on one object are AND

`apps` overrides the built-in catalog (Discord, Gmail, Messages, …). `match` / `pkg` use the same exact-or-regex rules.

Unmatched apps still get the raw Android name instead of “Valent”. If `log_unknown` is true they are appended to:

`~/.local/state/valent-notify/unknown.jsonl`

```bash
valent-notify unknown
```

Add noisy ones to `mute`, or add an `apps` entry for the right icon / desktop file.

## Commands

```text
valent-notify status    # preload loaded?
valent-notify config    # $EDITOR
valent-notify unknown    # unmatched log
valent-notify restart    # bounce Valent
```

## Notes

- Click actions still belong to Valent (reply / open-on-phone). We only change how the toast is labeled.
- Generated stubs live in `~/.local/share/applications/valent-notify-*.desktop` (`NoDisplay=true`).
