# valent-notify

Valent forwards every phone notification as **Valent** (`ca.andyholmes.Valent`) with no Android app name, no FDO category, and (on GLib 2.88) no PNG icon. SwayNC then groups them as one uncategorized stream and you cannot mute “just YouTube”.

This is a tiny **LD_PRELOAD** middleman. It sits inside Valent, reads the KDE Connect `appName` / `packageName` from the packet, and rewrites the `Notify` call before swaync sees it:

- `app_name` becomes the Android app (“Discord”, “Gmail”, …)
- `desktop-entry` / `app_icon` come from a catalog or a generated `.desktop`
- `category` is set (`im.received`, `email.arrived`, …)
- phone PNG icons GLib would drop are dumped and passed as `image-path`
- `~/.config/valent-notify/config.json` can **mute** by app, package, title, or body
- repeats of the same title/body are dropped (phone screen-wake re-sends the shade)
- every toast is appended to a JSONL log so you can decide what to silence

## Install

```bash
make
make install
valent-notify restart
```

`install` copies `~/.local/lib/libvalent-notify.so`, patches `~/.local/bin/valent` to export `LD_PRELOAD`, installs a user systemd/D-Bus unit so activation does not start bare `/usr/bin/valent` (that path keeps every toast labeled **Valent**), and tells swaync to ignore the internal mute sink.

## Log

```bash
valent-notify log        # last 40
valent-notify log 100
valent-notify log -f    # follow
valent-notify unknown     # unmatched app names to map or mute
```

Raw file: `~/.local/state/valent-notify/notifications.jsonl`

Each line has `ts`, `app`, `pkg`, `id`, `summary`, `body`, `muted`, `mapped`, `deduped`, `shown_as`, `category`. See a noisy one, then add it to `mute` in the config.

Set `"log": false` to turn this off.

## Config

Edit with `valent-notify config` (or the JSON file directly). Changes apply on the next notification (mtime reload). No Valent restart needed for config edits.

```json
{
  "enabled": true,
  "log": true,
  "log_unknown": true,
  "mute_media": true,
  "dedupe": true,
  "dedupe_ttl_hours": 48,
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
- `"mute_media": true` (default) drops now-playing toasts with Play/Pause/Next actions, even when the title is the song name
- `"dedupe": true` (default) drops a toast if we already showed that exact app/title/body. Unlocking the phone re-sends every undismissed notification. Valent reuses the id. SwayNC still pops a banner. We suppress the identical content. `"dedupe_ttl_hours"` (default 48) is how long a fingerprint is kept. `0` keeps it until Valent exits (and on disk until you delete `~/.local/state/valent-notify/seen`). Set `"log_deduped": true` to log the suppressed repeats.

`apps` overrides the built-in catalog (Discord, Gmail, Messages, …). `match` / `pkg` use the same exact-or-regex rules.

Unmatched apps still get the raw Android name instead of “Valent”.

## Commands

```text
valent-notify log        # recent notifications (mute fodder)
valent-notify unknown     # unmatched Android apps
valent-notify config      # $EDITOR
valent-notify status      # preload loaded?
valent-notify restart     # bounce Valent
```

## Notes

- Click actions still belong to Valent (reply / open-on-phone). We only change how the toast is labeled.
- Generated stubs live in `~/.local/share/applications/valent-notify-*.desktop` (`NoDisplay=true`).
- If toasts still say **Valent** with the Valent icon, the preload is not loaded: `valent-notify status` then `valent-notify restart`. Session D-Bus activation of `/usr/bin/valent` skips the wrapper; install puts a systemd user unit in front of that.
- Muted toasts are dropped before swaync (a D-Bus reply is sent so Valent does not retry). SwayNC also ignores `app-name` / `desktop-entry` `__valent-notify-muted__`.
- Duplicate toasts use that same drop path. Unlocking the phone re-sends every undismissed notification. Valent reuses the id. SwayNC still alerts. We suppress the identical content.
