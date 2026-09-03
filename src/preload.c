#define _GNU_SOURCE

#include "config.h"
#include "rewrite.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>

typedef gboolean (*PacketGetStringFn) (gpointer packet, const char *key, const char **value);
typedef GNotification *(*NotifNewFn) (const char *title);
typedef void (*NotifSetIconFn) (GNotification *notification, GIcon *icon);
typedef void (*SendNotifFn) (GApplication *app, const gchar *id, GNotification *notification);

typedef struct {
  char *app_name;
  char *pkg;
  GIcon *icon;
} NotifCtx;

static PacketGetStringFn real_packet_get_string;
static NotifNewFn real_notif_new;
static NotifSetIconFn real_notif_set_icon;
static SendNotifFn real_send_notif;
static guint g_filter_id = 0;

static GMutex g_mu;
static NotifyConfig g_cfg;
static gboolean g_cfg_ready = FALSE;
static GHashTable *g_by_notif;
static NotifCtx *g_inflight;
static char *g_pending_app;
static char *g_pending_pkg;

static void
ensure_reals (void)
{
  if (real_packet_get_string == NULL)
    real_packet_get_string = (PacketGetStringFn)dlsym (RTLD_NEXT, "valent_packet_get_string");
  if (real_notif_new == NULL)
    real_notif_new = (NotifNewFn)dlsym (RTLD_NEXT, "g_notification_new");
  if (real_notif_set_icon == NULL)
    real_notif_set_icon = (NotifSetIconFn)dlsym (RTLD_NEXT, "g_notification_set_icon");
  if (real_send_notif == NULL)
    real_send_notif = (SendNotifFn)dlsym (RTLD_NEXT, "g_application_send_notification");
}

static void
ensure_config_locked (void)
{
  if (g_cfg_ready)
    {
      notify_config_reload_if_changed (&g_cfg);
      return;
    }
  g_autoptr (GError) err = NULL;
  if (!notify_config_load (&g_cfg, &err))
    g_printerr ("valent-notify: %s\n", err ? err->message : "config load failed");
  g_cfg_ready = TRUE;
}

static void
ctx_free (gpointer data)
{
  NotifCtx *c = data;
  if (c == NULL)
    return;
  g_free (c->app_name);
  g_free (c->pkg);
  g_clear_object (&c->icon);
  g_free (c);
}

static GHashTable *
table (void)
{
  if (g_by_notif == NULL)
    g_by_notif = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, ctx_free);
  return g_by_notif;
}

static char *
dump_icon (GIcon *icon)
{
  if (icon == NULL)
    return NULL;

  if (G_IS_FILE_ICON (icon))
    {
      GFile *file = g_file_icon_get_file (G_FILE_ICON (icon));
      return g_file_get_path (file);
    }

  if (G_IS_THEMED_ICON (icon))
    {
      const char *const *names = g_themed_icon_get_names (G_THEMED_ICON (icon));
      if (names && names[0])
        return g_strdup (names[0]);
      return NULL;
    }

  if (!G_IS_BYTES_ICON (icon))
    return NULL;

  GBytes *bytes = g_bytes_icon_get_bytes (G_BYTES_ICON (icon));
  gsize n = 0;
  const guint8 *data = g_bytes_get_data (bytes, &n);
  if (data == NULL || n == 0)
    return NULL;

  g_autofree char *dir = g_build_filename (g_get_user_cache_dir (), "valent-notify", "icons", NULL);
  g_mkdir_with_parents (dir, 0700);
  g_autofree char *hash = g_compute_checksum_for_bytes (G_CHECKSUM_SHA256, bytes);
  const char *ext = "bin";
  if (n >= 4 && memcmp (data, "\x89PNG", 4) == 0)
    ext = "png";
  else if (n >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff)
    ext = "jpg";
  g_autofree char *name = g_strdup_printf ("%s.%s", hash, ext);
  char *path = g_build_filename (dir, name, NULL);
  if (!g_file_test (path, G_FILE_TEST_EXISTS))
    g_file_set_contents (path, (const char *)data, n, NULL);
  return path;
}

static void
ensure_desktop_file (const char *desktop_id, const char *name, const char *icon)
{
  g_autofree char *fn = g_strdup_printf ("%s.desktop", desktop_id);
  g_autofree char *dir = g_build_filename (g_get_user_data_dir (), "applications", NULL);
  g_autofree char *path = g_build_filename (dir, fn, NULL);
  g_autofree char *safe_name = NULL;
  g_autofree char *body = NULL;

  if (g_file_test (path, G_FILE_TEST_EXISTS))
    return;
  g_mkdir_with_parents (dir, 0700);
  safe_name = g_strdup (name ? name : "App");
  for (char *p = safe_name; *p; p++)
    {
      if (*p == '\n' || *p == '\r')
        *p = ' ';
    }
  body = g_strdup_printf (
      "[Desktop Entry]\n"
      "Type=Application\n"
      "Name=%s\n"
      "Icon=%s\n"
      "Exec=true\n"
      "NoDisplay=true\n"
      "StartupNotify=false\n"
      "X-GNOME-UsesNotifications=true\n",
      safe_name, icon && *icon ? icon : "phone");
  g_file_set_contents (path, body, -1, NULL);
}

static GVariant *
hints_rebuild (GVariant *hints, const char *desktop_entry, const char *category,
               const char *image_path)
{
  GVariantBuilder b;
  GVariantIter iter;
  const char *key;
  GVariant *val;
  gboolean have_desktop = FALSE;
  gboolean have_cat = FALSE;
  gboolean have_image = FALSE;

  g_variant_builder_init (&b, G_VARIANT_TYPE ("a{sv}"));
  g_variant_iter_init (&iter, hints);
  while (g_variant_iter_loop (&iter, "{&sv}", &key, &val))
    {
      if (g_strcmp0 (key, "desktop-entry") == 0)
        {
          if (desktop_entry && *desktop_entry)
            {
              g_variant_builder_add (&b, "{sv}", "desktop-entry",
                                     g_variant_new_string (desktop_entry));
              have_desktop = TRUE;
            }
          continue;
        }
      if (g_strcmp0 (key, "category") == 0)
        {
          if (category && *category)
            {
              g_variant_builder_add (&b, "{sv}", "category",
                                     g_variant_new_string (category));
              have_cat = TRUE;
            }
          continue;
        }
      if (g_strcmp0 (key, "image-path") == 0 && image_path && *image_path)
        {
          g_variant_builder_add (&b, "{sv}", "image-path",
                                 g_variant_new_string (image_path));
          have_image = TRUE;
          continue;
        }
      g_variant_builder_add (&b, "{sv}", key, val);
    }
  if (desktop_entry && *desktop_entry && !have_desktop)
    g_variant_builder_add (&b, "{sv}", "desktop-entry",
                           g_variant_new_string (desktop_entry));
  if (category && *category && !have_cat)
    g_variant_builder_add (&b, "{sv}", "category", g_variant_new_string (category));
  if (image_path && *image_path && !have_image)
    g_variant_builder_add (&b, "{sv}", "image-path", g_variant_new_string (image_path));
  return g_variant_builder_end (&b);
}

static GVariant *
rewrite_notify_params (GVariant *parameters, NotifCtx *ctx)
{
  const char *app_name = "";
  guint32 replaces = 0;
  const char *app_icon = "";
  const char *summary = "";
  const char *body = "";
  g_autoptr (GVariant) actions = NULL;
  g_autoptr (GVariant) hints = NULL;
  gint32 expire = -1;
  RewriteResult r = { 0 };
  g_autofree char *dumped = NULL;
  gboolean generated_desktop = FALSE;
  const char *out_icon;
  GVariant *new_hints;
  GVariant *out;

  g_variant_get (parameters, "(&su&s&s&s@as@a{sv}i)", &app_name, &replaces, &app_icon,
                 &summary, &body, &actions, &hints, &expire);

  g_mutex_lock (&g_mu);
  ensure_config_locked ();
  r = rewrite_apply (&g_cfg, ctx ? ctx->app_name : NULL, ctx ? ctx->pkg : NULL, summary, body);
  notify_log_event (&g_cfg, ctx ? ctx->app_name : NULL, ctx ? ctx->pkg : NULL, summary, body, &r);
  dumped = dump_icon (ctx ? ctx->icon : NULL);
  g_mutex_unlock (&g_mu);

  if (r.muted)
    {
      new_hints = hints_rebuild (hints, NULL, NULL, NULL);
      out = g_variant_new ("(susss@as@a{sv}i)", VALENT_NOTIFY_MUTED_APP, replaces, "",
                           summary, body, actions, new_hints, expire);
      rewrite_result_free (&r);
      return out;
    }

  if (r.desktop_entry == NULL || r.desktop_entry[0] == '\0')
    {
      g_autofree char *slug = vn_slugify (r.app_name);
      g_free (r.desktop_entry);
      r.desktop_entry = g_strconcat ("valent-notify-", slug, NULL);
      generated_desktop = TRUE;
    }

  out_icon = (r.icon && *r.icon) ? r.icon : (dumped ? dumped : app_icon);
  if (generated_desktop)
    ensure_desktop_file (r.desktop_entry, r.app_name, out_icon && *out_icon ? out_icon : dumped);

  new_hints = hints_rebuild (hints, r.desktop_entry, r.category, dumped);
  out = g_variant_new ("(susss@as@a{sv}i)", r.app_name ? r.app_name : app_name, replaces,
                       out_icon ? out_icon : "", summary, body, actions, new_hints, expire);
  rewrite_result_free (&r);
  return out;
}

gboolean
valent_packet_get_string (gpointer packet, const char *key, const char **value)
{
  gboolean ok;

  ensure_reals ();
  if (real_packet_get_string == NULL)
    return FALSE;

  ok = real_packet_get_string (packet, key, value);
  if (!ok || value == NULL || *value == NULL)
    return ok;

  g_mutex_lock (&g_mu);
  if (g_strcmp0 (key, "appName") == 0)
    {
      g_free (g_pending_app);
      g_pending_app = g_strdup (*value);
    }
  else if (g_strcmp0 (key, "packageName") == 0 || g_strcmp0 (key, "appPackage") == 0)
    {
      g_free (g_pending_pkg);
      g_pending_pkg = g_strdup (*value);
    }
  g_mutex_unlock (&g_mu);
  return ok;
}

GNotification *
g_notification_new (const char *title)
{
  GNotification *n;

  ensure_reals ();
  if (real_notif_new == NULL)
    return NULL;
  n = real_notif_new (title);

  g_mutex_lock (&g_mu);
  if (g_pending_app != NULL)
    {
      NotifCtx *c = g_new0 (NotifCtx, 1);
      c->app_name = g_steal_pointer (&g_pending_app);
      c->pkg = g_steal_pointer (&g_pending_pkg);
      g_hash_table_insert (table (), n, c);
    }
  g_mutex_unlock (&g_mu);
  return n;
}

void
g_notification_set_icon (GNotification *notification, GIcon *icon)
{
  ensure_reals ();
  g_mutex_lock (&g_mu);
  if (notification)
    {
      NotifCtx *c = g_hash_table_lookup (table (), notification);
      if (c != NULL)
        g_set_object (&c->icon, icon);
    }
  g_mutex_unlock (&g_mu);
  if (real_notif_set_icon)
    real_notif_set_icon (notification, icon);
}

static GDBusMessage *
notify_filter (GDBusConnection *connection, GDBusMessage *message, gboolean incoming,
               gpointer user_data)
{
  GVariant *body;
  NotifCtx *ctx = NULL;
  gboolean enabled = FALSE;

  (void)connection;
  (void)user_data;
  if (incoming || message == NULL)
    return message;
  if (g_dbus_message_get_message_type (message) != G_DBUS_MESSAGE_TYPE_METHOD_CALL)
    return message;
  if (g_strcmp0 (g_dbus_message_get_interface (message), "org.freedesktop.Notifications") != 0)
    return message;
  if (g_strcmp0 (g_dbus_message_get_member (message), "Notify") != 0)
    return message;

  body = g_dbus_message_get_body (message);
  if (body == NULL || !g_variant_is_of_type (body, G_VARIANT_TYPE ("(susssasa{sv}i)")))
    return message;

  g_mutex_lock (&g_mu);
  ensure_config_locked ();
  enabled = g_cfg.enabled;
  ctx = g_inflight;
  g_mutex_unlock (&g_mu);

  if (!enabled || ctx == NULL)
    return message;

  {
    GVariant *rewritten = rewrite_notify_params (body, ctx);
    if (g_variant_is_floating (rewritten))
      g_variant_ref_sink (rewritten);
    g_dbus_message_set_body (message, rewritten);
    g_variant_unref (rewritten);
  }
  return message;
}

static void
ensure_filter (GApplication *app)
{
  GDBusConnection *conn;

  if (g_filter_id != 0 || app == NULL)
    return;
  conn = g_application_get_dbus_connection (app);
  if (conn == NULL)
    return;
  g_filter_id = g_dbus_connection_add_filter (conn, notify_filter, NULL, NULL);
}

void
g_application_send_notification (GApplication *app, const gchar *id, GNotification *notification)
{
  ensure_reals ();
  ensure_filter (app);

  g_mutex_lock (&g_mu);
  g_inflight = notification ? g_hash_table_lookup (table (), notification) : NULL;
  g_mutex_unlock (&g_mu);

  if (real_send_notif)
    real_send_notif (app, id, notification);

  g_mutex_lock (&g_mu);
  g_inflight = NULL;
  if (notification)
    g_hash_table_remove (table (), notification);
  g_mutex_unlock (&g_mu);
}

__attribute__ ((constructor)) static void
valent_notify_init (void)
{
  ensure_reals ();
}
