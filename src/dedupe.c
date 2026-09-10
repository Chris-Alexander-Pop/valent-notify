#include "dedupe.h"

#include <stdio.h>
#include <string.h>
#include <glib/gstdio.h>

#define VN_DEDUPE_MAX 8000

struct DedupeState
{
  GHashTable *by_fp; /* fp -> gint64 * now_us */
  gint64 ttl_us;
  char *persist_path;
  guint inserts;
};

static gint64 *
ts_dup (gint64 ts)
{
  gint64 *p = g_new (gint64, 1);
  *p = ts;
  return p;
}

static void
store (DedupeState *s, const char *fp, gint64 ts)
{
  g_hash_table_insert (s->by_fp, g_strdup (fp), ts_dup (ts));
}

static void persist_rewrite (DedupeState *s);

static gboolean
expired (const DedupeState *s, gint64 ts, gint64 now)
{
  if (s->ttl_us <= 0)
    return FALSE;
  return now > ts && (now - ts) > s->ttl_us;
}

static void
drop_oldest (DedupeState *s)
{
  GHashTableIter iter;
  gpointer k, v;
  gpointer best_k = NULL;
  gint64 best_ts = G_MAXINT64;

  g_hash_table_iter_init (&iter, s->by_fp);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      gint64 ts = *(gint64 *)v;
      if (ts < best_ts)
        {
          best_ts = ts;
          best_k = k;
        }
    }
  if (best_k)
    g_hash_table_remove (s->by_fp, best_k);
}

static void
compact_if_needed (DedupeState *s, gint64 now)
{
  GHashTableIter iter;
  gpointer k, v;

  if (g_hash_table_size (s->by_fp) <= VN_DEDUPE_MAX && s->inserts < 200)
    return;

  s->inserts = 0;
  g_hash_table_iter_init (&iter, s->by_fp);
  while (g_hash_table_iter_next (&iter, &k, &v))
    {
      if (expired (s, *(gint64 *)v, now))
        g_hash_table_iter_remove (&iter);
    }

  while (g_hash_table_size (s->by_fp) > VN_DEDUPE_MAX)
    drop_oldest (s);

  persist_rewrite (s);
}

static void
persist_append (DedupeState *s, const char *fp, gint64 ts)
{
  FILE *f;

  if (s->persist_path == NULL)
    return;
  f = fopen (s->persist_path, "a");
  if (f == NULL)
    return;
  fprintf (f, "%lld\t%s\n", (long long)ts, fp);
  fclose (f);
}

static void
persist_rewrite (DedupeState *s)
{
  g_autofree char *tmp = NULL;
  FILE *f;
  GHashTableIter iter;
  gpointer k, v;

  if (s->persist_path == NULL)
    return;
  tmp = g_strconcat (s->persist_path, ".tmp", NULL);
  f = fopen (tmp, "w");
  if (f == NULL)
    return;
  g_hash_table_iter_init (&iter, s->by_fp);
  while (g_hash_table_iter_next (&iter, &k, &v))
    fprintf (f, "%lld\t%s\n", (long long)(*(gint64 *)v), (const char *)k);
  fclose (f);
  g_rename (tmp, s->persist_path);
}

static void
persist_load (DedupeState *s, gint64 now)
{
  g_autofree char *contents = NULL;
  g_auto (GStrv) lines = NULL;
  gboolean dirty = FALSE;

  if (s->persist_path == NULL || !g_file_test (s->persist_path, G_FILE_TEST_EXISTS))
    return;
  if (!g_file_get_contents (s->persist_path, &contents, NULL, NULL))
    return;

  lines = g_strsplit (contents, "\n", -1);
  for (char **l = lines; l && *l; l++)
    {
      char *tab;
      gint64 ts;
      const char *fp;

      if (**l == '\0')
        continue;
      tab = strchr (*l, '\t');
      if (tab == NULL)
        continue;
      *tab = '\0';
      ts = g_ascii_strtoll (*l, NULL, 10);
      fp = tab + 1;
      if (*fp == '\0')
        continue;
      if (expired (s, ts, now))
        {
          dirty = TRUE;
          continue;
        }
      store (s, fp, ts);
    }

  if (dirty)
    persist_rewrite (s);
}

char *
vn_content_fp (const char *app, const char *pkg, const char *summary, const char *body)
{
  g_autofree char *blob = NULL;

  (void)pkg; /* pkg is often missing on the first packet; app+title+body is enough */
  blob = g_strconcat (app ? app : "", "\n", summary ? summary : "", "\n", body ? body : "", NULL);
  return g_compute_checksum_for_string (G_CHECKSUM_SHA256, blob, -1);
}

DedupeState *
vn_dedupe_new (gint64 ttl_us, const char *persist_path)
{
  DedupeState *s = g_new0 (DedupeState, 1);
  gint64 now = g_get_real_time ();

  s->by_fp = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
  s->ttl_us = ttl_us;
  s->persist_path = persist_path && *persist_path ? g_strdup (persist_path) : NULL;
  persist_load (s, now);
  return s;
}

void
vn_dedupe_free (DedupeState *s)
{
  if (s == NULL)
    return;
  g_clear_pointer (&s->by_fp, g_hash_table_unref);
  g_free (s->persist_path);
  g_free (s);
}

void
vn_dedupe_set_ttl (DedupeState *s, gint64 ttl_us)
{
  if (s)
    s->ttl_us = ttl_us;
}

gboolean
vn_dedupe_should_drop (DedupeState *s, const char *app, const char *pkg, const char *summary,
                       const char *body, gint64 now_us)
{
  g_autofree char *fp = NULL;
  gint64 *prev;

  if (s == NULL)
    return FALSE;
  if (now_us <= 0)
    now_us = g_get_real_time ();

  fp = vn_content_fp (app, pkg, summary, body);
  prev = g_hash_table_lookup (s->by_fp, fp);
  if (prev != NULL && !expired (s, *prev, now_us))
    {
      *prev = now_us;
      return TRUE;
    }

  store (s, fp, now_us);
  s->inserts++;
  persist_append (s, fp, now_us);
  compact_if_needed (s, now_us);
  return FALSE;
}

void
vn_dedupe_ingest (DedupeState *s, const char *app, const char *pkg, const char *summary,
                   const char *body, gint64 ts_us)
{
  g_autofree char *fp = NULL;
  gint64 *prev;
  gint64 now;

  if (s == NULL)
    return;
  now = g_get_real_time ();
  if (ts_us <= 0)
    ts_us = now;
  if (expired (s, ts_us, now))
    return;

  fp = vn_content_fp (app, pkg, summary, body);
  prev = g_hash_table_lookup (s->by_fp, fp);
  if (prev != NULL)
    {
      if (ts_us > *prev)
        *prev = ts_us;
      return;
    }
  store (s, fp, ts_us);
}

void
vn_dedupe_flush (DedupeState *s)
{
  if (s)
    persist_rewrite (s);
}
