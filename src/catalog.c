#include "rewrite.h"

#include <string.h>
#include <gio/gio.h>

typedef struct {
  const char *const *names;    /* Android appName aliases, NULL-terminated */
  const char *const *pkgs;     /* Android package names, NULL-terminated */
  const char *name;
  const char *const *desktops; /* "foo.desktop" candidates, NULL-terminated */
  const char *icon;
  const char *category;
} CatalogEntry;

static const char *gmail_names[] = { "Gmail", "Email", "Google Mail", NULL };
static const char *gmail_pkgs[] = { "com.google.android.gm", NULL };
static const char *gmail_desk[] = { "mailspring.desktop", "org.gnome.Geary.desktop", "thunderbird.desktop", NULL };

static const char *msg_names[] = { "Messages", "Messaging", "Google Messages", NULL };
static const char *msg_pkgs[] = { "com.google.android.apps.messaging", "com.google.android.apps.tachyon", NULL };

static const char *phone_names[] = { "Phone", "Dialer", "Call Management", "Phone Services", NULL };
static const char *phone_pkgs[] = { "com.google.android.dialer", "com.android.phone", "com.google.android.apps.dialer", NULL };

static const char *discord_names[] = { "Discord", NULL };
static const char *discord_pkgs[] = { "com.discord", NULL };
static const char *discord_desk[] = { "discord.desktop", "com.discordapp.Discord.desktop", NULL };

static const char *telegram_names[] = { "Telegram", "Telegram X", NULL };
static const char *telegram_pkgs[] = { "org.telegram.messenger", "org.thunderdog.challegram", NULL };
static const char *telegram_desk[] = { "org.telegram.desktop.desktop", "telegramdesktop.desktop", NULL };

static const char *signal_names[] = { "Signal", NULL };
static const char *signal_pkgs[] = { "org.thoughtcrime.securesms", NULL };
static const char *signal_desk[] = { "org.signal.Signal.desktop", "signal-desktop.desktop", NULL };

static const char *whatsapp_names[] = { "WhatsApp", "WhatsApp Business", NULL };
static const char *whatsapp_pkgs[] = { "com.whatsapp", "com.whatsapp.w4b", NULL };

static const char *slack_names[] = { "Slack", NULL };
static const char *slack_pkgs[] = { "com.Slack", "com.slack", NULL };
static const char *slack_desk[] = { "slack.desktop", "com.slack.Slack.desktop", NULL };

static const char *yt_names[] = { "YouTube", "YouTube Music", NULL };
static const char *yt_pkgs[] = { "com.google.android.youtube", "com.google.android.apps.youtube.music", NULL };

static const char *chrome_names[] = { "Chrome", "Google Chrome", "Chromium", NULL };
static const char *chrome_pkgs[] = { "com.android.chrome", "com.chrome.beta", "com.chrome.dev", NULL };
static const char *chrome_desk[] = { "google-chrome.desktop", "com.google.Chrome.desktop", "chromium.desktop", NULL };

static const char *maps_names[] = { "Maps", "Google Maps", NULL };
static const char *maps_pkgs[] = { "com.google.android.apps.maps", NULL };

static const char *cal_names[] = { "Calendar", "Google Calendar", NULL };
static const char *cal_pkgs[] = { "com.google.android.calendar", NULL };
static const char *cal_desk[] = { "org.gnome.Calendar.desktop", NULL };

static const char *photos_names[] = { "Photos", "Google Photos", NULL };
static const char *photos_pkgs[] = { "com.google.android.apps.photos", NULL };

static const char *spotify_names[] = { "Spotify", NULL };
static const char *spotify_pkgs[] = { "com.spotify.music", NULL };
static const char *spotify_desk[] = { "spotify.desktop", "com.spotify.Client.desktop", NULL };

static const char *namida_names[] = { "Namida", NULL };
static const char *namida_pkgs[] = { "com.namidaco.namida", NULL };

static const char *ig_names[] = { "Instagram", NULL };
static const char *ig_pkgs[] = { "com.instagram.android", NULL };

static const char *x_names[] = { "X", "Twitter", NULL };
static const char *x_pkgs[] = { "com.twitter.android", NULL };

static const char *reddit_names[] = { "Reddit", NULL };
static const char *reddit_pkgs[] = { "com.reddit.frontpage", NULL };

static const char *outlook_names[] = { "Outlook", "Microsoft Outlook", NULL };
static const char *outlook_pkgs[] = { "com.microsoft.office.outlook", NULL };
static const char *outlook_desk[] = { "mailspring.desktop", NULL };

static const char *teams_names[] = { "Teams", "Microsoft Teams", NULL };
static const char *teams_pkgs[] = { "com.microsoft.teams", "com.microsoft.skype.teams", NULL };

static const char *clock_names[] = { "Clock", "Alarm Clock", "Clock (Pixel)", NULL };
static const char *clock_pkgs[] = { "com.google.android.deskclock", NULL };
static const char *clock_desk[] = { "org.gnome.clocks.desktop", NULL };

static const char *sysui_names[] = { "System UI", "Android System", "System", NULL };
static const char *sysui_pkgs[] = { "com.android.systemui", "android", NULL };

static const char *gms_names[] = { "Google Play services", "Google", NULL };
static const char *gms_pkgs[] = { "com.google.android.gms", "com.google.android.gsf", NULL };

static const char *play_names[] = { "Google Play Store", "Play Store", NULL };
static const char *play_pkgs[] = { "com.android.vending", NULL };

static const char *auth_names[] = { "Authenticator", "Google Authenticator", NULL };
static const char *auth_pkgs[] = { "com.google.android.apps.authenticator2", NULL };

static const char *proton_names[] = { "Proton Mail", "ProtonMail", NULL };
static const char *proton_pkgs[] = { "ch.protonmail.android", NULL };

static const char *fb_names[] = { "Facebook", "Messenger", "Facebook Messenger", NULL };
static const char *fb_pkgs[] = { "com.facebook.katana", "com.facebook.orca", NULL };

static const char *tiktok_names[] = { "TikTok", NULL };
static const char *tiktok_pkgs[] = { "com.zhiliaoapp.musically", NULL };

static const char *snap_names[] = { "Snapchat", NULL };
static const char *snap_pkgs[] = { "com.snapchat.android", NULL };

static const CatalogEntry CATALOG[] = {
  { gmail_names, gmail_pkgs, "Gmail", gmail_desk, "mailspring", "email.arrived" },
  { msg_names, msg_pkgs, "Messages", NULL, "phone", "im.received" },
  { phone_names, phone_pkgs, "Phone", NULL, "phone", "call.incoming" },
  { discord_names, discord_pkgs, "Discord", discord_desk, "discord", "im.received" },
  { telegram_names, telegram_pkgs, "Telegram", telegram_desk, "telegram", "im.received" },
  { signal_names, signal_pkgs, "Signal", signal_desk, "signal-desktop", "im.received" },
  { whatsapp_names, whatsapp_pkgs, "WhatsApp", NULL, "whatsapp", "im.received" },
  { slack_names, slack_pkgs, "Slack", slack_desk, "slack", "im.received" },
  { yt_names, yt_pkgs, "YouTube", NULL, "youtube", "video" },
  { chrome_names, chrome_pkgs, "Chrome", chrome_desk, "google-chrome", "web" },
  { maps_names, maps_pkgs, "Maps", NULL, "maps", "device" },
  { cal_names, cal_pkgs, "Calendar", cal_desk, "office-calendar", "appointment-reminder" },
  { photos_names, photos_pkgs, "Photos", NULL, "multimedia-photo-viewer", "device" },
  { spotify_names, spotify_pkgs, "Spotify", spotify_desk, "spotify", "device" },
  { namida_names, namida_pkgs, "Namida", NULL, "audio-x-generic", "device" },
  { ig_names, ig_pkgs, "Instagram", NULL, "instagram", "im.received" },
  { x_names, x_pkgs, "X", NULL, "twitter", "im.received" },
  { reddit_names, reddit_pkgs, "Reddit", NULL, "reddit", "im.received" },
  { outlook_names, outlook_pkgs, "Outlook", outlook_desk, "mailspring", "email.arrived" },
  { teams_names, teams_pkgs, "Teams", NULL, "teams", "im.received" },
  { clock_names, clock_pkgs, "Clock", clock_desk, "alarm", "device" },
  { sysui_names, sysui_pkgs, "Android System", NULL, "phone", "device" },
  { gms_names, gms_pkgs, "Google Play services", NULL, "google", "device" },
  { play_names, play_pkgs, "Play Store", NULL, "google-play", "device" },
  { auth_names, auth_pkgs, "Authenticator", NULL, "dialog-password", "device" },
  { proton_names, proton_pkgs, "Proton Mail", NULL, "mailspring", "email.arrived" },
  { fb_names, fb_pkgs, "Messenger", NULL, "facebook-messenger", "im.received" },
  { tiktok_names, tiktok_pkgs, "TikTok", NULL, "tiktok", "video" },
  { snap_names, snap_pkgs, "Snapchat", NULL, "snapchat", "im.received" },
};

static gboolean
strv_has_ci (const char *const *list, const char *value)
{
  if (value == NULL || *value == '\0' || list == NULL)
    return FALSE;
  for (gsize i = 0; list[i]; i++)
    {
      if (g_ascii_strcasecmp (list[i], value) == 0)
        return TRUE;
    }
  return FALSE;
}

char *
vn_first_desktop_id (const char *const *candidates)
{
  const char *dirs[] = { NULL, "/usr/share/applications", "/usr/local/share/applications", NULL };
  g_autofree char *local = NULL;

  if (candidates == NULL)
    return NULL;

  local = g_build_filename (g_get_user_data_dir (), "applications", NULL);
  dirs[0] = local;

  for (gsize i = 0; candidates[i]; i++)
    {
      const char *fn = candidates[i];
      for (gsize d = 0; dirs[d]; d++)
        {
          g_autofree char *path = g_build_filename (dirs[d], fn, NULL);
          if (g_file_test (path, G_FILE_TEST_EXISTS))
            {
              char *id = g_strdup (fn);
              char *dot = g_strrstr (id, ".desktop");
              if (dot)
                *dot = '\0';
              return id;
            }
        }
    }
  return NULL;
}

gboolean
catalog_lookup (const char *app_name, const char *pkg, RewriteResult *out)
{
  for (gsize i = 0; i < G_N_ELEMENTS (CATALOG); i++)
    {
      const CatalogEntry *e = &CATALOG[i];
      if (!strv_has_ci (e->names, app_name) && !strv_has_ci (e->pkgs, pkg))
        continue;

      out->mapped = TRUE;
      out->muted = FALSE;
      out->app_name = g_strdup (e->name);
      out->desktop_entry = vn_first_desktop_id (e->desktops);
      out->icon = e->icon ? g_strdup (e->icon) : NULL;
      out->category = e->category ? g_strdup (e->category) : NULL;
      return TRUE;
    }
  return FALSE;
}
