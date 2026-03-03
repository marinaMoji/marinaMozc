// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// marinaMozc: GTK toolbar (marinaMoji-style: white/rounded, draggable,
// bottom-right, icon-only: logo, shin/kyū, odoriji).
// Build with MOZC_HAVE_GTK_TOOLBAR and link GTK to enable.

#include "unix/ibus/mozc_toolbar.h"

#include "unix/ibus/mozc_engine.h"
#include "unix/ibus/path_util.h"
#include "protocol/commands.pb.h"

#ifdef MOZC_HAVE_GTK_TOOLBAR
#include <gdk/gdk.h>
#include <gdk/gdkpixbuf.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <string>
#ifdef MOZC_HAVE_GTK_LAYER_SHELL
#include <gtk-layer-shell.h>
#endif
#endif

namespace mozc {
namespace ibus {

#ifdef MOZC_HAVE_GTK_TOOLBAR

namespace {

constexpr int kToolbarHeight = 36;
constexpr int kToolbarMargin = 20;
constexpr int kIconSize = 24;
constexpr int kToolbarLogoWidth = 120;

GtkWidget* g_toolbar_window = nullptr;
GtkWidget* g_toolbar_frame = nullptr;
GtkWidget* g_toolbar_box = nullptr;
GtkWidget* g_lead_cell = nullptr;
GtkWidget* g_logo_cell = nullptr;
GtkWidget* g_trail_cell = nullptr;
GtkWidget* g_mode_indicator_cell = nullptr;
GtkWidget* g_mode_indicator_image = nullptr;
GtkWidget* g_trad_btn = nullptr;
GtkWidget* g_odoriji_btn = nullptr;
GtkWidget* g_dict_cell = nullptr;
MozcEngine* g_engine = nullptr;
bool g_toolbar_positioned = false;
bool g_use_layer_shell = false;  // true if we used gtk-layer-shell for positioning
bool g_toolbar_user_moved = false;  // user dragged toolbar; restore position on next show
bool g_drag_active = false;
// marinaMoji-style: debounced raise on Wayland to reduce flicker.
static guint g_raise_idle_id = 0;
static gint64 g_last_raise_time = 0;
constexpr gint64 kRaiseIntervalUsec = 200 * 1000;  // 200 ms
// Delayed hide on focus loss (marinaMoji-style).
static guint g_pending_hide_timeout_id = 0;
constexpr guint kHideDelayMs = 150;
// Delayed reposition on Wayland (after compositor has laid out the window).
static guint g_reposition_timeout_id = 0;
constexpr guint kRepositionDelayMs = 150;
int g_drag_offset_x = 0;
int g_drag_offset_y = 0;
int g_window_x = 0;
int g_window_y = 0;

// Force toolbar to use X11/XWayland on GNOME Wayland so positioning and skip-taskbar work.
// Call only before any GDK/GTK init; uses env only (no gdk_display_*).
static void MaybeForceX11OnGnomeWayland() {
  const char* session = g_getenv("XDG_SESSION_TYPE");
  if (!session || g_ascii_strcasecmp(session, "wayland") != 0) return;
  const char* desktop = g_getenv("XDG_CURRENT_DESKTOP");
  const bool is_gnome = desktop && g_strrstr(desktop, "GNOME");
  if (!is_gnome || g_getenv("GDK_BACKEND")) return;
  g_setenv("GDK_BACKEND", "x11", TRUE);
}

// Runtime detection: Wayland vs X11 (for dual-backend toolbar behaviour).
// marinaMoji uses #ifdef GDK_WINDOWING_* and GDK_IS_*_DISPLAY() plus XDG_SESSION_TYPE
// fallback; that requires GDK built with both backends. This code uses display name and
// XDG_SESSION_TYPE only so it compiles on any GDK build (e.g. X11-only in Bazel env).
static bool IsWayland() {
  const char* session = g_getenv("XDG_SESSION_TYPE");
  if (session && g_ascii_strcasecmp(session, "wayland") == 0) return true;
  GdkDisplay* d = gdk_display_get_default();
  if (!d) return false;
  const char* n = gdk_display_get_name(d);
  return n && g_str_has_prefix(n, "wayland");
}

static bool IsX11() {
  const char* session = g_getenv("XDG_SESSION_TYPE");
  if (session && g_ascii_strcasecmp(session, "x11") == 0) return true;
  GdkDisplay* d = gdk_display_get_default();
  if (!d) return false;
  const char* n = gdk_display_get_name(d);
  return n && n[0] == ':';
}

// Config path: ~/.config/ibus/marinamozc/toolbar.conf
static gchar* GetToolbarConfigPath() {
  gchar* config_dir =
      g_build_filename(g_get_user_config_dir(), "ibus", "marinamozc", nullptr);
  gchar* path = g_build_filename(config_dir, "toolbar.conf", nullptr);
  g_free(config_dir);
  return path;
}

// marinaMoji-style: read toolbar_hide_on_focus_loss from ~/.config/ibus/marinamozc/toolbar.conf
static bool LoadHideOnFocusLossPreference() {
  gchar* path = GetToolbarConfigPath();
  GKeyFile* keyfile = g_key_file_new();
  GError* error = nullptr;
  gboolean loaded = g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error);
  g_free(path);
  if (!loaded) {
    if (error) g_error_free(error);
    g_key_file_free(keyfile);
    return true;  // default: hide on focus loss
  }
  error = nullptr;
  gboolean hide =
      g_key_file_get_boolean(keyfile, "ui", "toolbar_hide_on_focus_loss", &error);
  if (error) {
    g_error_free(error);
    hide = true;
  }
  g_key_file_free(keyfile);
  return hide;
}

// On Wayland we never auto-hide on focus loss (marinaMoji behaviour).
static bool ShouldHideOnFocusLoss() {
  if (IsWayland()) return false;
  return LoadHideOnFocusLossPreference();
}

static void CancelPendingHide() {
  if (g_pending_hide_timeout_id == 0) return;
  g_source_remove(g_pending_hide_timeout_id);
  g_pending_hide_timeout_id = 0;
}

// Load an SVG icon from the install path and return a pixbuf at (w,h), or nullptr.
static GdkPixbuf* LoadSvgIcon(const std::string& filename, int w, int h) {
  std::string path = GetIconPath(filename);
  GError* err = nullptr;
  RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
  if (!handle) {
    if (err) g_error_free(err);
    return nullptr;
  }
  cairo_surface_t* surface =
      cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
  cairo_t* cr = cairo_create(surface);
  RsvgRectangle viewport = {0.0, 0.0, static_cast<double>(w),
                            static_cast<double>(h)};
  if (!rsvg_handle_render_document(handle, cr, &viewport, &err)) {
    if (err) g_error_free(err);
    g_object_unref(handle);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return nullptr;
  }
  GdkPixbuf* pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, w, h);
  g_object_unref(handle);
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  return pixbuf;
}

// Set a button image from an SVG icon; if load fails, leave the label.
static void SetButtonIcon(GtkButton* btn, const char* svg_name) {
  GdkPixbuf* pixbuf = LoadSvgIcon(svg_name, kIconSize, kIconSize);
  if (pixbuf) {
    GtkWidget* img = gtk_image_new_from_pixbuf(pixbuf);
    g_object_unref(pixbuf);
    gtk_button_set_image(btn, img);
    gtk_button_set_always_show_image(btn, TRUE);
  }
}

static bool IsButtonWidget(GtkWidget* widget) {
  if (!widget) return false;
  return (widget == g_trad_btn || widget == g_odoriji_btn ||
          widget == g_dict_cell ||
          gtk_widget_is_ancestor(widget, g_trad_btn) ||
          gtk_widget_is_ancestor(widget, g_odoriji_btn) ||
          gtk_widget_is_ancestor(widget, g_dict_cell));
}

static bool IsModeIndicatorWidget(GtkWidget* widget) {
  if (!widget || !g_mode_indicator_cell) return false;
  return (widget == g_mode_indicator_cell ||
          gtk_widget_is_ancestor(g_mode_indicator_cell, widget));
}

// Menu entries for the mode-indicator popup (same order as IME panel).
struct ModeMenuEntry {
  commands::CompositionMode mode;
  const char* label;
};
static constexpr ModeMenuEntry kModeMenuEntries[] = {
    {commands::DIRECT, "Direct input"},
    {commands::HIRAGANA, "Hiragana"},
    {commands::FULL_KATAKANA, "Katakana"},
    {commands::HALF_ASCII, "Latin"},
    {commands::FULL_ASCII, "Wide Latin"},
    {commands::HALF_KATAKANA, "Half width katakana"},
};

static void OnModeMenuActivate(GtkMenuItem* item, gpointer /*user_data*/) {
  if (!g_engine) return;
  gpointer p = g_object_get_data(G_OBJECT(item), "composition-mode");
  if (!p) return;
  auto mode = static_cast<commands::CompositionMode>(GPOINTER_TO_INT(p));
  g_engine->SetCompositionModeFromToolbar(mode);
}

// Deferred destroy so "activate" is delivered before the menu is destroyed (marinaMoji-style).
static gboolean ModeMenuDestroyIdle(gpointer user_data) {
  GtkWidget* menu = static_cast<GtkWidget*>(user_data);
  if (menu && GTK_IS_WIDGET(menu)) gtk_widget_destroy(menu);
  return G_SOURCE_REMOVE;
}
static void OnModeMenuDeactivate(GtkWidget* menu, gpointer /*user_data*/) {
  if (menu) g_idle_add(ModeMenuDestroyIdle, menu);
}

// Key press on menu: Escape closes it so user can dismiss without selecting.
static gboolean OnModeMenuKeyPress(GtkWidget* menu, GdkEventKey* event,
                                  gpointer /*user_data*/) {
  if (event->keyval == GDK_KEY_Escape) {
    gtk_menu_popdown(GTK_MENU(menu));
    return TRUE;
  }
  return FALSE;
}

// Build and show the input-schema menu. ESC / click outside / choice closes it (marinaMoji-style).
static void ShowModeIndicatorMenu(GdkEventButton* event) {
  if (!g_mode_indicator_cell || !g_engine) return;
  GtkWidget* menu = gtk_menu_new();
  for (const ModeMenuEntry& entry : kModeMenuEntries) {
    GtkWidget* item = gtk_menu_item_new_with_label(entry.label);
    g_object_set_data(G_OBJECT(item), "composition-mode",
                      GINT_TO_POINTER(static_cast<int>(entry.mode)));
    g_signal_connect(item, "activate", G_CALLBACK(OnModeMenuActivate), nullptr);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  }
  gtk_widget_show_all(menu);
  g_signal_connect(menu, "key-press-event", G_CALLBACK(OnModeMenuKeyPress), nullptr);
  g_signal_connect(menu, "deactivate", G_CALLBACK(OnModeMenuDeactivate), nullptr);
  GdkEvent* ev = event ? reinterpret_cast<GdkEvent*>(event) : nullptr;
  gtk_menu_popup_at_pointer(GTK_MENU(menu), ev);
}

// MarinaMoji-style mode indicator: map composition mode to toolbar SVG (light theme).
static const char* GetModeIndicatorIconName(commands::CompositionMode mode,
                                             bool light) {
  switch (mode) {
    case commands::DIRECT:
      return light ? "toolbar_roma_half_light.svg" : "toolbar_roma_half_dark.svg";
    case commands::HIRAGANA:
      return light ? "toolbar_hira_light.svg" : "toolbar_hira_dark.svg";
    case commands::FULL_KATAKANA:
      return light ? "toolbar_kata_light.svg" : "toolbar_kata_dark.svg";
    case commands::HALF_ASCII:
      return light ? "toolbar_roma_half_light.svg" : "toolbar_roma_half_dark.svg";
    case commands::FULL_ASCII:
      return light ? "toolbar_roma_full_light.svg" : "toolbar_roma_full_dark.svg";
    case commands::HALF_KATAKANA:
      return light ? "toolbar_kata_half_light.svg" : "toolbar_kata_half_dark.svg";
    case commands::MANYOSHU:
      // Show full katakana icon for Manyōshū (same as FULL_KATAKANA).
      return light ? "toolbar_kata_light.svg" : "toolbar_kata_dark.svg";
    default:
      return light ? "toolbar_hira_light.svg" : "toolbar_hira_dark.svg";
  }
}

static void UpdateModeIndicatorIcon(commands::CompositionMode mode) {
  if (!g_mode_indicator_image) return;
  const char* icon_name = GetModeIndicatorIconName(mode, true);
  GdkPixbuf* pixbuf = LoadSvgIcon(icon_name, kIconSize, kIconSize);
  if (pixbuf) {
    gtk_image_set_from_pixbuf(GTK_IMAGE(g_mode_indicator_image), pixbuf);
    g_object_unref(pixbuf);
  }
}

// Create image from SVG for logo (wider than button icons).
static GtkWidget* CreateLogoImage() {
  GdkPixbuf* pixbuf = LoadSvgIcon("logo_long_light.svg", kToolbarLogoWidth, kIconSize);
  if (!pixbuf) return gtk_image_new();
  GtkWidget* img = gtk_image_new_from_pixbuf(pixbuf);
  g_object_unref(pixbuf);
  return img;
}

static void MoveToBottomRight() {
  if (!g_toolbar_window) return;
  GdkDisplay* display = gtk_widget_get_display(g_toolbar_window);
  if (!display) return;
  GdkMonitor* monitor = gdk_display_get_primary_monitor(display);
  if (!monitor) {
    int n = gdk_display_get_n_monitors(display);
    if (n > 0) monitor = gdk_display_get_monitor(display, 0);
  }
  if (!monitor) {
    gtk_window_move(GTK_WINDOW(g_toolbar_window), 100, 100);
    return;
  }
  GdkRectangle workarea;
  gdk_monitor_get_workarea(monitor, &workarea);
  if (workarea.width <= 0 || workarea.height <= 0) {
    gdk_monitor_get_geometry(monitor, &workarea);
  }
  if (workarea.width <= 0 || workarea.height <= 0) {
    gtk_window_move(GTK_WINDOW(g_toolbar_window), 100, 100);
    return;
  }
  int width = 0, height = 0;
  gtk_window_get_size(GTK_WINDOW(g_toolbar_window), &width, &height);
  if (width <= 0 || height <= 0) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(g_toolbar_window, &alloc);
    width = alloc.width;
    height = alloc.height;
  }
  if (width <= 0) width = 380;
  if (height <= 0) height = kToolbarHeight;
  // Same margin formula as marinaMoji: distance from screen edge.
  int margin = (height / 2 > kToolbarMargin) ? (height / 2) : kToolbarMargin;
  int x = workarea.x + workarea.width - width - margin;
  int y = workarea.y + workarea.height - height - margin;
  if (x < workarea.x + margin) x = workarea.x + margin;
  if (y < workarea.y + margin) y = workarea.y + margin;
  gtk_window_move(GTK_WINDOW(g_toolbar_window), x, y);
  g_window_x = x;
  g_window_y = y;
}

// One-shot: re-apply bottom-right position after compositor has laid out the window (Wayland).
static gboolean DelayedRepositionCb(gpointer /*data*/) {
  g_reposition_timeout_id = 0;
  if (g_toolbar_window && gtk_widget_get_visible(g_toolbar_window) &&
      !g_toolbar_user_moved && IsWayland() && !g_use_layer_shell) {
    MoveToBottomRight();
  }
  return G_SOURCE_REMOVE;
}

// On X11/XWayland: unmanaged overlay so no taskbar entry and no WM focus animations.
static void OnRealize(GtkWidget* w, gpointer /*data*/) {
  if (!IsX11()) return;
  GdkWindow* gw = gtk_widget_get_window(w);
  if (gw) gdk_window_set_override_redirect(gw, TRUE);
}

static guint32 GetPresentTime() {
  guint32 timestamp = gtk_get_current_event_time();
  if (timestamp == GDK_CURRENT_TIME) {
    guint64 now = static_cast<guint64>(g_get_monotonic_time() / 1000);
    if (now == 0) now = static_cast<guint64>(g_get_real_time() / 1000);
    timestamp = static_cast<guint32>(now & 0xffffffffu);
  }
  return timestamp;
}

static void ClearRaiseSource() {
  if (g_raise_idle_id == 0) return;
  g_source_remove(g_raise_idle_id);
  g_raise_idle_id = 0;
}

static gboolean RaiseTimeoutCb(gpointer /*data*/) {
  g_raise_idle_id = 0;
  if (g_toolbar_window && gtk_widget_get_visible(g_toolbar_window)) {
    GdkWindow* gw = gtk_widget_get_window(g_toolbar_window);
    if (gw) gdk_window_raise(gw);
    g_last_raise_time = g_get_monotonic_time();
  }
  return G_SOURCE_REMOVE;
}

static void RequestRaise(bool immediate) {
  if (!g_toolbar_window || g_raise_idle_id != 0) return;
  gint64 now = g_get_monotonic_time();
  gint64 elapsed = now - g_last_raise_time;
  if (!immediate && g_last_raise_time > 0 && elapsed < kRaiseIntervalUsec) {
    gint64 remaining = kRaiseIntervalUsec - elapsed;
    guint delay_ms = static_cast<guint>(remaining / 1000);
    if (delay_ms < 1) delay_ms = 1;
    g_raise_idle_id = g_timeout_add(delay_ms, RaiseTimeoutCb, nullptr);
  } else {
    g_raise_idle_id = g_idle_add(RaiseTimeoutCb, nullptr);
  }
}

static gboolean PendingHideTimeoutCb(gpointer /*data*/) {
  g_pending_hide_timeout_id = 0;
  if (g_toolbar_window) {
    ClearRaiseSource();
    if (g_reposition_timeout_id != 0) {
      g_source_remove(g_reposition_timeout_id);
      g_reposition_timeout_id = 0;
    }
    gtk_widget_hide(g_toolbar_window);
  }
  return G_SOURCE_REMOVE;
}

static gboolean HideIdleCb(gpointer /*data*/) {
  if (g_toolbar_window) {
    ClearRaiseSource();
    if (g_reposition_timeout_id != 0) {
      g_source_remove(g_reposition_timeout_id);
      g_reposition_timeout_id = 0;
    }
    g_toolbar_user_moved = false;  // marinaMoji-style: revert to bottom-right on next show
    gtk_widget_hide(g_toolbar_window);
  }
  return G_SOURCE_REMOVE;
}

static void EnsureToolbarCreated();  // defined below

static gboolean ShowIdleCb(gpointer data) {
  MozcEngine* engine = static_cast<MozcEngine*>(data);
  if (!engine || engine != g_engine) return G_SOURCE_REMOVE;
  EnsureToolbarCreated();
  if (!g_toolbar_window) return G_SOURCE_REMOVE;
  gtk_window_set_keep_above(GTK_WINDOW(g_toolbar_window), TRUE);
  gboolean already_visible = gtk_widget_get_visible(g_toolbar_window);
  if (!already_visible) {
    gtk_widget_show_all(g_toolbar_window);
    if (g_toolbar_user_moved && (g_window_x != 0 || g_window_y != 0)) {
      gtk_window_move(GTK_WINDOW(g_toolbar_window), g_window_x, g_window_y);
    } else {
      MoveToBottomRight();
      g_toolbar_user_moved = false;
      // On Wayland without layer-shell, re-apply position after layout (marinaMoji-style).
      if (IsWayland() && !g_use_layer_shell && g_reposition_timeout_id == 0) {
        g_reposition_timeout_id =
            g_timeout_add(kRepositionDelayMs, DelayedRepositionCb, nullptr);
      }
    }
  } else {
    if (g_toolbar_user_moved && (g_window_x != 0 || g_window_y != 0)) {
      gtk_window_move(GTK_WINDOW(g_toolbar_window), g_window_x, g_window_y);
    }
  }
  if (IsWayland()) {
    RequestRaise(already_visible ? false : true);
  } else if (!already_visible) {
    gtk_window_present_with_time(GTK_WINDOW(g_toolbar_window), GetPresentTime());
  } else {
    GdkWindow* gw = gtk_widget_get_window(g_toolbar_window);
    if (gw) gdk_window_raise(gw);
  }
  return G_SOURCE_REMOVE;
}

// Position once we have a real size. X11: configure-event. Wayland: same when not using layer-shell (marinaMoji-style).
static gboolean OnConfigure(GtkWidget* w, GdkEventConfigure* ev, gpointer /*data*/) {
  if (w != g_toolbar_window || !ev || ev->width <= 0 || ev->height <= 0 || g_toolbar_positioned)
    return FALSE;
  const bool need_position = IsX11() || (IsWayland() && !g_use_layer_shell);
  if (need_position) {
    MoveToBottomRight();
    g_toolbar_positioned = true;
    g_toolbar_user_moved = false;  // marinaMoji-style: default position = not user-moved
  }
  return FALSE;
}

// Wayland without layer-shell: position on map (fallback if configure doesn't run).
static gboolean OnMap(GtkWidget* w, GdkEvent* /*ev*/, gpointer /*data*/) {
  if (w != g_toolbar_window || g_toolbar_positioned) return FALSE;
  if (IsWayland() && !g_use_layer_shell) {
    MoveToBottomRight();
    g_toolbar_positioned = true;
  }
  return FALSE;
}

#ifdef MOZC_HAVE_GTK_LAYER_SHELL
// Wayland: anchor bottom-right via layer-shell (wlr/Sway/Hyprland, etc.).
static void SetupLayerShellBottomRight(GtkWidget* window) {
  gtk_layer_init_for_window(GTK_WINDOW(window));
  gtk_layer_set_layer(GTK_WINDOW(window), GTK_LAYER_SHELL_LAYER_TOP);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
  gtk_layer_set_anchor(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
  gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_BOTTOM, kToolbarMargin);
  gtk_layer_set_margin(GTK_WINDOW(window), GTK_LAYER_SHELL_EDGE_RIGHT, kToolbarMargin);
  gtk_layer_set_keyboard_mode(GTK_WINDOW(window), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
}
#endif

static gboolean OnButtonPress(GtkWidget* widget, GdkEventButton* event,
                              gpointer /*data*/) {
  if (!g_toolbar_window || event->type != GDK_BUTTON_PRESS || event->button != 1) {
    return FALSE;
  }
  GtkWidget* target = gtk_get_event_widget(reinterpret_cast<GdkEvent*>(event));
  // Left-click on mode indicator: open input-schema menu (ESC / click outside / choice closes it).
  if (target && IsModeIndicatorWidget(target)) {
    ShowModeIndicatorMenu(event);
    return TRUE;
  }
  // Let actual buttons (trad, odoriji, dict) handle their own click.
  if (target && IsButtonWidget(target)) return FALSE;
  // marinaMoji-style: drag from anywhere else (logo, frame, box, spacers, gaps).
  g_toolbar_user_moved = true;
  if (IsWayland()) {
    gtk_window_begin_move_drag(GTK_WINDOW(g_toolbar_window), event->button,
                               static_cast<gint>(event->x_root),
                               static_cast<gint>(event->y_root),
                               event->time);
    return TRUE;
  }
  // X11 fallback: pointer grab + motion-notify + gtk_window_move.
  gtk_window_get_position(GTK_WINDOW(g_toolbar_window), &g_window_x, &g_window_y);
  g_drag_offset_x = static_cast<int>(event->x_root) - g_window_x;
  g_drag_offset_y = static_cast<int>(event->y_root) - g_window_y;
  g_drag_active = true;
  gtk_grab_add(g_toolbar_window);
  return TRUE;
}

static gboolean OnButtonRelease(GtkWidget* /*widget*/, GdkEventButton* event,
                                 gpointer /*data*/) {
  if (event->button == 1 && g_drag_active) {
    g_drag_active = false;
    gtk_grab_remove(g_toolbar_window);
    g_drag_offset_x = 0;
    g_drag_offset_y = 0;
    gtk_window_get_position(GTK_WINDOW(g_toolbar_window), &g_window_x, &g_window_y);
    return TRUE;
  }
  return FALSE;
}

static gboolean OnMotion(GtkWidget* /*widget*/, GdkEventMotion* event,
                         gpointer /*data*/) {
  if (!g_toolbar_window || !g_drag_active) return FALSE;
  gint new_x = static_cast<gint>(event->x_root) - g_drag_offset_x;
  gint new_y = static_cast<gint>(event->y_root) - g_drag_offset_y;
  gtk_window_move(GTK_WINDOW(g_toolbar_window), new_x, new_y);
  g_window_x = new_x;
  g_window_y = new_y;
  return TRUE;
}

static gboolean OnFocusIn(GtkWidget* /*widget*/, GdkEventFocus* /*event*/,
                          gpointer /*data*/) {
  return FALSE;
}

static gboolean OnFocusOut(GtkWidget* /*widget*/, GdkEventFocus* /*event*/,
                           gpointer /*data*/) {
  return FALSE;  // marinaMoji-style: window focus-out connected for consistency
}

static void EnsureToolbarCSS() {
  GtkCssProvider* provider = gtk_css_provider_new();
  const char* css =
      "#marinamozc-toolbar-window { background-color: transparent; }"
      "#marinamozc-toolbar {"
      "  background-color: rgba(255, 255, 255, 0.97);"
      "  color: #203758;"
      "  border-radius: 10px;"
      "  border: 1px solid rgba(0, 0, 0, 0.08);"
      "  padding: 6px 10px;"
      "}"
      "#marinamozc-mode-indicator,"
      "#marinamozc-trad-btn, #marinamozc-trad-btn:hover, #marinamozc-trad-btn:active,"
      "#marinamozc-odoriji-btn, #marinamozc-odoriji-btn:hover,"
      "#marinamozc-odoriji-btn:active,"
      "#marinamozc-dict-btn, #marinamozc-dict-btn:hover,"
      "#marinamozc-dict-btn:active {"
      "  background-color: transparent; border: none; box-shadow: none;"
      "  padding: 0; margin: 0; outline: none;"
      "}";
  gtk_css_provider_load_from_data(provider, css, -1, nullptr);
  gtk_style_context_add_provider_for_screen(
      gdk_screen_get_default(),
      GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

// Shin/kyū: regular button (like odoriji); icon is updated from output, not toggle state.
void OnTradClicked(GtkWidget* /*widget*/, gpointer /*data*/) {
  if (g_engine) {
    g_engine->SendToolbarSessionCommand(
        commands::SessionCommand::TOGGLE_TRADITIONAL_KANJI);
  }
}

void OnOdorijiClicked(GtkWidget* /*widget*/, gpointer /*data*/) {
  if (g_engine) {
    g_engine->SendToolbarSessionCommand(
        commands::SessionCommand::SHOW_ODORIJI_PALETTE);
  }
}

// Left click = Add Word, right click = Dictionary tool.
static gboolean OnDictButtonPress(GtkWidget* /*widget*/, GdkEventButton* event,
                                  gpointer /*data*/) {
  if (!g_engine || event->type != GDK_BUTTON_PRESS) return FALSE;
  if (event->button == 1) {
    g_engine->LaunchToolFromToolbar("word_register_dialog");
    return TRUE;
  }
  if (event->button == 3) {
    g_engine->LaunchToolFromToolbar("dictionary_tool");
    return TRUE;
  }
  return FALSE;
}

void EnsureToolbarCreated() {
  if (g_toolbar_window) return;
  MaybeForceX11OnGnomeWayland();
  if (!gtk_init_check(nullptr, nullptr)) return;

  EnsureToolbarCSS();

  // TOPLEVEL + DOCK (marinaMoji-style) for bottom-right placement; skip_taskbar still set.
  // keep_above + accept_focus/focus_on_map/can_focus false; draggable via gtk_window_begin_move_drag.
  g_toolbar_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(g_toolbar_window), "marinaMozc");
  gtk_window_set_decorated(GTK_WINDOW(g_toolbar_window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(g_toolbar_window), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_skip_pager_hint(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_type_hint(GTK_WINDOW(g_toolbar_window), GDK_WINDOW_TYPE_HINT_DOCK);
  gtk_window_set_accept_focus(GTK_WINDOW(g_toolbar_window), FALSE);
  gtk_window_set_focus_on_map(GTK_WINDOW(g_toolbar_window), FALSE);
  gtk_widget_set_can_focus(g_toolbar_window, FALSE);
  gtk_window_set_default_size(GTK_WINDOW(g_toolbar_window), 380, kToolbarHeight);
  gtk_container_set_border_width(GTK_CONTAINER(g_toolbar_window), 0);
  gtk_widget_set_app_paintable(g_toolbar_window, TRUE);
  gtk_widget_set_name(g_toolbar_window, "marinamozc-toolbar-window");

  GdkScreen* screen = gtk_widget_get_screen(g_toolbar_window);
  GdkVisual* visual = gdk_screen_get_rgba_visual(screen);
  if (visual) gtk_widget_set_visual(g_toolbar_window, visual);

  gtk_widget_add_events(g_toolbar_window,
      GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK);
  g_signal_connect(g_toolbar_window, "button-press-event",
                   G_CALLBACK(OnButtonPress), nullptr);
  g_signal_connect(g_toolbar_window, "button-release-event",
                   G_CALLBACK(OnButtonRelease), nullptr);
  g_signal_connect(g_toolbar_window, "motion-notify-event",
                   G_CALLBACK(OnMotion), nullptr);
  g_signal_connect(g_toolbar_window, "focus-in-event",
                   G_CALLBACK(OnFocusIn), nullptr);
  g_signal_connect(g_toolbar_window, "focus-out-event",
                   G_CALLBACK(OnFocusOut), nullptr);
  g_signal_connect(g_toolbar_window, "realize", G_CALLBACK(OnRealize), nullptr);
  g_signal_connect(g_toolbar_window, "configure-event",
                   G_CALLBACK(OnConfigure), nullptr);
  g_signal_connect(g_toolbar_window, "map-event", G_CALLBACK(OnMap), nullptr);

  if (IsWayland()) {
#ifdef MOZC_HAVE_GTK_LAYER_SHELL
    SetupLayerShellBottomRight(g_toolbar_window);
    g_toolbar_positioned = true;
    g_use_layer_shell = true;
#else
    // Wayland without layer-shell: position via MoveToBottomRight in OnConfigure/OnMap (marinaMoji-style).
#endif
  }

  gtk_widget_set_size_request(g_toolbar_window, -1, kToolbarHeight);

  g_toolbar_frame = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(g_toolbar_frame), TRUE);
  gtk_widget_set_can_focus(g_toolbar_frame, FALSE);
  gtk_widget_set_name(g_toolbar_frame, "marinamozc-toolbar");
  gtk_widget_add_events(g_toolbar_frame, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_toolbar_frame, "button-press-event",
                   G_CALLBACK(OnButtonPress), nullptr);
  gtk_container_add(GTK_CONTAINER(g_toolbar_window), g_toolbar_frame);

  g_toolbar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_can_focus(g_toolbar_box, FALSE);
  gtk_widget_add_events(g_toolbar_box, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_toolbar_box, "button-press-event",
                   G_CALLBACK(OnButtonPress), nullptr);
  gtk_container_add(GTK_CONTAINER(g_toolbar_frame), g_toolbar_box);

  // Lead spacer and logo: draggable (no handler here so click propagates to box → OnButtonPress).
  g_lead_cell = gtk_event_box_new();
  gtk_widget_set_size_request(g_lead_cell, 8, kToolbarHeight);
  gtk_widget_set_can_focus(g_lead_cell, FALSE);
  gtk_widget_add_events(g_lead_cell, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_lead_cell, "button-press-event", G_CALLBACK(OnButtonPress), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_lead_cell, FALSE, FALSE, 0);

  GtkWidget* logo_img = CreateLogoImage();
  gtk_widget_set_halign(logo_img, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(logo_img, GTK_ALIGN_CENTER);
  g_logo_cell = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(g_logo_cell), TRUE);
  gtk_widget_set_size_request(g_logo_cell, kToolbarLogoWidth, 32);
  gtk_widget_set_can_focus(g_logo_cell, FALSE);
  gtk_widget_add_events(g_logo_cell, GDK_BUTTON_PRESS_MASK);
  // No button-press on logo_cell: let event propagate to box so drag is handled in one place.
  gtk_container_add(GTK_CONTAINER(g_logo_cell), logo_img);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_logo_cell, FALSE, FALSE, 0);

  // Mode indicator (marinaMoji-style: あ/ア/roma etc.) — same vertical alignment as other toolbar icons.
  GdkPixbuf* mode_pixbuf =
      LoadSvgIcon(GetModeIndicatorIconName(commands::HIRAGANA, true),
                  kIconSize, kIconSize);
  g_mode_indicator_image = gtk_image_new_from_pixbuf(mode_pixbuf);
  if (mode_pixbuf) g_object_unref(mode_pixbuf);
  gtk_widget_set_halign(g_mode_indicator_image, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(g_mode_indicator_image, GTK_ALIGN_CENTER);
  g_mode_indicator_cell = gtk_event_box_new();
  gtk_widget_set_name(g_mode_indicator_cell, "marinamozc-mode-indicator");
  gtk_widget_set_size_request(g_mode_indicator_cell, 32, 32);
  gtk_widget_set_halign(g_mode_indicator_cell, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(g_mode_indicator_cell, GTK_ALIGN_CENTER);
  gtk_widget_set_can_focus(g_mode_indicator_cell, FALSE);
  gtk_widget_add_events(g_mode_indicator_cell, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_mode_indicator_cell, "button-press-event",
                   G_CALLBACK(OnButtonPress), nullptr);
  gtk_container_add(GTK_CONTAINER(g_mode_indicator_cell), g_mode_indicator_image);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_mode_indicator_cell, FALSE, FALSE, 0);

  g_trad_btn = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(g_trad_btn), GTK_RELIEF_NONE);
  gtk_widget_set_focus_on_click(g_trad_btn, FALSE);
  gtk_widget_set_can_focus(g_trad_btn, FALSE);
  gtk_widget_set_size_request(g_trad_btn, 32, 32);
  gtk_widget_set_name(g_trad_btn, "marinamozc-trad-btn");
  gtk_button_set_label(GTK_BUTTON(g_trad_btn), "");
  SetButtonIcon(GTK_BUTTON(g_trad_btn), "toolbar_shin_light.svg");
  g_signal_connect(g_trad_btn, "clicked", G_CALLBACK(OnTradClicked), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_trad_btn, FALSE, FALSE, 0);

  g_odoriji_btn = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(g_odoriji_btn), GTK_RELIEF_NONE);
  gtk_widget_set_focus_on_click(g_odoriji_btn, FALSE);
  gtk_widget_set_can_focus(g_odoriji_btn, FALSE);
  gtk_widget_set_size_request(g_odoriji_btn, 32, 32);
  gtk_widget_set_name(g_odoriji_btn, "marinamozc-odoriji-btn");
  gtk_button_set_label(GTK_BUTTON(g_odoriji_btn), "");
  SetButtonIcon(GTK_BUTTON(g_odoriji_btn), "toolbar_marks_light.svg");
  g_signal_connect(g_odoriji_btn, "clicked", G_CALLBACK(OnOdorijiClicked), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_odoriji_btn, FALSE, FALSE, 0);

  // Dict button: left click = Add Word, right click = Dictionary tool.
  GdkPixbuf* dict_pixbuf = LoadSvgIcon("toolbar_dict_light.svg", kIconSize, kIconSize);
  GtkWidget* dict_img = gtk_image_new_from_pixbuf(dict_pixbuf);
  if (dict_pixbuf) g_object_unref(dict_pixbuf);
  gtk_widget_set_halign(dict_img, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(dict_img, GTK_ALIGN_CENTER);
  g_dict_cell = gtk_event_box_new();
  gtk_widget_set_name(g_dict_cell, "marinamozc-dict-btn");
  gtk_widget_set_size_request(g_dict_cell, 32, 32);
  gtk_widget_set_halign(g_dict_cell, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(g_dict_cell, GTK_ALIGN_CENTER);
  gtk_widget_set_can_focus(g_dict_cell, FALSE);
  gtk_widget_add_events(g_dict_cell, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_dict_cell, "button-press-event",
                   G_CALLBACK(OnDictButtonPress), nullptr);
  gtk_container_add(GTK_CONTAINER(g_dict_cell), dict_img);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_dict_cell, FALSE, FALSE, 0);

  g_trail_cell = gtk_event_box_new();
  gtk_widget_set_size_request(g_trail_cell, 8, kToolbarHeight);
  gtk_widget_set_can_focus(g_trail_cell, FALSE);
  gtk_widget_add_events(g_trail_cell, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_trail_cell, "button-press-event", G_CALLBACK(OnButtonPress), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_trail_cell, FALSE, FALSE, 0);
}

void ApplyOutputToToolbar(const commands::Output& output) {
  if (!g_trad_btn || !g_odoriji_btn) return;
  if (output.has_config()) {
    bool use_trad = output.config().use_traditional_kanji();
    SetButtonIcon(GTK_BUTTON(g_trad_btn),
                  use_trad ? "toolbar_kyu_light.svg" : "toolbar_shin_light.svg");
  }
  if (output.has_status() && g_mode_indicator_image) {
    commands::CompositionMode mode = output.status().activated()
                                         ? output.status().mode()
                                         : commands::DIRECT;
    UpdateModeIndicatorIcon(mode);
  }
}

}  // namespace

void MozcToolbarShow(MozcEngine* engine) {
  if (!engine) return;
  CancelPendingHide();
  g_engine = engine;
  g_idle_add(ShowIdleCb, engine);
}

void MozcToolbarHide() {
  CancelPendingHide();
  g_idle_add(HideIdleCb, nullptr);
}

bool MozcToolbarShouldHideOnFocusLoss() {
  return ShouldHideOnFocusLoss();
}

void MozcToolbarScheduleHideDelayed(guint delay_ms) {
  CancelPendingHide();
  if (!g_toolbar_window) return;
  g_pending_hide_timeout_id = g_timeout_add(delay_ms, PendingHideTimeoutCb, nullptr);
}

void MozcToolbarUpdate(const commands::Output& output) {
  if (!g_toolbar_window || !gtk_widget_get_visible(g_toolbar_window)) return;
  ApplyOutputToToolbar(output);
}

bool MozcToolbarAvailable() { return true; }

// Load toolbar visibility. Default true (on) for first install; thereafter use saved value.
bool MozcToolbarLoadVisiblePreference() {
  gchar* config_dir =
      g_build_filename(g_get_user_config_dir(), "ibus", "marinamozc", nullptr);
  gchar* path = g_build_filename(config_dir, "toolbar.conf", nullptr);
  g_free(config_dir);
  GKeyFile* keyfile = g_key_file_new();
  GError* error = nullptr;
  gboolean loaded = g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error);
  g_free(path);
  if (!loaded) {
    if (error) g_error_free(error);
    g_key_file_free(keyfile);
    return true;  // default: toolbar on for new install
  }
  error = nullptr;
  gboolean visible =
      g_key_file_get_boolean(keyfile, "ui", "toolbar_visible", &error);
  g_key_file_free(keyfile);
  if (error) {
    g_error_free(error);
    return true;  // key missing: first run, default on
  }
  return visible;
}

// Persist toolbar visibility so it stays off/on until user toggles again.
void MozcToolbarSaveVisiblePreference(bool visible) {
  gchar* config_dir =
      g_build_filename(g_get_user_config_dir(), "ibus", "marinamozc", nullptr);
  gchar* path = g_build_filename(config_dir, "toolbar.conf", nullptr);
  g_free(config_dir);
  GKeyFile* keyfile = g_key_file_new();
  GError* error = nullptr;
  g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error);
  if (error) g_error_free(error);
  g_key_file_set_boolean(keyfile, "ui", "toolbar_visible", visible ? TRUE : FALSE);
  gchar* dir = g_path_get_dirname(path);
  if (g_mkdir_with_parents(dir, 0755) == 0) {
    error = nullptr;
    if (!g_key_file_save_to_file(keyfile, path, &error) && error) {
      g_error_free(error);
    }
  }
  g_free(dir);
  g_free(path);
  g_key_file_free(keyfile);
}

#else

void MozcToolbarShow(MozcEngine* /*engine*/) {}
void MozcToolbarHide() {}
bool MozcToolbarShouldHideOnFocusLoss() { return false; }
void MozcToolbarScheduleHideDelayed(unsigned int /*delay_ms*/) {}
void MozcToolbarUpdate(const commands::Output& /*output*/) {}
bool MozcToolbarAvailable() { return false; }
bool MozcToolbarLoadVisiblePreference() { return true; }
void MozcToolbarSaveVisiblePreference(bool /*visible*/) {}

#endif  // MOZC_HAVE_GTK_TOOLBAR

}  // namespace ibus
}  // namespace mozc
