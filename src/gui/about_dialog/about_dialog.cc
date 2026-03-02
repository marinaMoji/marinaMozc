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

#include "gui/about_dialog/about_dialog.h"

#include <QtGui>
#include <algorithm>
#include <memory>
#include <string>

#include "base/file_util.h"
#include "base/process.h"
#include "base/run_level.h"
#include "base/system_util.h"
#include "base/version.h"
#include "gui/base/util.h"

#ifdef MARINAMOZC
#include <QImageReader>
#endif  // MARINAMOZC

namespace mozc {
namespace gui {
namespace {

void defaultLinkActivated(const QString &str) {
  QByteArray utf8 = str.toUtf8();
  Process::OpenBrowser(std::string(utf8.data(), utf8.length()));
}

inline void Replace(QString &str, const char pattern[], const char repl[]) {
  str.replace(QLatin1String(pattern), QLatin1String(repl));
}

inline void Replace(QString &str, const char pattern[], const QString &repl) {
  str.replace(QLatin1String(pattern), repl);
}

QString ReplaceString(const QString &str) {
  QString replaced(str);
  Replace(replaced, "[ProductName]", GuiUtil::ProductName());

#ifdef MARINAMOZC
  Replace(replaced, "[ProductUrl]", "https://github.com/marinaMoji/marinaMozc");
  Replace(replaced, "[ForumUrl]", "https://github.com/marinaMoji/marinaMozc/issues");
  Replace(replaced, "[ForumName]", QObject::tr("issues"));
#elif defined(GOOGLE_JAPANESE_INPUT_BUILD)
  Replace(replaced, "[ProductUrl]", "https://www.google.co.jp/ime/");
  Replace(replaced, "[ForumUrl]",
          "https://support.google.com/gboard/community?hl=ja");
  Replace(replaced, "[ForumName]", QObject::tr("product forum"));
#else  // GOOGLE_JAPANESE_INPUT_BUILD
  Replace(replaced, "[ProductUrl]", "https://github.com/google/mozc");
  Replace(replaced, "[ForumUrl]", "https://github.com/google/mozc/issues");
  Replace(replaced, "[ForumName]", QObject::tr("issues"));
#endif  // MARINAMOZC / GOOGLE_JAPANESE_INPUT_BUILD

  const std::string credit_filepath =
      FileUtil::JoinPath(SystemUtil::GetDocumentDirectory(), "credits_en.html");
  Replace(replaced, "credits_en.html", credit_filepath.c_str());

  return replaced;
}

void SetLabelText(QLabel *label) {
  label->setText(ReplaceString(label->text()));
}
}  // namespace

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent), callback_(nullptr) {
  setupUi(this);
  setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
  setWindowModality(Qt::NonModal);
  const QColor white(255, 255, 255);
  QPalette window_palette;
  window_palette.setColor(QPalette::Window, white);
  window_palette.setColor(QPalette::WindowText, QColor(0, 0, 0));
  setPalette(window_palette);
  setAutoFillBackground(true);
  std::string version_info = "(" + Version::GetMozcVersion() + ")";
  version_label->setText(QLatin1String(version_info.c_str()));
  GuiUtil::ReplaceWidgetLabels(this);

  QPalette palette;
#ifdef MARINAMOZC
  palette.setColor(QPalette::Window, white);
#else
  palette.setColor(QPalette::Window, QColor(236, 233, 216));
#endif  // MARINAMOZC
  color_frame->setPalette(palette);
  color_frame->setAutoFillBackground(true);

#ifdef MARINAMOZC
  // marinaMoji: logo on left only (includes product name); single credit line.
  label->setVisible(false);
  label_credits->setVisible(false);  // We use only label_6 for credit.
  label_6->setText(
      QObject::tr("marinaMoji is a fork of Mozc by M. Pandolfino and D.P. Morgan. "
                  "Mozc © Google LLC."));
  // Load toolbar logo SVG and render at ~2x line height (keep aspect ratio).
  QImageReader reader(QLatin1String(":/marinamozc_logo.svg"));
  QImage svgImage = reader.read();
  if (!svgImage.isNull()) {
    const int line_height = version_label->fontMetrics().height();
    const int logo_height = std::max(line_height * 2, 24);
    product_image_ = std::make_unique<QImage>(svgImage.scaledToHeight(
        logo_height, Qt::SmoothTransformation));
  } else {
    product_image_ = std::make_unique<QImage>();
  }
  SetLabelText(label_terms);
  SetLabelText(label_credits);
#else
  // change font size for product name
  QFont font = label->font();
#ifdef _WIN32
  font.setPointSize(22);
#endif  // _WIN32

#ifdef __APPLE__
  font.setPointSize(26);
#endif  // __APPLE__

  label->setFont(font);

  SetLabelText(label_terms);
  SetLabelText(label_credits);

  product_image_ =
      std::make_unique<QImage>(QLatin1String(":/product_logo.png"));
#endif  // MARINAMOZC
}

void AboutDialog::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  const QRect image_rect = product_image_->rect();
  if (image_rect.isEmpty()) return;
#ifdef MARINAMOZC
  // Logo on left only (same vertical zone as version_label).
  const int left_margin = 10;
  const int top_align = 20;
  const QRect draw_rect(left_margin, top_align, image_rect.width(),
                        image_rect.height());
  painter.drawImage(draw_rect, *product_image_);
#else
  // draw product logo on right
  const QRect draw_rect(std::max(5, width() - image_rect.width() - 15),
                        std::max(0, color_frame->y() - image_rect.height()),
                        image_rect.width(), image_rect.height());
  painter.drawImage(draw_rect, *product_image_);
#endif  // MARINAMOZC
}

void AboutDialog::SetLinkCallback(LinkCallbackInterface *callback) {
  callback_ = callback;
}

void AboutDialog::linkActivated(const QString &link) {
  // we don't activate the link if about dialog is running as root
  if (!RunLevel::IsValidClientRunLevel()) {
    return;
  }
  if (callback_ != nullptr) {
    callback_->linkActivated(link);
  } else {
    defaultLinkActivated(link);
  }
}

}  // namespace gui
}  // namespace mozc
