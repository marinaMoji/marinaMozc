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
#include "protocol/config.pb.h"

#ifdef MOZC_HAVE_GTK_TOOLBAR
#include <gdk/gdk.h>
#include <gdk/gdkpixbuf.h>
#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
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
GtkWidget* g_shortcuts_btn = nullptr;
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
static unsigned g_drag_motion_count = 0;

// Theme: follow system light/dark (e.g. Linux Mint, GNOME, Cinnamon).
static bool g_toolbar_dark_theme = false;
static GtkCssProvider* g_toolbar_css_provider = nullptr;
static commands::CompositionMode g_last_toolbar_mode = commands::HIRAGANA;
static GSettings* g_theme_settings = nullptr;
static gulong g_theme_signal_id_color = 0;
static gulong g_theme_signal_id_gtk = 0;

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

// Detect system dark mode (GNOME/Linux Mint/Cinnamon: color-scheme or gtk-theme name).
static bool IsSystemThemeDark() {
  GSettings* settings = g_settings_new("org.gnome.desktop.interface");
  if (!settings) return false;
  bool dark = false;
  gchar* color_scheme = g_settings_get_string(settings, "color-scheme");
  if (color_scheme) {
    if (g_str_equal(color_scheme, "prefer-dark")) dark = true;
    g_free(color_scheme);
  }
  if (!dark) {
    gchar* gtk_theme = g_settings_get_string(settings, "gtk-theme");
    if (gtk_theme) {
      for (char* p = gtk_theme; *p; ++p) *p = g_ascii_tolower(*p);
      if (strstr(gtk_theme, "dark")) dark = true;
      g_free(gtk_theme);
    }
  }
  g_object_unref(settings);
  return dark;
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
          widget == g_dict_cell || widget == g_shortcuts_btn ||
          gtk_widget_is_ancestor(widget, g_trad_btn) ||
          gtk_widget_is_ancestor(widget, g_odoriji_btn) ||
          gtk_widget_is_ancestor(widget, g_dict_cell) ||
          gtk_widget_is_ancestor(widget, g_shortcuts_btn));
}

static bool IsModeIndicatorWidget(GtkWidget* widget) {
  if (!widget || !g_mode_indicator_cell) return false;
  return (widget == g_mode_indicator_cell ||
          gtk_widget_is_ancestor(widget, g_mode_indicator_cell));
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
  // Stored as (mode + 1) so DIRECT (0) is not NULL and get_data returns a valid pointer.
  int mode_val = GPOINTER_TO_INT(p) - 1;
  auto mode = static_cast<commands::CompositionMode>(mode_val);
  g_engine->SetCompositionModeFromToolbar(mode);
}

// Deferred destroy so "activate" is delivered before the menu is destroyed (marinaMoji-style).
// Use a short timeout (150ms) so deactivate doesn't destroy the menu before the clicked item's
// "activate" signal is processed (fixes Direct input / mode menu not applying on some setups).
static gboolean ModeMenuDestroyIdle(gpointer user_data) {
  GtkWidget* menu = static_cast<GtkWidget*>(user_data);
  if (menu && GTK_IS_WIDGET(menu)) gtk_widget_destroy(menu);
  return G_SOURCE_REMOVE;
}
static void OnModeMenuDeactivate(GtkWidget* menu, gpointer /*user_data*/) {
  if (menu) g_timeout_add(150, ModeMenuDestroyIdle, menu);
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
    // Store (mode + 1) so DIRECT (0) is not stored as NULL (GINT_TO_POINTER(0) == NULL).
    g_object_set_data(G_OBJECT(item), "composition-mode",
                      GINT_TO_POINTER(static_cast<int>(entry.mode) + 1));
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
  bool light = !g_toolbar_dark_theme;
  const char* icon_name = GetModeIndicatorIconName(mode, light);
  GdkPixbuf* pixbuf = LoadSvgIcon(icon_name, kIconSize, kIconSize);
  if (pixbuf) {
    gtk_image_set_from_pixbuf(GTK_IMAGE(g_mode_indicator_image), pixbuf);
    g_object_unref(pixbuf);
  }
}

// Create image from SVG for logo (wider than button icons). |dark| = use dark-theme logo.
static GtkWidget* CreateLogoImage(bool dark) {
  const char* filename = dark ? "logo_long_dark.svg" : "logo_long_light.svg";
  GdkPixbuf* pixbuf = LoadSvgIcon(filename, kToolbarLogoWidth, kIconSize);
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
static void SetToolbarPosition(int x, int y);  // defined below
static void RefreshToolbarTheme();  // defined below

static gboolean ShowIdleCb(gpointer data) {
  MozcEngine* engine = static_cast<MozcEngine*>(data);
  if (!engine || engine != g_engine) return G_SOURCE_REMOVE;
  EnsureToolbarCreated();
  if (!g_toolbar_window) return G_SOURCE_REMOVE;
  RefreshToolbarTheme();  // Pick up system theme changes while toolbar was hidden.
  gtk_window_set_keep_above(GTK_WINDOW(g_toolbar_window), TRUE);
  gboolean already_visible = gtk_widget_get_visible(g_toolbar_window);
  if (!already_visible) {
    gtk_widget_show_all(g_toolbar_window);
    if (g_toolbar_user_moved && (g_window_x != 0 || g_window_y != 0)) {
      SetToolbarPosition(g_window_x, g_window_y);
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
      SetToolbarPosition(g_window_x, g_window_y);
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
  // Set shin/kyu icon from current config so toolbar matches actual mode at startup.
  if (g_trad_btn && g_engine) {
    config::Config config;
    if (g_engine->GetConfig(&config)) {
      bool use_trad = config.use_traditional_kanji();
      const char* kyu = g_toolbar_dark_theme ? "toolbar_kyu_dark.svg" : "toolbar_kyu_light.svg";
      const char* shin = g_toolbar_dark_theme ? "toolbar_shin_dark.svg" : "toolbar_shin_light.svg";
      SetButtonIcon(GTK_BUTTON(g_trad_btn), use_trad ? kyu : shin);
    }
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

// Reposition layer-shell by anchoring to top-left and setting margins (for drag).
static void SetLayerShellPosition(GtkWindow* window, int x, int y) {
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_TOP, TRUE);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_BOTTOM, FALSE);
  gtk_layer_set_anchor(window, GTK_LAYER_SHELL_EDGE_RIGHT, FALSE);
  gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_LEFT, x);
  gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_TOP, y);
  gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_RIGHT, 0);
  gtk_layer_set_margin(window, GTK_LAYER_SHELL_EDGE_BOTTOM, 0);
}
#endif

// Set toolbar position; uses layer-shell anchors+margins on Wayland when layer-shell is active.
static void SetToolbarPosition(int x, int y) {
  if (!g_toolbar_window) return;
#ifdef MOZC_HAVE_GTK_LAYER_SHELL
  if (g_use_layer_shell) {
    SetLayerShellPosition(GTK_WINDOW(g_toolbar_window), x, y);
    return;
  }
#endif
  gtk_window_move(GTK_WINDOW(g_toolbar_window), x, y);
}

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
  // Use manual grab + motion + gtk_window_move everywhere: begin_move_drag does not move
  // the window on many Wayland compositors; on X11 manual path is used for override_redirect.
  const bool use_native_move = false;
  if (use_native_move) {
    gtk_window_begin_move_drag(GTK_WINDOW(g_toolbar_window), event->button,
                               static_cast<gint>(event->x_root),
                               static_cast<gint>(event->y_root),
                               event->time);
    return TRUE;
  }
  // Pointer grab + motion, then SetToolbarPosition (gtk_window_move or layer-shell margins).
  // On Wayland (with or without layer-shell) gtk_window_get_position is often unreliable;
  // use window-relative offset so desired position = (x_root - event->x, y_root - event->y).
  if (IsWayland() || g_use_layer_shell) {
    g_drag_offset_x = static_cast<int>(event->x);
    g_drag_offset_y = static_cast<int>(event->y);
  } else {
    gtk_window_get_position(GTK_WINDOW(g_toolbar_window), &g_window_x, &g_window_y);
    g_drag_offset_x = static_cast<int>(event->x_root) - g_window_x;
    g_drag_offset_y = static_cast<int>(event->y_root) - g_window_y;
  }
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
    // g_window_x/y already updated in OnMotion; get_position is unreliable for layer-shell.
    if (!g_use_layer_shell)
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
  SetToolbarPosition(new_x, new_y);
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

static void EnsureToolbarCSS(bool dark) {
  const char* bg = dark
      ? "rgba(32, 35, 40, 0.97)"
      : "rgba(255, 255, 255, 0.97)";
  const char* fg = dark ? "#e0e0e0" : "#203758";
  const char* border = dark
      ? "rgba(255, 255, 255, 0.12)"
      : "rgba(0, 0, 0, 0.08)";
  if (!g_toolbar_css_provider) {
    g_toolbar_css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(g_toolbar_css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  std::string css =
      "#marinamozc-toolbar-window { background-color: transparent; }"
      "#marinamozc-toolbar {"
      "  background-color: " + std::string(bg) + ";"
      "  color: " + std::string(fg) + ";"
      "  border-radius: 10px;"
      "  border: 1px solid " + std::string(border) + ";"
      "  padding: 6px 10px;"
      "}"
      "#marinamozc-mode-indicator,"
      "#marinamozc-trad-btn, #marinamozc-trad-btn:hover, #marinamozc-trad-btn:active,"
      "#marinamozc-odoriji-btn, #marinamozc-odoriji-btn:hover,"
      "#marinamozc-odoriji-btn:active,"
      "#marinamozc-dict-btn, #marinamozc-dict-btn:hover,"
      "#marinamozc-dict-btn:active,"
      "#marinamozc-shortcuts-btn, #marinamozc-shortcuts-btn:hover,"
      "#marinamozc-shortcuts-btn:active {"
      "  background-color: transparent; border: none; box-shadow: none;"
      "  padding: 0; margin: 0; outline: none;"
      "}";
  gtk_css_provider_load_from_data(g_toolbar_css_provider, css.c_str(),
                                  static_cast<gssize>(css.size()), nullptr);
  if (g_toolbar_frame) gtk_widget_queue_draw(g_toolbar_frame);
}

// Re-apply theme (CSS + all icons) when system theme changes or on first show.
static void RefreshToolbarTheme() {
  g_toolbar_dark_theme = IsSystemThemeDark();
  EnsureToolbarCSS(g_toolbar_dark_theme);
  if (!g_toolbar_window) return;
  // Logo: replace image inside g_logo_cell.
  if (g_logo_cell) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(g_logo_cell));
    if (children) {
      gtk_container_remove(GTK_CONTAINER(g_logo_cell), GTK_WIDGET(children->data));
      g_list_free(children);
    }
    GtkWidget* logo_img = CreateLogoImage(g_toolbar_dark_theme);
    gtk_widget_set_halign(logo_img, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(logo_img, GTK_ALIGN_CENTER);
    gtk_container_add(GTK_CONTAINER(g_logo_cell), logo_img);
    gtk_widget_show(logo_img);
  }
  UpdateModeIndicatorIcon(g_last_toolbar_mode);
  if (g_trad_btn && g_engine) {
    config::Config config;
    bool use_trad = false;
    if (g_engine->GetConfig(&config)) use_trad = config.use_traditional_kanji();
    SetButtonIcon(GTK_BUTTON(g_trad_btn),
                  use_trad ? (g_toolbar_dark_theme ? "toolbar_kyu_dark.svg" : "toolbar_kyu_light.svg")
                          : (g_toolbar_dark_theme ? "toolbar_shin_dark.svg" : "toolbar_shin_light.svg"));
  }
  if (g_odoriji_btn) {
    SetButtonIcon(GTK_BUTTON(g_odoriji_btn),
                  g_toolbar_dark_theme ? "toolbar_marks_dark.svg" : "toolbar_marks_light.svg");
  }
  if (g_dict_cell) {
    GList* children = gtk_container_get_children(GTK_CONTAINER(g_dict_cell));
    if (children && GTK_IS_IMAGE(children->data)) {
      GdkPixbuf* dict_pixbuf = LoadSvgIcon(
          g_toolbar_dark_theme ? "toolbar_dict_dark.svg" : "toolbar_dict_light.svg",
          kIconSize, kIconSize);
      if (dict_pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(children->data), dict_pixbuf);
        g_object_unref(dict_pixbuf);
      }
      g_list_free(children);
    }
  }
  if (g_shortcuts_btn) {
    SetButtonIcon(GTK_BUTTON(g_shortcuts_btn),
                  g_toolbar_dark_theme ? "toolbar_shortcuts_dark.svg" : "toolbar_shortcuts_light.svg");
  }
}

static void OnThemeChanged(GSettings* /*settings*/, const char* /*key*/,
                          gpointer /*user_data*/) {
  RefreshToolbarTheme();
}

static void RegisterToolbarThemeObserver() {
  if (g_theme_settings) return;
  g_theme_settings = g_settings_new("org.gnome.desktop.interface");
  if (!g_theme_settings) return;
  g_theme_signal_id_color = g_signal_connect(g_theme_settings, "changed::color-scheme",
                                             G_CALLBACK(OnThemeChanged), nullptr);
  g_theme_signal_id_gtk = g_signal_connect(g_theme_settings, "changed::gtk-theme",
                                            G_CALLBACK(OnThemeChanged), nullptr);
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

// Shortcuts popup: multi-page dialog listing key combinations from the keymap
// TSV selected in settings (ms-ime, atok, kotoeri, or custom).
using ShortcutEntry = std::pair<std::string, std::string>;  // key, command
// Grouped row: (function/result name, comma-separated keys)
using GroupedShortcutRow = std::pair<std::string, std::string>;

// Returns the keymap TSV filename for the given session keymap (for loading
// via GetIconPath). Returns empty string for CUSTOM (content comes from config).
static std::string KeymapFilenameFromSessionKeymap(
    config::Config::SessionKeymap keymap) {
  switch (keymap) {
    case config::Config::ATOK:
      return "atok.tsv";
    case config::Config::MSIME:
      return "ms-ime.tsv";
    case config::Config::KOTOERI:
      return "kotoeri.tsv";
    case config::Config::MOBILE:
      return "mobile.tsv";
    case config::Config::CHROMEOS:
      return "chromeos.tsv";
    case config::Config::CUSTOM:
      return "";
    case config::Config::NONE:
    default:
      return "ms-ime.tsv";
  }
}

static const char* const kScriptCommands[] = {
    "ToggleAlphanumericMode", "ToggleHiraganaDirect", "ToggleTraditionalKanji",
    "ToggleManyoshuHiragana", "ConvertToFullKatakana", "ConvertToHalfWidth",
    "ConvertToFullAlphanumeric", "ConvertToHiragana", nullptr};
static const char* const kCompositionCommands[] = {
    "Commit", "LaunchWordRegisterDialog", "SegmentWidthShrink",
    "SegmentWidthExpand", nullptr};

static bool CommandInList(const char* cmd, const char* const* list) {
  for (; *list; ++list)
    if (strcmp(cmd, *list) == 0) return true;
  return false;
}

static void ParseKeymapTsv(const std::string& path,
                           std::vector<ShortcutEntry>* script,
                           std::vector<ShortcutEntry>* composition) {
  script->clear();
  composition->clear();
  std::ifstream f(path);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    size_t t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    std::string state = line.substr(0, t1);
    std::string key = line.substr(t1 + 1, t2 - (t1 + 1));
    std::string command = line.substr(t2 + 1);
    while (!command.empty() && (command.back() == '\r' || command.back() == ' '))
      command.pop_back();
    if (state == "status" && key == "key") continue;  // skip header line
    if (CommandInList(command.c_str(), kScriptCommands))
      script->emplace_back(key, command);
    if (CommandInList(command.c_str(), kCompositionCommands))
      composition->emplace_back(key, command);
  }
}

// Same as ParseKeymapTsv but from in-memory TSV content (e.g. custom keymap).
static void ParseKeymapFromString(const std::string& content,
                                  std::vector<ShortcutEntry>* script,
                                  std::vector<ShortcutEntry>* composition) {
  script->clear();
  composition->clear();
  std::istringstream stream(content);
  std::string line;
  while (std::getline(stream, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    size_t t2 = line.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    std::string state = line.substr(0, t1);
    std::string key = line.substr(t1 + 1, t2 - (t1 + 1));
    std::string command = line.substr(t2 + 1);
    while (!command.empty() && (command.back() == '\r' || command.back() == ' '))
      command.pop_back();
    if (state == "status" && key == "key") continue;
    if (CommandInList(command.c_str(), kScriptCommands))
      script->emplace_back(key, command);
    if (CommandInList(command.c_str(), kCompositionCommands))
      composition->emplace_back(key, command);
  }
}

// Kaeriten (返り点) preedit table: input \t result \t [pending] \t [attrs]
static void ParseKaeritenTsv(const std::string& path,
                             std::vector<ShortcutEntry>* kaeriten) {
  kaeriten->clear();
  std::ifstream f(path);
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    size_t t1 = line.find('\t');
    if (t1 == std::string::npos) continue;
    size_t t2 = line.find('\t', t1 + 1);
    std::string input = line.substr(0, t1);
    std::string result = (t2 != std::string::npos)
                             ? line.substr(t1 + 1, t2 - (t1 + 1))
                             : line.substr(t1 + 1);
    while (!result.empty() && (result.back() == '\r' || result.back() == ' '))
      result.pop_back();
    if (!input.empty()) kaeriten->emplace_back(input, result);
  }
}

// Groups (key, command) entries by command/result; keys become comma-separated.
// For script/comp, order follows the given command_order (only listed commands).
// For kaeriten, order is first-occurrence of result (command_order is null).
static void GroupShortcutsByCommand(
    const std::vector<ShortcutEntry>& entries,
    const char* const* command_order,
    std::vector<GroupedShortcutRow>* out) {
  out->clear();
  std::map<std::string, std::vector<std::string>> by_cmd;
  for (const auto& p : entries)
    by_cmd[p.second].push_back(p.first);
  if (command_order) {
    for (; *command_order; ++command_order) {
      auto it = by_cmd.find(*command_order);
      if (it == by_cmd.end()) continue;
      std::string keys;
      for (size_t i = 0; i < it->second.size(); ++i) {
        if (i) keys += ", ";
        keys += it->second[i];
      }
      out->emplace_back(it->first, keys);
    }
  } else {
    for (const auto& p : by_cmd) {
      std::string keys;
      for (size_t i = 0; i < p.second.size(); ++i) {
        if (i) keys += ", ";
        keys += p.second[i];
      }
      out->emplace_back(p.first, keys);
    }
  }
}

// Fallback when TSV files are not installed (e.g. only icons in install dir).
static void FillDefaultScriptShortcuts(std::vector<ShortcutEntry>* script) {
  if (!script->empty()) return;
  const std::pair<const char*, const char*> kDefault[] = {
      {"Ctrl Shift `", "ToggleAlphanumericMode"},
      {"Eisu", "ToggleAlphanumericMode"},
      {"Ctrl Shift 5", "ToggleHiraganaDirect"},
      {"Ctrl Shift ²", "ToggleHiraganaDirect"},
      {"Ctrl Shift F", "ToggleTraditionalKanji"},
      {"RightShift", "ToggleManyoshuHiragana"},
      {"Ctrl i", "ConvertToFullKatakana"},
      {"F7", "ConvertToFullKatakana"},
      {"Ctrl o", "ConvertToHalfWidth"},
      {"F8", "ConvertToHalfWidth"},
      {"Ctrl p", "ConvertToFullAlphanumeric"},
      {"F9", "ConvertToFullAlphanumeric"},
      {"Ctrl u", "ConvertToHiragana"},
      {"F6", "ConvertToHiragana"},
  };
  for (const auto& p : kDefault) script->emplace_back(p.first, p.second);
}

static void FillDefaultCompositionShortcuts(std::vector<ShortcutEntry>* composition) {
  if (!composition->empty()) return;
  const std::pair<const char*, const char*> kDefault[] = {
      {"Enter", "Commit"},
      {"Ctrl Enter", "Commit"},
      {"Ctrl m", "Commit"},
      {"VirtualEnter", "Commit"},
      {"Ctrl 0", "LaunchWordRegisterDialog"},
      {"Ctrl Shift 0", "LaunchWordRegisterDialog"},
      {"Ctrl k", "SegmentWidthShrink"},
      {"Shift Left", "SegmentWidthShrink"},
      {"VirtualLeft", "SegmentWidthShrink"},
      {"Ctrl l", "SegmentWidthExpand"},
      {"Shift Right", "SegmentWidthExpand"},
      {"VirtualRight", "SegmentWidthExpand"},
  };
  for (const auto& p : kDefault) composition->emplace_back(p.first, p.second);
}

static void FillDefaultKaeritenShortcuts(std::vector<ShortcutEntry>* kaeriten) {
  if (!kaeriten->empty()) return;
  const std::pair<const char*, const char*> kDefault[] = {
      {";te", "\xe3\x86\x9d"},   // ㆝
      {";ti", "\xe3\x86\x9e"},   // ㆞
      {";ji", "\xe3\x86\x9f"},   // ㆟
      {";r", "\xe3\x86\x91"},    // ㆑
      {";1", "\xe3\x86\x92"},    // ㆒
      {";2", "\xe3\x86\x93"},    // ㆓
      {";3", "\xe3\x86\x94"},    // ㆔
      {";4", "\xe3\x86\x95"},    // ㆕
      {";u", "\xe3\x86\x96"},    // ㆖
      {";m", "\xe3\x86\x97"},    // ㆗
      {";d", "\xe3\x86\x98"},    // ㆘
      {";k", "\xe3\x86\x99"},    // ㆙
      {";o", "\xe3\x86\x9a"},    // ㆚
      {";h", "\xe3\x86\x9b"},    // ㆛
      {";t", "\xe3\x86\x9c"},    // ㆜
      {";.", "\xe3\x83\xbb"},    // ・
      {";,", "\xe3\x80\x81"},    // 、
  };
  for (const auto& p : kDefault) kaeriten->emplace_back(p.first, p.second);
}

static GtkWidget* g_shortcuts_window = nullptr;
static GtkWidget* g_shortcuts_stack = nullptr;
static GtkLabel* g_shortcuts_title = nullptr;
static GtkWidget* g_shortcuts_prev_btn = nullptr;
static GtkWidget* g_shortcuts_next_btn = nullptr;
static GtkLabel* g_shortcuts_prev_label = nullptr;
static GtkLabel* g_shortcuts_next_label = nullptr;
static GtkListBox* g_shortcuts_script_list = nullptr;
static GtkListBox* g_shortcuts_comp_list = nullptr;
static GtkListBox* g_shortcuts_kaeriten_list = nullptr;
static const char* const kPageNames[] = {"Script", "Composition", "Kaeriten"};
enum { kPageScript = 0, kPageComposition = 1, kPageKaeriten = 2, kPageCount = 3 };
static int g_shortcuts_page = 0;

struct ShortcutsData {
  std::vector<GroupedShortcutRow> script;   // (function name, "key1, key2, ...")
  std::vector<GroupedShortcutRow> composition;
  std::vector<GroupedShortcutRow> kaeriten; // (result char, "input1, input2, ...")
};

static const char* ShortcutsStackPageName(int page) {
  if (page == kPageScript) return "script";
  if (page == kPageComposition) return "composition";
  return "kaeriten";
}

static void ShortcutsPopupRefreshPage(int page, ShortcutsData* data) {
  GtkListBox* list = nullptr;
  const std::vector<GroupedShortcutRow>* rows = nullptr;
  if (page == kPageScript) {
    list = g_shortcuts_script_list;
    rows = &data->script;
  } else if (page == kPageComposition) {
    list = g_shortcuts_comp_list;
    rows = &data->composition;
  } else {
    list = g_shortcuts_kaeriten_list;
    rows = &data->kaeriten;
  }
  if (!list) return;
  GList* children = gtk_container_get_children(GTK_CONTAINER(list));
  for (GList* it = children; it; it = it->next)
    gtk_widget_destroy(GTK_WIDGET(it->data));
  g_list_free(children);
  for (const auto& row_pair : *rows) {
    const std::string& name = row_pair.first;   // function name or result char
    const std::string& keys = row_pair.second;   // "key1, key2, ..."
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(box), 4);
    GtkWidget* name_l = gtk_label_new(name.c_str());
    gtk_label_set_selectable(GTK_LABEL(name_l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(name_l), 0.0f);
    gtk_widget_set_halign(name_l, GTK_ALIGN_START);
    GtkWidget* keys_l = gtk_label_new(keys.c_str());
    gtk_label_set_selectable(GTK_LABEL(keys_l), TRUE);
    gtk_label_set_xalign(GTK_LABEL(keys_l), 0.0f);
    gtk_widget_set_halign(keys_l, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), name_l, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), keys_l, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    gtk_container_add(GTK_CONTAINER(list), row);
  }
  gtk_widget_show_all(GTK_WIDGET(list));
}

static void ShortcutsPopupUpdateHeader() {
  char* bold = g_strdup_printf("<b>%s</b>", kPageNames[g_shortcuts_page]);
  gtk_label_set_markup(g_shortcuts_title, bold);
  g_free(bold);
  int prev_i = (g_shortcuts_page + kPageCount - 1) % kPageCount;
  int next_i = (g_shortcuts_page + 1) % kPageCount;
  gtk_label_set_text(g_shortcuts_prev_label, kPageNames[prev_i]);
  gtk_label_set_text(g_shortcuts_next_label, kPageNames[next_i]);
  gtk_widget_set_sensitive(g_shortcuts_prev_btn, TRUE);
  gtk_widget_set_sensitive(g_shortcuts_next_btn, TRUE);
}

static void OnShortcutsPrev(GtkButton* /*btn*/, gpointer data) {
  g_shortcuts_page = (g_shortcuts_page + kPageCount - 1) % kPageCount;
  gtk_stack_set_visible_child_name(GTK_STACK(g_shortcuts_stack),
                                  ShortcutsStackPageName(g_shortcuts_page));
  ShortcutsPopupUpdateHeader();
  ShortcutsPopupRefreshPage(g_shortcuts_page, static_cast<ShortcutsData*>(data));
}

static void OnShortcutsNext(GtkButton* /*btn*/, gpointer data) {
  g_shortcuts_page = (g_shortcuts_page + 1) % kPageCount;
  gtk_stack_set_visible_child_name(GTK_STACK(g_shortcuts_stack),
                                  ShortcutsStackPageName(g_shortcuts_page));
  ShortcutsPopupUpdateHeader();
  ShortcutsPopupRefreshPage(g_shortcuts_page, static_cast<ShortcutsData*>(data));
}

static void OnShortcutsPopupDestroy(GtkWidget* /*w*/, gpointer data) {
  g_shortcuts_window = nullptr;
  g_shortcuts_stack = nullptr;
  g_shortcuts_title = nullptr;
  g_shortcuts_prev_btn = nullptr;
  g_shortcuts_next_btn = nullptr;
  g_shortcuts_prev_label = nullptr;
  g_shortcuts_next_label = nullptr;
  g_shortcuts_script_list = nullptr;
  g_shortcuts_comp_list = nullptr;
  g_shortcuts_kaeriten_list = nullptr;
  delete static_cast<ShortcutsData*>(data);
}

static void ShowShortcutsPopup() {
  ShortcutsData* data = new ShortcutsData();
  std::vector<ShortcutEntry> script_entries, comp_entries, kaeriten_entries;

  // Load keymap from the TSV selected in settings (or fallback to ms-ime).
  if (g_engine) {
    config::Config config;
    if (g_engine->GetConfig(&config)) {
      config::Config::SessionKeymap keymap = config.session_keymap();
      if (keymap == config::Config::CUSTOM && config.has_custom_keymap_table() &&
          !config.custom_keymap_table().empty()) {
        ParseKeymapFromString(config.custom_keymap_table(), &script_entries,
                             &comp_entries);
      } else {
        std::string filename = KeymapFilenameFromSessionKeymap(keymap);
        if (!filename.empty())
          ParseKeymapTsv(GetIconPath(filename), &script_entries, &comp_entries);
      }
    }
  }
  if (script_entries.empty() && comp_entries.empty())
    ParseKeymapTsv(GetIconPath("ms-ime.tsv"), &script_entries, &comp_entries);
  FillDefaultScriptShortcuts(&script_entries);
  FillDefaultCompositionShortcuts(&comp_entries);

  ParseKaeritenTsv(GetIconPath("kaeriten.tsv"), &kaeriten_entries);
  FillDefaultKaeritenShortcuts(&kaeriten_entries);

  GroupShortcutsByCommand(script_entries, kScriptCommands, &data->script);
  GroupShortcutsByCommand(comp_entries, kCompositionCommands, &data->composition);
  GroupShortcutsByCommand(kaeriten_entries, nullptr, &data->kaeriten);

  if (g_shortcuts_window) {
    gtk_widget_destroy(g_shortcuts_window);
    g_shortcuts_window = nullptr;
  }
  g_shortcuts_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(g_shortcuts_window), "Keyboard shortcuts");
  gtk_window_set_resizable(GTK_WINDOW(g_shortcuts_window), TRUE);
  gtk_window_set_default_size(GTK_WINDOW(g_shortcuts_window), 420, 380);
  gtk_window_set_position(GTK_WINDOW(g_shortcuts_window), GTK_WIN_POS_CENTER);
  GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_container_set_border_width(GTK_CONTAINER(content), 12);
  gtk_container_add(GTK_CONTAINER(g_shortcuts_window), content);
  GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  g_shortcuts_prev_btn = gtk_button_new();
  GtkWidget* prev_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_container_add(GTK_CONTAINER(g_shortcuts_prev_btn), prev_box);
  gtk_box_pack_start(GTK_BOX(prev_box), gtk_label_new("←"), FALSE, FALSE, 0);
  g_shortcuts_prev_label = GTK_LABEL(gtk_label_new(""));
  gtk_box_pack_start(GTK_BOX(prev_box), GTK_WIDGET(g_shortcuts_prev_label), FALSE, FALSE, 0);
  g_shortcuts_next_btn = gtk_button_new();
  GtkWidget* next_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_container_add(GTK_CONTAINER(g_shortcuts_next_btn), next_box);
  g_shortcuts_next_label = GTK_LABEL(gtk_label_new(""));
  gtk_box_pack_start(GTK_BOX(next_box), GTK_WIDGET(g_shortcuts_next_label), FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(next_box), gtk_label_new("→"), FALSE, FALSE, 0);
  g_shortcuts_title = GTK_LABEL(gtk_label_new(""));
  gtk_box_pack_start(GTK_BOX(header), g_shortcuts_prev_btn, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(header), GTK_WIDGET(g_shortcuts_title), TRUE, TRUE, 0);
  gtk_box_pack_end(GTK_BOX(header), g_shortcuts_next_btn, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(content), header, FALSE, FALSE, 0);
  g_shortcuts_stack = gtk_stack_new();
  GtkWidget* script_frame = gtk_frame_new(nullptr);
  GtkWidget* script_sw = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(script_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(script_sw), 260);
  g_shortcuts_script_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_container_add(GTK_CONTAINER(script_sw), GTK_WIDGET(g_shortcuts_script_list));
  gtk_container_add(GTK_CONTAINER(script_frame), script_sw);
  gtk_stack_add_titled(GTK_STACK(g_shortcuts_stack), script_frame, "script", "Script");
  GtkWidget* comp_frame = gtk_frame_new(nullptr);
  GtkWidget* comp_sw = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(comp_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(comp_sw), 260);
  g_shortcuts_comp_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_container_add(GTK_CONTAINER(comp_sw), GTK_WIDGET(g_shortcuts_comp_list));
  gtk_container_add(GTK_CONTAINER(comp_frame), comp_sw);
  gtk_stack_add_titled(GTK_STACK(g_shortcuts_stack), comp_frame, "composition", "Composition");
  GtkWidget* kaeriten_frame = gtk_frame_new(nullptr);
  GtkWidget* kaeriten_sw = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(kaeriten_sw), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(kaeriten_sw), 260);
  g_shortcuts_kaeriten_list = GTK_LIST_BOX(gtk_list_box_new());
  gtk_container_add(GTK_CONTAINER(kaeriten_sw), GTK_WIDGET(g_shortcuts_kaeriten_list));
  gtk_container_add(GTK_CONTAINER(kaeriten_frame), kaeriten_sw);
  gtk_stack_add_titled(GTK_STACK(g_shortcuts_stack), kaeriten_frame, "kaeriten", "Kaeriten");
  gtk_box_pack_start(GTK_BOX(content), g_shortcuts_stack, TRUE, TRUE, 0);
  g_shortcuts_page = 0;
  gtk_stack_set_visible_child_name(GTK_STACK(g_shortcuts_stack), "script");
  ShortcutsPopupUpdateHeader();
  g_signal_connect(g_shortcuts_prev_btn, "clicked", G_CALLBACK(OnShortcutsPrev), data);
  g_signal_connect(g_shortcuts_next_btn, "clicked", G_CALLBACK(OnShortcutsNext), data);
  g_signal_connect(g_shortcuts_window, "destroy", G_CALLBACK(OnShortcutsPopupDestroy), data);
  ShortcutsPopupRefreshPage(kPageScript, data);
  ShortcutsPopupRefreshPage(kPageComposition, data);
  ShortcutsPopupRefreshPage(kPageKaeriten, data);
  gtk_widget_show_all(g_shortcuts_window);
}

void OnShortcutsClicked(GtkWidget* /*widget*/, gpointer /*data*/) {
  ShowShortcutsPopup();
}

void EnsureToolbarCreated() {
  if (g_toolbar_window) return;
  MaybeForceX11OnGnomeWayland();
  if (!gtk_init_check(nullptr, nullptr)) return;

  g_toolbar_dark_theme = IsSystemThemeDark();
  EnsureToolbarCSS(g_toolbar_dark_theme);
  RegisterToolbarThemeObserver();

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

  GtkWidget* logo_img = CreateLogoImage(g_toolbar_dark_theme);
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
  bool light = !g_toolbar_dark_theme;
  GdkPixbuf* mode_pixbuf =
      LoadSvgIcon(GetModeIndicatorIconName(commands::HIRAGANA, light),
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
  SetButtonIcon(GTK_BUTTON(g_trad_btn),
                g_toolbar_dark_theme ? "toolbar_shin_dark.svg" : "toolbar_shin_light.svg");
  g_signal_connect(g_trad_btn, "clicked", G_CALLBACK(OnTradClicked), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_trad_btn, FALSE, FALSE, 0);

  g_odoriji_btn = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(g_odoriji_btn), GTK_RELIEF_NONE);
  gtk_widget_set_focus_on_click(g_odoriji_btn, FALSE);
  gtk_widget_set_can_focus(g_odoriji_btn, FALSE);
  gtk_widget_set_size_request(g_odoriji_btn, 32, 32);
  gtk_widget_set_name(g_odoriji_btn, "marinamozc-odoriji-btn");
  gtk_button_set_label(GTK_BUTTON(g_odoriji_btn), "");
  SetButtonIcon(GTK_BUTTON(g_odoriji_btn),
                g_toolbar_dark_theme ? "toolbar_marks_dark.svg" : "toolbar_marks_light.svg");
  g_signal_connect(g_odoriji_btn, "clicked", G_CALLBACK(OnOdorijiClicked), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_odoriji_btn, FALSE, FALSE, 0);

  // Dict button: left click = Add Word, right click = Dictionary tool.
  const char* dict_icon = g_toolbar_dark_theme ? "toolbar_dict_dark.svg" : "toolbar_dict_light.svg";
  GdkPixbuf* dict_pixbuf = LoadSvgIcon(dict_icon, kIconSize, kIconSize);
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

  g_shortcuts_btn = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(g_shortcuts_btn), GTK_RELIEF_NONE);
  gtk_widget_set_focus_on_click(g_shortcuts_btn, FALSE);
  gtk_widget_set_can_focus(g_shortcuts_btn, FALSE);
  gtk_widget_set_size_request(g_shortcuts_btn, 32, 32);
  gtk_widget_set_name(g_shortcuts_btn, "marinamozc-shortcuts-btn");
  gtk_button_set_label(GTK_BUTTON(g_shortcuts_btn), "");
  SetButtonIcon(GTK_BUTTON(g_shortcuts_btn),
                g_toolbar_dark_theme ? "toolbar_shortcuts_dark.svg" : "toolbar_shortcuts_light.svg");
  g_signal_connect(g_shortcuts_btn, "clicked", G_CALLBACK(OnShortcutsClicked), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_shortcuts_btn, FALSE, FALSE, 0);

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
    const char* kyu = g_toolbar_dark_theme ? "toolbar_kyu_dark.svg" : "toolbar_kyu_light.svg";
    const char* shin = g_toolbar_dark_theme ? "toolbar_shin_dark.svg" : "toolbar_shin_light.svg";
    SetButtonIcon(GTK_BUTTON(g_trad_btn), use_trad ? kyu : shin);
  }
  if (output.has_status() && g_mode_indicator_image) {
    commands::CompositionMode mode = output.status().activated()
                                         ? output.status().mode()
                                         : commands::DIRECT;
    g_last_toolbar_mode = mode;
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
