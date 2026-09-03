#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>
#include <json-glib/json-glib.h>

static char *g_config_path = NULL;
static gint64 g_loaded_mtime = 0;

const char *
notify_config_path (void)
{
  const char *env = g_getenv ("VALENT_NOTIFY_CONFIG");
  if (env && *env)
    return env;
  if (g_config_path == NULL)
    g_config_path = g_build_filename (g_get_user_config_dir (), "valent-notify", "config.json", NULL);
  return g_config_path;
}

const char *
notify_state_dir (void)
{
  static char *dir = NULL;
  if (dir == NULL)
    {
      dir = g_build_filename (g_get_user_state_dir (), "valent-notify", NULL);
      g_mkdir_with_parents (dir, 0700);
    }
  return dir;
}

const char *
notify_log_path (void)
{
  static char *path = NULL;
  if (path == NULL)
    path = g_build_filename (notify_state_dir (), "notifications.jsonl", NULL);
  return path;
}

static char *
truncate_field (const char *s, gsize max)
{
  if (s == NULL)
    return g_strdup ("");
  if (g_utf8_strlen (s, -1) <= (glong)max)
    return g_strdup (s);
  g_autofree char *cut = g_utf8_substring (s, 0, (glong)max);
  return g_strconcat (cut, "…", NULL);
}

void
notify_log_event (const NotifyConfig *cfg, const char *app, const char *pkg,
                   const char *summary, const char *body, const RewriteResult *r)
{
  g_autoptr (JsonBuilder) builder = NULL;
  g_autoptr (JsonGenerator) gen = NULL;
  g_autoptr (JsonNode) root = NULL;
  g_autoptr (GDateTime) now = NULL;
  g_autofree char *iso = NULL;
  g_autofree char *app_s = NULL;
  g_autofree char *pkg_s = NULL;
  g_autofree char *sum_s = NULL;
  g_autofree char *body_s = NULL;
  g_autofree char *json = NULL;
  FILE *f;

  if (cfg != NULL && !cfg->log)
    return;

  now = g_date_time_new_now_local ();
  iso = g_date_time_format_iso8601 (now);
  app_s = truncate_field (app, 120);
  pkg_s = truncate_field (pkg, 120);
  sum_s = truncate_field (summary, 200);
  body_s = truncate_field (body, 400);

  builder = json_builder_new ();
  json_builder_begin_object (builder);
  json_builder_set_member_name (builder, "ts");
  json_builder_add_string_value (builder, iso ? iso : "");
  json_builder_set_member_name (builder, "app");
  json_builder_add_string_value (builder, app_s);
  json_builder_set_member_name (builder, "pkg");
  json_builder_add_string_value (builder, pkg_s);
  json_builder_set_member_name (builder, "summary");
  json_builder_add_string_value (builder, sum_s);
  json_builder_set_member_name (builder, "body");
  json_builder_add_string_value (builder, body_s);
  json_builder_set_member_name (builder, "muted");
  json_builder_add_boolean_value (builder, r && r->muted);
  json_builder_set_member_name (builder, "mapped");
  json_builder_add_boolean_value (builder, r && r->mapped);
  json_builder_set_member_name (builder, "shown_as");
  json_builder_add_string_value (builder, r && r->app_name ? r->app_name : "");
  json_builder_set_member_name (builder, "category");
  json_builder_add_string_value (builder, r && r->category ? r->category : "");
  json_builder_end_object (builder);

  root = json_builder_get_root (builder);
  gen = json_generator_new ();
  json_generator_set_root (gen, root);
  json = json_generator_to_data (gen, NULL);

  f = fopen (notify_log_path (), "a");
  if (f)
    {
      fputs (json, f);
      fputc ('\n', f);
      fclose (f);
    }

  if (cfg && cfg->log_unknown && r && !r->muted && !r->mapped)
    {
      g_autofree char *upath = g_build_filename (notify_state_dir (), "unknown.jsonl", NULL);
      FILE *uf = fopen (upath, "a");
      if (uf)
        {
          fputs (json, uf);
          fputc ('\n', uf);
          fclose (uf);
        }
    }
}

void
notify_config_free (NotifyConfig *cfg)
{
  if (cfg == NULL)
    return;
  for (gsize i = 0; i < cfg->n_mutes; i++)
    {
      g_free (cfg->mutes[i].app);
      g_free (cfg->mutes[i].pkg);
      g_free (cfg->mutes[i].summary);
      g_free (cfg->mutes[i].body);
    }
  g_free (cfg->mutes);
  for (gsize i = 0; i < cfg->n_apps; i++)
    {
      g_free (cfg->apps[i].match);
      g_free (cfg->apps[i].pkg);
      g_free (cfg->apps[i].name);
      g_free (cfg->apps[i].desktop_entry);
      g_free (cfg->apps[i].icon);
      g_free (cfg->apps[i].category);
    }
  g_free (cfg->apps);
  memset (cfg, 0, sizeof (*cfg));
}

static char *
node_str (JsonNode *n)
{
  if (n == NULL || !JSON_NODE_HOLDS_VALUE (n))
    return NULL;
  if (json_node_get_value_type (n) != G_TYPE_STRING)
    return NULL;
  return g_strdup (json_node_get_string (n));
}

static gboolean
obj_bool (JsonObject *o, const char *key, gboolean def)
{
  if (!json_object_has_member (o, key))
    return def;
  return json_object_get_boolean_member (o, key);
}

static void
parse_mute_node (JsonNode *n, MuteRule *out)
{
  memset (out, 0, sizeof (*out));
  if (JSON_NODE_HOLDS_VALUE (n) && json_node_get_value_type (n) == G_TYPE_STRING)
    {
      out->app = g_strdup (json_node_get_string (n));
      return;
    }
  if (!JSON_NODE_HOLDS_OBJECT (n))
    return;
  JsonObject *o = json_node_get_object (n);
  out->app = node_str (json_object_get_member (o, "app"));
  out->pkg = node_str (json_object_get_member (o, "pkg"));
  out->summary = node_str (json_object_get_member (o, "summary"));
  out->body = node_str (json_object_get_member (o, "body"));
}

static void
parse_app_node (JsonNode *n, AppRule *out)
{
  memset (out, 0, sizeof (*out));
  if (!JSON_NODE_HOLDS_OBJECT (n))
    return;
  JsonObject *o = json_node_get_object (n);
  out->match = node_str (json_object_get_member (o, "match"));
  out->pkg = node_str (json_object_get_member (o, "pkg"));
  out->name = node_str (json_object_get_member (o, "name"));
  out->desktop_entry = node_str (json_object_get_member (o, "desktop_entry"));
  out->icon = node_str (json_object_get_member (o, "icon"));
  out->category = node_str (json_object_get_member (o, "category"));
}

gboolean
notify_config_load (NotifyConfig *cfg, GError **error)
{
  const char *path = notify_config_path ();
  g_autoptr (JsonParser) parser = json_parser_new ();
  JsonNode *root;
  JsonObject *obj;
  GStatBuf st;

  notify_config_free (cfg);
  cfg->enabled = TRUE;
  cfg->log = TRUE;
  cfg->log_unknown = TRUE;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      g_loaded_mtime = 0;
      return TRUE;
    }

  if (!json_parser_load_from_file (parser, path, error))
    return FALSE;

  if (g_stat (path, &st) == 0)
    g_loaded_mtime = (gint64)st.st_mtime;

  root = json_parser_get_root (parser);
  if (root == NULL || !JSON_NODE_HOLDS_OBJECT (root))
    return TRUE;

  obj = json_node_get_object (root);
  cfg->enabled = obj_bool (obj, "enabled", TRUE);
  cfg->log = obj_bool (obj, "log", TRUE);
  cfg->log_unknown = obj_bool (obj, "log_unknown", TRUE);

  if (json_object_has_member (obj, "mute") &&
      JSON_NODE_HOLDS_ARRAY (json_object_get_member (obj, "mute")))
    {
      JsonArray *arr = json_object_get_array_member (obj, "mute");
      guint n = json_array_get_length (arr);
      cfg->mutes = g_new0 (MuteRule, n);
      cfg->n_mutes = n;
      for (guint i = 0; i < n; i++)
        parse_mute_node (json_array_get_element (arr, i), &cfg->mutes[i]);
    }

  if (json_object_has_member (obj, "apps") &&
      JSON_NODE_HOLDS_ARRAY (json_object_get_member (obj, "apps")))
    {
      JsonArray *arr = json_object_get_array_member (obj, "apps");
      guint n = json_array_get_length (arr);
      cfg->apps = g_new0 (AppRule, n);
      cfg->n_apps = n;
      for (guint i = 0; i < n; i++)
        parse_app_node (json_array_get_element (arr, i), &cfg->apps[i]);
    }

  return TRUE;
}

gboolean
notify_config_reload_if_changed (NotifyConfig *cfg)
{
  const char *path = notify_config_path ();
  GStatBuf st;
  g_autoptr (GError) err = NULL;

  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    {
      if (g_loaded_mtime == 0)
        return FALSE;
      notify_config_free (cfg);
      cfg->enabled = TRUE;
      cfg->log = TRUE;
      cfg->log_unknown = TRUE;
      g_loaded_mtime = 0;
      return TRUE;
    }

  if (g_stat (path, &st) != 0)
    return FALSE;
  if ((gint64)st.st_mtime == g_loaded_mtime)
    return FALSE;

  if (!notify_config_load (cfg, &err))
    {
      g_printerr ("valent-notify: reload failed: %s\n", err ? err->message : "unknown");
      return FALSE;
    }
  return TRUE;
}
