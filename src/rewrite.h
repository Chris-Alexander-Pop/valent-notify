#pragma once

#include <glib.h>

#define VALENT_NOTIFY_MUTED_APP "__valent-notify-muted__"
#define VALENT_NOTIFY_MUTED_DESKTOP "valent-notify-muted"

typedef struct {
  char *app;     /* exact or regex against Android appName */
  char *pkg;     /* exact or regex against packageName */
  char *summary; /* regex against notification title */
  char *body;    /* regex against body */
} MuteRule;

typedef struct {
  char *match; /* regex against appName (and aliases) */
  char *pkg;    /* regex against packageName */
  char *name;
  char *desktop_entry; /* without .desktop */
  char *icon;
  char *category;
} AppRule;

typedef struct {
  gboolean enabled;
  gboolean log;         /* log every notification (default true) */
  gboolean log_unknown; /* also append unmatched apps to unknown.jsonl */
  gboolean mute_media;  /* drop play/pause/next now-playing toasts (default true) */
  MuteRule *mutes;
  gsize n_mutes;
  AppRule *apps; /* user overrides, checked before catalog */
  gsize n_apps;
} NotifyConfig;

typedef struct {
  gboolean muted;
  gboolean mapped;
  char *app_name;
  char *desktop_entry;
  char *icon;
  char *category;
} RewriteResult;

void rewrite_result_free(RewriteResult *r);

gboolean vn_is_regex_pattern(const char *s);
gboolean vn_match_field(const char *pattern, const char *value);
char *vn_slugify(const char *name);

/* Catalog lookup (compiled-in). Fills result if matched. */
gboolean catalog_lookup(const char *app_name, const char *pkg, RewriteResult *out);

/* Resolve first existing *.desktop among NULL-terminated candidates. */
char *vn_first_desktop_id(const char *const *candidates);

RewriteResult rewrite_apply(const NotifyConfig *cfg, const char *app_name,
                            const char *pkg, const char *summary,
                            const char *body);

/* FDO Notify actions are [id, label, id, label, ...]. True for play/pause/next. */
gboolean vn_fdo_actions_are_media(GVariant *actions);
