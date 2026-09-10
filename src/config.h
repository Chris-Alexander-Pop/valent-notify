#pragma once

#include "rewrite.h"

void notify_config_free (NotifyConfig *cfg);

/* Load ~/.config/valent-notify/config.json (or $VALENT_NOTIFY_CONFIG). */
gboolean notify_config_load (NotifyConfig *cfg, GError **error);

/* Reload when mtime changes. Returns TRUE if a new file was applied. */
gboolean notify_config_reload_if_changed (NotifyConfig *cfg);

const char *notify_config_path (void);
const char *notify_state_dir (void);
const char *notify_log_path (void);
const char *notify_seen_path (void);

void notify_log_event (const NotifyConfig *cfg, const char *app, const char *pkg,
                       const char *id, const char *summary, const char *body,
                       const RewriteResult *r);
