// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// marinaMozc: GTK toolbar (schema, shin/kyu, odoriji, half/full).
// Build with MOZC_HAVE_GTK_TOOLBAR and link GTK to enable.

#include "unix/ibus/mozc_toolbar.h"

#include "unix/ibus/mozc_engine.h"
#include "protocol/commands.pb.h"

#ifdef MOZC_HAVE_GTK_TOOLBAR
#include <gtk/gtk.h>
#endif

namespace mozc {
namespace ibus {

#ifdef MOZC_HAVE_GTK_TOOLBAR

namespace {

GtkWidget* g_toolbar_window = nullptr;
GtkWidget* g_toolbar_box = nullptr;
GtkWidget* g_schema_label = nullptr;
GtkWidget* g_trad_toggle = nullptr;
GtkWidget* g_half_full_btn = nullptr;
MozcEngine* g_engine = nullptr;

// Panel labels for composition mode (match property_handler).
const char* ModeLabel(commands::CompositionMode mode) {
  switch (mode) {
    case commands::DIRECT:
      return "A";
    case commands::HIRAGANA:
      return "\u3042";  // あ
    case commands::FULL_KATAKANA:
      return "\u30a2";  // ア
    case commands::HALF_ASCII:
      return "_A";
    case commands::FULL_ASCII:
      return "\uff21";  // Ａ (full-width A)
    case commands::HALF_KATAKANA:
      return "_\uFF71";  // _ｱ
    default:
      return "\u3042";
  }
}

bool IsHalfFullRelevant(commands::CompositionMode mode) {
  return mode == commands::HALF_ASCII || mode == commands::FULL_ASCII;
}

void OnTraditionalKanjiToggled(GtkToggleButton* btn, gpointer /*data*/) {
  if (g_engine == nullptr) {
    return;
  }
  g_signal_handlers_block_by_func(btn, reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
  g_engine->SendToolbarSessionCommand(
      commands::SessionCommand::TOGGLE_TRADITIONAL_KANJI);
  g_signal_handlers_unblock_by_func(btn, reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
}

void OnOdorijiClicked(GtkWidget* /*widget*/, gpointer /*data*/) {
  if (g_engine != nullptr) {
    g_engine->SendToolbarSessionCommand(
        commands::SessionCommand::SHOW_ODORIJI_PALETTE);
  }
}

void OnHalfFullClicked(GtkWidget* /*widget*/, gpointer /*data*/) {
  if (g_engine != nullptr) {
    g_engine->SendToolbarSessionCommand(
        commands::SessionCommand::TOGGLE_FULL_HALF_WIDTH);
  }
}

void EnsureToolbarCreated() {
  if (g_toolbar_window != nullptr) {
    return;
  }
  if (!gtk_init_check()) {
    return;
  }
  g_toolbar_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(g_toolbar_window), "marinaMozc");
  gtk_window_set_decorated(GTK_WINDOW(g_toolbar_window), TRUE);
  gtk_window_set_resizable(GTK_WINDOW(g_toolbar_window), FALSE);

  g_toolbar_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_margin_start(g_toolbar_box, 8);
  gtk_widget_set_margin_end(g_toolbar_box, 8);
  gtk_widget_set_margin_top(g_toolbar_box, 6);
  gtk_widget_set_margin_bottom(g_toolbar_box, 6);
  gtk_window_set_child(GTK_WINDOW(g_toolbar_window), g_toolbar_box);

  // Schema (input mode) label
  g_schema_label = gtk_label_new("\u3042");
  gtk_widget_set_margin_end(g_schema_label, 4);
  gtk_box_append(GTK_BOX(g_toolbar_box), g_schema_label);

  // Shin/Kyū toggle
  g_trad_toggle = gtk_toggle_button_new_with_label("Shin/Ky\u016b");
  g_signal_connect(g_trad_toggle, "toggled", G_CALLBACK(OnTraditionalKanjiToggled), nullptr);
  gtk_box_append(GTK_BOX(g_toolbar_box), g_trad_toggle);

  // Odoriji button
  GtkWidget* btn_odoriji = gtk_button_new_with_label("\u30fb\u30fb Odoriji");
  g_signal_connect(btn_odoriji, "clicked", G_CALLBACK(OnOdorijiClicked), nullptr);
  gtk_box_append(GTK_BOX(g_toolbar_box), btn_odoriji);

  // Half/Full toggle button
  g_half_full_btn = gtk_button_new_with_label("Half/Full");
  g_signal_connect(g_half_full_btn, "clicked", G_CALLBACK(OnHalfFullClicked), nullptr);
  gtk_box_append(GTK_BOX(g_toolbar_box), g_half_full_btn);
}

void ApplyOutputToToolbar(const commands::Output& output) {
  if (g_schema_label == nullptr || g_trad_toggle == nullptr || g_half_full_btn == nullptr) {
    return;
  }
  if (output.has_status()) {
    const commands::CompositionMode mode = output.status().mode();
    gtk_label_set_text(GTK_LABEL(g_schema_label), ModeLabel(mode));
    if (IsHalfFullRelevant(mode)) {
      gtk_button_set_label(GTK_BUTTON(g_half_full_btn),
                           mode == commands::HALF_ASCII ? "Half" : "Full");
    } else {
      gtk_button_set_label(GTK_BUTTON(g_half_full_btn), "Half/Full");
    }
  }
  if (output.has_config()) {
    const bool use_trad = output.config().use_traditional_kanji();
    g_signal_handlers_block_by_func(g_trad_toggle, reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_trad_toggle), use_trad);
    g_signal_handlers_unblock_by_func(g_trad_toggle, reinterpret_cast<gpointer>(OnTraditionalKanjiToggled), nullptr);
  }
}

}  // namespace

void MozcToolbarShow(MozcEngine* engine) {
  if (engine == nullptr) {
    return;
  }
  g_engine = engine;
  EnsureToolbarCreated();
  gtk_widget_show(g_toolbar_window);
}

void MozcToolbarHide() {
  if (g_toolbar_window != nullptr) {
    gtk_widget_hide(g_toolbar_window);
  }
}

void MozcToolbarUpdate(const commands::Output& output) {
  if (g_toolbar_window == nullptr || !gtk_widget_get_visible(g_toolbar_window)) {
    return;
  }
  ApplyOutputToToolbar(output);
}

bool MozcToolbarAvailable() { return true; }

#else  // !MOZC_HAVE_GTK_TOOLBAR

void MozcToolbarShow(MozcEngine* /*engine*/) {}

void MozcToolbarHide() {}

void MozcToolbarUpdate(const commands::Output& /*output*/) {}

bool MozcToolbarAvailable() { return false; }

#endif  // MOZC_HAVE_GTK_TOOLBAR

}  // namespace ibus
}  // namespace mozc
