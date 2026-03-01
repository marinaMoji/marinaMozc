// Copyright 2010-2021, Google Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of Google Inc. nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "rewriter/opencc_rewriter.h"

#include <cstdlib>
#include <mutex>
#include <string>

#include "absl/log/check.h"
#include "converter/candidate.h"
#include "converter/segments.h"
#include "protocol/config.pb.h"
#include "request/conversion_request.h"
#include "rewriter/rewriter_interface.h"

#ifdef MOZC_USE_OPENCC
#include <opencc/Config.hpp>
#include <opencc/Converter.hpp>
#endif  // MOZC_USE_OPENCC

namespace mozc {

namespace {

#ifdef MOZC_USE_OPENCC
std::string GetOpenccConfigPath() {
  const char* env = std::getenv("OPENCC_DATA_DIR");
  if (env && env[0] != '\0') {
    return std::string(env) + "/jp2t.json";
  }
  return "/usr/share/opencc/jp2t.json";
}

opencc::ConverterPtr GetConverter() {
  static opencc::ConverterPtr converter;
  static std::once_flag once;
  std::call_once(once, []() {
    try {
      opencc::Config config;
      converter = config.NewFromFile(GetOpenccConfigPath());
    } catch (...) {
      converter = nullptr;
    }
  });
  return converter;
}

bool ConvertWithOpencc(const std::string& input, std::string* output) {
  opencc::ConverterPtr converter = GetConverter();
  if (!converter || input.empty()) {
    return false;
  }
  try {
    *output = converter->Convert(input);
    return !output->empty();
  } catch (...) {
    return false;
  }
}
#else   // !MOZC_USE_OPENCC
bool ConvertWithOpencc(const std::string& input, std::string* output) {
  (void)input;
  (void)output;
  return false;
}
#endif  // MOZC_USE_OPENCC

}  // namespace

int OpenccRewriter::capability(const ConversionRequest& request) const {
  if (!request.config().use_traditional_kanji()) {
    return RewriterInterface::NOT_AVAILABLE;
  }
  return RewriterInterface::ALL;
}

bool OpenccRewriter::Rewrite(const ConversionRequest& request,
                              Segments* segments) const {
  if (!request.config().use_traditional_kanji()) {
    return false;
  }

  bool modified = false;
  for (size_t i = 0; i < segments->conversion_segments_size(); ++i) {
    converter::Segment* seg = segments->mutable_conversion_segment(i);
    if (!seg) continue;

    for (size_t j = 0; j < seg->candidates_size(); ++j) {
      converter::Candidate* cand = seg->mutable_candidate(j);
      if (!cand) continue;

      std::string converted_value;
      if (!cand->value.empty() &&
          ConvertWithOpencc(cand->value, &converted_value)) {
        cand->value = std::move(converted_value);
        modified = true;
      }
      if (!cand->content_value.empty() &&
          ConvertWithOpencc(cand->content_value, &converted_value)) {
        cand->content_value = std::move(converted_value);
        modified = true;
      }
    }
  }
  return modified;
}

}  // namespace mozc
