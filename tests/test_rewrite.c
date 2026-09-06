#include "rewrite.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

static int fails = 0;

static void
expect (gboolean cond, const char *msg)
{
  if (!cond)
    {
      fprintf (stderr, "FAIL: %s\n", msg);
      fails++;
    }
}

static void
test_slugify (void)
{
  g_autofree char *a = vn_slugify ("Google Play services");
  g_autofree char *b = vn_slugify ("***");
  g_autofree char *c = vn_slugify ("");
  expect (g_strcmp0 (a, "google-play-services") == 0, "slugify spaces");
  expect (g_strcmp0 (b, "app") == 0, "slugify symbols");
  expect (g_strcmp0 (c, "app") == 0, "slugify empty");
}

static void
test_match (void)
{
  expect (vn_match_field ("YouTube", "YouTube"), "exact");
  expect (vn_match_field ("youtube", "YouTube"), "exact ci");
  expect (!vn_match_field ("YouTube", "YouTube Music"), "exact does not substring");
  expect (vn_match_field ("YouTube.*", "YouTube Music"), "regex");
  expect (vn_match_field ("(?i)charging", "Battery is charging"), "summary regex");
}

static void
test_catalog (void)
{
  RewriteResult r = { 0 };
  expect (catalog_lookup ("Discord", NULL, &r), "discord name");
  expect (g_strcmp0 (r.app_name, "Discord") == 0, "discord mapped name");
  expect (g_strcmp0 (r.category, "im.received") == 0, "discord category");
  rewrite_result_free (&r);

  expect (catalog_lookup ("nope", "com.google.android.gm", &r), "gmail pkg");
  expect (g_strcmp0 (r.app_name, "Gmail") == 0, "gmail name");
  rewrite_result_free (&r);

  expect (catalog_lookup ("Namida", "com.namidaco.namida", &r), "namida pkg");
  expect (g_strcmp0 (r.app_name, "Namida") == 0, "namida mapped name");
  rewrite_result_free (&r);

  expect (!catalog_lookup ("Totally Unknown App", "com.example.foo", &r), "unknown");
}

static void
test_mute_and_override (void)
{
  NotifyConfig cfg = { 0 };
  RewriteResult r;
  cfg.enabled = TRUE;
  cfg.n_mutes = 2;
  cfg.mutes = g_new0 (MuteRule, 2);
  cfg.mutes[0].app = g_strdup ("YouTube");
  cfg.mutes[1].summary = g_strdup ("(?i)is charging");
  cfg.n_apps = 1;
  cfg.apps = g_new0 (AppRule, 1);
  cfg.apps[0].match = g_strdup ("CoolApp");
  cfg.apps[0].name = g_strdup ("CoolApp");
  cfg.apps[0].icon = g_strdup ("utilities-system-monitor");
  cfg.apps[0].category = g_strdup ("device");

  r = rewrite_apply (&cfg, "YouTube", NULL, "New video", "hello");
  expect (r.muted, "mute youtube");
  expect (g_strcmp0 (r.app_name, VALENT_NOTIFY_MUTED_APP) == 0, "muted app name");
  rewrite_result_free (&r);

  r = rewrite_apply (&cfg, "Android System", NULL, "Battery is charging", "");
  expect (r.muted, "mute charging summary");
  rewrite_result_free (&r);

  r = rewrite_apply (&cfg, "CoolApp", "com.example.coolapp", "Logged", "lunch");
  expect (!r.muted, "override not muted");
  expect (r.mapped, "override mapped");
  expect (g_strcmp0 (r.app_name, "CoolApp") == 0, "override name");
  expect (g_strcmp0 (r.icon, "utilities-system-monitor") == 0, "override icon");
  rewrite_result_free (&r);

  r = rewrite_apply (&cfg, "Discord", "com.discord", "Alice", "hi");
  expect (!r.muted, "discord not muted");
  expect (r.mapped, "discord catalog");
  expect (g_strcmp0 (r.app_name, "Discord") == 0, "discord catalog name");
  rewrite_result_free (&r);

  r = rewrite_apply (&cfg, "Namida", "com.namidaco.namida", "Some Track", "Artist");
  expect (!r.muted, "namida not muted until listed");
  expect (r.mapped, "namida catalog");
  expect (g_strcmp0 (r.app_name, "Namida") == 0, "namida catalog name");
  rewrite_result_free (&r);

  {
    MuteRule extra = { 0 };
    extra.app = g_strdup ("Namida");
    cfg.mutes = g_renew (MuteRule, cfg.mutes, 3);
    cfg.mutes[2] = extra;
    cfg.n_mutes = 3;
    r = rewrite_apply (&cfg, "Namida", "com.namidaco.namida", "Some Track", "Artist");
    expect (r.muted, "mute namida now-playing");
    rewrite_result_free (&r);
  }

  r = rewrite_apply (&cfg, "Weird App", NULL, "Hello", "world");
  expect (!r.muted, "unknown not muted");
  expect (!r.mapped, "unknown not mapped");
  expect (g_strcmp0 (r.app_name, "Weird App") == 0, "unknown keeps android name");
  rewrite_result_free (&r);

  notify_config_free (&cfg);
}

static void
test_config_json (void)
{
  g_autofree char *dir = g_dir_make_tmp ("valent-notify-test-XXXXXX", NULL);
  g_autofree char *path = g_build_filename (dir, "config.json", NULL);
  const char *json =
      "{ \"enabled\": true, \"mute\": [\"YouTube\", {\"pkg\": \"com.android.systemui\"}],"
      " \"apps\": [{\"match\": \"FooApp\", \"name\": \"Foo\", \"icon\": \"foo\", \"category\": \"im.received\"}] }";
  NotifyConfig cfg = { 0 };
  g_autoptr (GError) err = NULL;
  RewriteResult r;

  expect (g_file_set_contents (path, json, -1, NULL), "write temp config");
  g_setenv ("VALENT_NOTIFY_CONFIG", path, TRUE);
  expect (notify_config_load (&cfg, &err), "load json");
  expect (cfg.n_mutes == 2, "two mutes");
  expect (cfg.n_apps == 1, "one app");

  r = rewrite_apply (&cfg, "YouTube", NULL, "x", "");
  expect (r.muted, "json mute app");
  rewrite_result_free (&r);

  r = rewrite_apply (&cfg, "System UI", "com.android.systemui", "x", "");
  expect (r.muted, "json mute pkg");
  rewrite_result_free (&r);

  r = rewrite_apply (&cfg, "FooApp", NULL, "x", "");
  expect (g_strcmp0 (r.app_name, "Foo") == 0, "json app rewrite");
  rewrite_result_free (&r);

  notify_config_free (&cfg);
  g_unsetenv ("VALENT_NOTIFY_CONFIG");
  g_unlink (path);
  g_rmdir (dir);
}

static void
test_log_event (void)
{
  g_autofree char *dir = g_dir_make_tmp ("valent-notify-log-XXXXXX", NULL);
  g_autofree char *log_path = NULL;
  g_autofree char *contents = NULL;
  NotifyConfig cfg = { 0 };
  RewriteResult r = { 0 };

  g_setenv ("XDG_STATE_HOME", dir, TRUE);
  cfg.enabled = TRUE;
  cfg.log = TRUE;
  r.muted = TRUE;
  r.app_name = g_strdup (VALENT_NOTIFY_MUTED_APP);

  notify_log_event (&cfg, "YouTube", "com.google.android.youtube", "New video", "watch this", &r);
  rewrite_result_free (&r);

  log_path = g_build_filename (dir, "valent-notify", "notifications.jsonl", NULL);
  expect (g_file_get_contents (log_path, &contents, NULL, NULL), "log file written");
  expect (contents && strstr (contents, "\"app\":\"YouTube\"") != NULL, "log has app");
  expect (contents && strstr (contents, "\"muted\":true") != NULL, "log has muted");
  expect (contents && strstr (contents, "\"summary\":\"New video\"") != NULL, "log has summary");

  {
    g_autofree char *statedir = g_build_filename (dir, "valent-notify", NULL);
    g_unlink (log_path);
    g_rmdir (statedir);
  }
  g_rmdir (dir);
}

int
main (void)
{
  test_slugify ();
  test_match ();
  test_catalog ();
  test_mute_and_override ();
  test_log_event ();
  test_config_json ();
  if (fails)
    {
      fprintf (stderr, "%d failure(s)\n", fails);
      return 1;
    }
  printf ("ok\n");
  return 0;
}
