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
GtkWidget* g_logo_cell = nullptr;
GtkWidget* g_trad_toggle = nullptr;
GtkWidget* g_odoriji_btn = nullptr;
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

// marinaMoji-style: read toolbar_hide_on_focus_loss from ~/.config/ibus/marinamozc/toolbar.conf
static bool LoadHideOnFocusLossPreference() {
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
  return (widget == g_trad_toggle || widget == g_odoriji_btn ||
          gtk_widget_is_ancestor(widget, g_trad_toggle) ||
          gtk_widget_is_ancestor(widget, g_odoriji_btn));
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

// marinaMoji does not use override-redirect; TOPLEVEL + DOCK is enough.
static void OnRealize(GtkWidget* /*w*/, gpointer /*data*/) {}

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
  if (target && IsButtonWidget(target)) return FALSE;

  g_toolbar_user_moved = true;  // marinaMoji-style: restore position on next show
  g_drag_active = true;
  g_drag_offset_x = static_cast<int>(event->x_root) - g_window_x;
  g_drag_offset_y = static_cast<int>(event->y_root) - g_window_y;
  gtk_window_begin_move_drag(GTK_WINDOW(g_toolbar_window), event->button,
                             static_cast<gint>(event->x_root),
                             static_cast<gint>(event->y_root),
                             event->time);
  return TRUE;
}

static gboolean OnButtonRelease(GtkWidget* /*widget*/, GdkEventButton* event,
                                 gpointer /*data*/) {
  if (event->button == 1 && g_drag_active) {
    g_drag_active = false;
    gtk_window_get_position(GTK_WINDOW(g_toolbar_window), &g_window_x, &g_window_y);
  }
  return FALSE;
}

static gboolean OnMotion(GtkWidget* /*widget*/, GdkEventMotion* event,
                         gpointer /*data*/) {
  if (g_drag_active) {
    gtk_window_get_position(GTK_WINDOW(g_toolbar_window), &g_window_x, &g_window_y);
  }
  return FALSE;
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
      "#marinamozc-trad-btn, #marinamozc-trad-btn:hover,"
      "#marinamozc-trad-btn:checked, #marinamozc-trad-btn:active,"
      "#marinamozc-odoriji-btn, #marinamozc-odoriji-btn:hover,"
      "#marinamozc-odoriji-btn:active {"
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

void OnTraditionalKanjiToggled(GtkToggleButton* btn, gpointer /*data*/) {
  if (g_engine) {
    g_signal_handlers_block_by_func(btn,
        reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
    g_engine->SendToolbarSessionCommand(
        commands::SessionCommand::TOGGLE_TRADITIONAL_KANJI);
    g_signal_handlers_unblock_by_func(btn,
        reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
  }
}

void OnOdorijiClicked(GtkWidget* /*widget*/, gpointer /*data*/) {
  if (g_engine) {
    g_engine->SendToolbarSessionCommand(
        commands::SessionCommand::SHOW_ODORIJI_PALETTE);
  }
}

void EnsureToolbarCreated() {
  if (g_toolbar_window) return;
  if (!gtk_init_check(nullptr, nullptr)) return;

  EnsureToolbarCSS();

  // TOPLEVEL + UTILITY so compositor does not show the toolbar in the dock/taskbar.
  // keep_above + accept_focus/focus_on_map/can_focus false; draggable via gtk_window_begin_move_drag.
  g_toolbar_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(g_toolbar_window), "marinaMozc");
  gtk_window_set_decorated(GTK_WINDOW(g_toolbar_window), FALSE);
  gtk_window_set_resizable(GTK_WINDOW(g_toolbar_window), FALSE);
  gtk_window_set_keep_above(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_skip_taskbar_hint(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_skip_pager_hint(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_type_hint(GTK_WINDOW(g_toolbar_window), GDK_WINDOW_TYPE_HINT_UTILITY);
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

  GtkWidget* lead = gtk_event_box_new();
  gtk_widget_set_size_request(lead, 8, kToolbarHeight);
  gtk_widget_add_events(lead, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(lead, "button-press-event", G_CALLBACK(OnButtonPress), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), lead, FALSE, FALSE, 0);

  GtkWidget* logo_img = CreateLogoImage();
  gtk_widget_set_halign(logo_img, GTK_ALIGN_CENTER);
  gtk_widget_set_valign(logo_img, GTK_ALIGN_CENTER);
  g_logo_cell = gtk_event_box_new();
  gtk_widget_set_size_request(g_logo_cell, kToolbarLogoWidth, 32);
  gtk_widget_set_can_focus(g_logo_cell, FALSE);
  gtk_widget_add_events(g_logo_cell, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(g_logo_cell, "button-press-event", G_CALLBACK(OnButtonPress), nullptr);
  gtk_container_add(GTK_CONTAINER(g_logo_cell), logo_img);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_logo_cell, FALSE, FALSE, 0);

  g_trad_toggle = gtk_toggle_button_new();
  gtk_button_set_relief(GTK_BUTTON(g_trad_toggle), GTK_RELIEF_NONE);
  gtk_widget_set_focus_on_click(g_trad_toggle, FALSE);
  gtk_widget_set_can_focus(g_trad_toggle, FALSE);
  gtk_widget_set_size_request(g_trad_toggle, 32, 32);
  gtk_widget_set_name(g_trad_toggle, "marinamozc-trad-btn");
  gtk_button_set_label(GTK_BUTTON(g_trad_toggle), "");
  SetButtonIcon(GTK_BUTTON(g_trad_toggle), "toolbar_shin_light.svg");
  g_signal_connect(g_trad_toggle, "toggled", G_CALLBACK(OnTraditionalKanjiToggled), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), g_trad_toggle, FALSE, FALSE, 0);

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

  GtkWidget* trail = gtk_event_box_new();
  gtk_widget_set_size_request(trail, 8, kToolbarHeight);
  gtk_widget_add_events(trail, GDK_BUTTON_PRESS_MASK);
  g_signal_connect(trail, "button-press-event", G_CALLBACK(OnButtonPress), nullptr);
  gtk_box_pack_start(GTK_BOX(g_toolbar_box), trail, FALSE, FALSE, 0);
}

void ApplyOutputToToolbar(const commands::Output& output) {
  if (!g_trad_toggle || !g_odoriji_btn) return;
  if (output.has_config()) {
    bool use_trad = output.config().use_traditional_kanji();
    g_signal_handlers_block_by_func(g_trad_toggle,
        reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_trad_toggle), use_trad ? TRUE : FALSE);
    SetButtonIcon(GTK_BUTTON(g_trad_toggle),
                  use_trad ? "toolbar_kyu_light.svg" : "toolbar_shin_light.svg");
    g_signal_handlers_unblock_by_func(g_trad_toggle,
        reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
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

#else

void MozcToolbarShow(MozcEngine* /*engine*/) {}
void MozcToolbarHide() {}
bool MozcToolbarShouldHideOnFocusLoss() { return false; }
void MozcToolbarScheduleHideDelayed(unsigned int /*delay_ms*/) {}
void MozcToolbarUpdate(const commands::Output& /*output*/) {}
bool MozcToolbarAvailable() { return false; }

#endif  // MOZC_HAVE_GTK_TOOLBAR

}  // namespace ibus
}  // namespace mozc
