#include "rewrite.h"

#include <string.h>
#include <gio/gio.h>

void
rewrite_result_free (RewriteResult *r)
{
  if (r == NULL)
    return;
  g_free (r->app_name);
  g_free (r->desktop_entry);
  g_free (r->icon);
  g_free (r->category);
  memset (r, 0, sizeof (*r));
}

gboolean
vn_is_regex_pattern (const char *s)
{
  if (s == NULL || *s == '\0')
    return FALSE;
  return strpbrk (s, ".*+?[](){}|^$\\") != NULL;
}

gboolean
vn_match_field (const char *pattern, const char *value)
{
  if (pattern == NULL || *pattern == '\0')
    return FALSE;
  if (value == NULL)
    value = "";

  if (!vn_is_regex_pattern (pattern))
    return g_ascii_strcasecmp (pattern, value) == 0;

  return g_regex_match_simple (pattern, value, G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0);
}

char *
vn_slugify (const char *name)
{
  GString *s;
  gboolean hyphen = FALSE;

  if (name == NULL || *name == '\0')
    return g_strdup ("app");

  s = g_string_new (NULL);
  for (const char *p = name; *p; p++)
    {
      guchar c = (guchar)*p;
      if (g_ascii_isalnum (c))
        {
          g_string_append_c (s, g_ascii_tolower (c));
          hyphen = FALSE;
        }
      else if (!hyphen && s->len > 0)
        {
          g_string_append_c (s, '-');
          hyphen = TRUE;
        }
    }
  if (s->len == 0)
    {
      g_string_free (s, TRUE);
      return g_strdup ("app");
    }
  if (s->str[s->len - 1] == '-')
    g_string_truncate (s, s->len - 1);
  if (s->len > 80)
    g_string_truncate (s, 80);
  return g_string_free (s, FALSE);
}

static gboolean
mute_matches (const MuteRule *m, const char *app, const char *pkg,
              const char *summary, const char *body)
{
  gboolean any = FALSE;

  if (m->app && *m->app)
    {
      any = TRUE;
      if (!vn_match_field (m->app, app))
        return FALSE;
    }
  if (m->pkg && *m->pkg)
    {
      any = TRUE;
      if (!vn_match_field (m->pkg, pkg))
        return FALSE;
    }
  if (m->summary && *m->summary)
    {
      any = TRUE;
      if (!vn_match_field (m->summary, summary))
        return FALSE;
    }
  if (m->body && *m->body)
    {
      any = TRUE;
      if (!vn_match_field (m->body, body))
        return FALSE;
    }
  return any;
}

static gboolean
app_rule_matches (const AppRule *r, const char *app, const char *pkg)
{
  gboolean any = FALSE;

  if (r->match && *r->match)
    {
      any = TRUE;
      if (!vn_match_field (r->match, app))
        return FALSE;
    }
  if (r->pkg && *r->pkg)
    {
      any = TRUE;
      if (!vn_match_field (r->pkg, pkg))
        return FALSE;
    }
  return any;
}

static void
result_from_app_rule (const AppRule *r, const char *fallback_name, RewriteResult *out)
{
  out->mapped = TRUE;
  out->muted = FALSE;
  out->app_name = g_strdup ((r->name && *r->name) ? r->name : (fallback_name ? fallback_name : "App"));
  out->desktop_entry = r->desktop_entry ? g_strdup (r->desktop_entry) : NULL;
  out->icon = r->icon ? g_strdup (r->icon) : NULL;
  out->category = r->category ? g_strdup (r->category) : NULL;
}

RewriteResult
rewrite_apply (const NotifyConfig *cfg, const char *app_name, const char *pkg,
               const char *summary, const char *body)
{
  RewriteResult out = { 0 };
  const char *app = app_name ? app_name : "";
  const char *package = pkg ? pkg : "";

  if (cfg == NULL || !cfg->enabled)
    {
      out.app_name = g_strdup (app[0] ? app : "Valent");
      return out;
    }

  for (gsize i = 0; i < cfg->n_mutes; i++)
    {
      if (mute_matches (&cfg->mutes[i], app, package, summary, body))
        {
          out.muted = TRUE;
          out.app_name = g_strdup (VALENT_NOTIFY_MUTED_APP);
          return out;
        }
    }

  for (gsize i = 0; i < cfg->n_apps; i++)
    {
      if (app_rule_matches (&cfg->apps[i], app, package))
        {
          result_from_app_rule (&cfg->apps[i], app, &out);
          return out;
        }
    }

  if (catalog_lookup (app, package, &out))
    return out;

  out.app_name = g_strdup (app[0] ? app : "Valent");
  return out;
}

static gboolean
token_is_media_control (const char *s)
{
  const char *base;

  if (s == NULL || *s == '\0')
    return FALSE;
  base = strrchr (s, '.');
  if (base && base[1])
    s = base + 1;
  return g_ascii_strcasecmp (s, "pause") == 0
      || g_ascii_strcasecmp (s, "play") == 0
      || g_ascii_strcasecmp (s, "next") == 0
      || g_ascii_strcasecmp (s, "previous") == 0
      || g_ascii_strcasecmp (s, "prev") == 0
      || g_ascii_strcasecmp (s, "skip") == 0
      || g_ascii_strcasecmp (s, "stop") == 0
      || g_ascii_strcasecmp (s, "rewind") == 0
      || g_ascii_strcasecmp (s, "forward") == 0;
}

gboolean
vn_fdo_actions_are_media (GVariant *actions)
{
  GVariantIter iter;
  const char *s;

  if (actions == NULL || !g_variant_is_of_type (actions, G_VARIANT_TYPE_STRING_ARRAY))
    return FALSE;

  g_variant_iter_init (&iter, actions);
  while (g_variant_iter_next (&iter, "&s", &s))
    {
      if (token_is_media_control (s))
        return TRUE;
    }
  return FALSE;
}
