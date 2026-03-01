// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Odoriji (iteration marks) palette implementation. All odoriji-specific
// logic lives here so Session only needs thin delegation.

#include "session/odoriji_palette.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "protocol/commands.pb.h"

namespace mozc {
namespace session {

namespace {

// Odoriji (iteration marks) for the palette: 々ゝゞヽヾ〻〱〲 (UTF-8)
constexpr const char* kOdorijiChars[] = {
    "\xe3\x80\x85",  // 々 U+3005
    "\xe3\x82\x9d",  // ゝ U+309D
    "\xe3\x82\x9e",  // ゞ U+309E
    "\xe3\x83\xbd",  // ヽ U+30FD
    "\xe3\x83\xbe",  // ヾ U+30FE
    "\xe3\x80\xbb",  // 〻 U+303B
    "\xe3\x80\xb1",  // 〱 U+3031
    "\xe3\x80\xb2",  // 〲 U+3032
};

void FillCandidateWindow(commands::CandidateWindow* candidate_window,
                         int focused_index) {
  candidate_window->Clear();
  candidate_window->set_focused_index(static_cast<uint32_t>(focused_index));
  candidate_window->set_size(kCount);
  candidate_window->set_position(0);
  candidate_window->set_category(commands::CONVERSION);
  candidate_window->set_page_size(kCount);
  for (size_t i = 0; i < kCount; ++i) {
    auto* c = candidate_window->add_candidate();
    c->set_index(static_cast<uint32_t>(i));
    c->set_value(kOdorijiChars[i]);
    c->set_id(static_cast<int32_t>(i));
    commands::Annotation* ann = c->mutable_annotation();
    ann->set_shortcut(std::to_string(i + 1));
  }
}

}  // namespace

const char* OdorijiPalette::GetCharacter(size_t index) {
  if (index >= kCount) index = 0;
  return kOdorijiChars[index];
}

void OdorijiPalette::Show(bool* visible, int* focused_index) {
  if (visible) *visible = true;
  if (focused_index) *focused_index = 0;
}

bool OdorijiPalette::HandleKey(const commands::KeyEvent& key,
                                commands::Command* command,
                                bool* visible,
                                int* focused_index,
                                int* session_default_index) {
  if (!visible || !focused_index || !command) return false;
  if (!*visible) return false;

  if (key.has_special_key()) {
    if (key.special_key() == commands::KeyEvent::ESCAPE) {
      *visible = false;
      return true;
    }
    if (key.special_key() == commands::KeyEvent::ENTER ||
        key.special_key() == commands::KeyEvent::VIRTUAL_ENTER) {
      command->mutable_output()->set_consumed(true);
      commands::Result* result = command->mutable_output()->mutable_result();
      result->set_type(commands::Result::STRING);
      result->set_value(kOdorijiChars[*focused_index]);
      if (session_default_index) *session_default_index = *focused_index;
      *visible = false;
      return true;
    }
    if (key.special_key() == commands::KeyEvent::UP ||
        key.special_key() == commands::KeyEvent::VIRTUAL_UP) {
      *focused_index =
          static_cast<int>((*focused_index + kCount - 1) % kCount);
      return true;
    }
    if (key.special_key() == commands::KeyEvent::DOWN ||
        key.special_key() == commands::KeyEvent::VIRTUAL_DOWN) {
      *focused_index = static_cast<int>((*focused_index + 1) % kCount);
      return true;
    }
  }
  if (key.has_key_code()) {
    const uint32_t k = key.key_code();
    if (k >= 0x31 && k <= 0x38) {
      int id = static_cast<int>(k - 0x31);
      command->mutable_output()->set_consumed(true);
      commands::Result* result = command->mutable_output()->mutable_result();
      result->set_type(commands::Result::STRING);
      result->set_value(kOdorijiChars[id]);
      if (session_default_index) *session_default_index = id;
      *visible = false;
      return true;
    }
  }
  return false;
}

void OdorijiPalette::OverlayOutput(commands::Output* output, int focused_index) {
  if (!output) return;
  output->clear_preedit();
  commands::Preedit* preedit = output->mutable_preedit();
  auto* segment = preedit->add_segment();
  segment->set_value(
      "\xe9\x80\x90\xe3\x82\x8a\xe8\xbf\x94\xe3\x81\x97\xe8\xa8\x98\xe5\x8f\xb7");  // 繰り返し記号
  segment->set_annotation(commands::Preedit::Segment::HIGHLIGHT);
  segment->set_value_length(6);
  preedit->set_cursor_position(0);
  FillCandidateWindow(output->mutable_candidate_window(), focused_index);
}

bool OdorijiPalette::TryCommitCandidate(commands::Command* command,
                                        bool* visible,
                                        int* session_default_index) {
  if (!visible || !*visible || !command->input().has_command() ||
      !command->input().command().has_id()) {
    return false;
  }
  const int id = command->input().command().id();
  if (id < 0 || id >= static_cast<int>(kCount)) return false;
  command->mutable_output()->set_consumed(true);
  commands::Result* result = command->mutable_output()->mutable_result();
  result->set_type(commands::Result::STRING);
  result->set_value(kOdorijiChars[id]);
  if (session_default_index) *session_default_index = id;
  *visible = false;
  return true;
}

}  // namespace session
}  // namespace mozc
