// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Odoriji (iteration marks) palette: candidate-window UI for selecting
// 々ゝゞヽヾ〻〱〲. Isolated module to keep Session changes minimal.

#ifndef MOZC_SESSION_ODORIJI_PALETTE_H_
#define MOZC_SESSION_ODORIJI_PALETTE_H_

#include <string>

#include "protocol/commands.pb.h"

namespace mozc {
namespace session {

class OdorijiPalette {
 public:
  // Number of odoriji entries (0..7).
  static constexpr size_t kCount = 8;

  // Return UTF-8 string for odoriji at index [0, kCount-1]. Returns first
  // character (々) if index out of range.
  static const char* GetCharacter(size_t index);

  // Show palette: set *visible = true, *focused_index = 0.
  static void Show(bool* visible, int* focused_index);

  // Handle key when palette is visible. Updates *visible and *focused_index
  // as needed. If session_default_index is non-null, sets it to the chosen
  // index when committing (Enter or 1-8). If commit_result is non-null and the
  // user commits (Enter or 1-8), sets *commit_result to the chosen character
  // (caller must set it on output after Output() since PopOutput overwrites).
  // Returns true if the key was consumed (caller should Output and return).
  static bool HandleKey(const commands::KeyEvent& key,
                        commands::Command* command,
                        bool* visible,
                        int* focused_index,
                        int* session_default_index = nullptr,
                        std::string* commit_result = nullptr);

  // If palette is visible, overwrite output preedit and candidate_window.
  static void OverlayOutput(commands::Output* output, int focused_index);

  // If palette is visible and command has SUBMIT_CANDIDATE with id in [0,7],
  // set *commit_result to the chosen character, *visible = false; if
  // session_default_index non-null set it to id. Caller must set commit_result
  // on output after Output() since PopOutput overwrites. Return true.
  // Otherwise return false.
  static bool TryCommitCandidate(commands::Command* command, bool* visible,
                                  int* session_default_index = nullptr,
                                  std::string* commit_result = nullptr);
};

}  // namespace session
}  // namespace mozc

#endif  // MOZC_SESSION_ODORIJI_PALETTE_H_
