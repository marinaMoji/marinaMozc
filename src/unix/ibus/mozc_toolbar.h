// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// marinaMozc: GTK toolbar (schema, shin/kyu, odoriji, half/full).
// Optional: only built when GTK is available (MOZC_HAVE_GTK_TOOLBAR).

#ifndef MOZC_UNIX_IBUS_MOZC_TOOLBAR_H_
#define MOZC_UNIX_IBUS_MOZC_TOOLBAR_H_

#include "protocol/commands.pb.h"

namespace mozc {
namespace ibus {

class MozcEngine;

// Ensures the toolbar is created and shows it. Stores |engine| for button
// callbacks. Call from FocusIn. No-op if GTK toolbar is disabled.
void MozcToolbarShow(MozcEngine* engine);

// Hides the toolbar. Call from FocusOut or when disabling.
void MozcToolbarHide();

// marinaMoji-style: true if toolbar should auto-hide on focus loss (false on Wayland).
bool MozcToolbarShouldHideOnFocusLoss();

// marinaMoji-style: schedule hide after delay_ms; cancelled if show is requested first.
void MozcToolbarScheduleHideDelayed(unsigned int delay_ms);

// Updates toolbar state from engine output (schema mode, shin/kyu, half/full).
// Call after UpdateAll when the toolbar is visible.
void MozcToolbarUpdate(const commands::Output& output);

// Returns true if the toolbar module is available (GTK linked).
bool MozcToolbarAvailable();

// Load saved toolbar visibility. True = on, false = off. Default true (on) for first install.
bool MozcToolbarLoadVisiblePreference();

// Save toolbar visibility so it persists across restarts (off until user toggles on again).
void MozcToolbarSaveVisiblePreference(bool visible);

}  // namespace ibus
}  // namespace mozc

#endif  // MOZC_UNIX_IBUS_MOZC_TOOLBAR_H_
