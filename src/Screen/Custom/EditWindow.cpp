/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2015 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Screen/EditWindow.hpp"
#include "Screen/Canvas.hpp"
#include "Screen/Features.hpp"
#include "Screen/Layout.hpp"
#include "Screen/Key.h"
#include "Util/CharUtil.hpp"

void
EditWindow::Create(ContainerWindow &parent, PixelRect rc,
                   const EditWindowStyle style, size_t _max_length)
{
  read_only = style.is_read_only;
  max_length = _max_length;

  Window::Create(&parent, rc, style);
}

void
EditWindow::OnPaint(Canvas &canvas)
{
  if (IsEnabled()) {
    if (IsReadOnly())
      canvas.Clear(Color(0xf0, 0xf0, 0xf0));
    else
      canvas.ClearWhite();
    canvas.SetTextColor(COLOR_BLACK);
  } else {
    canvas.Clear(COLOR_LIGHT_GRAY);
    canvas.SetTextColor(COLOR_DARK_GRAY);
  }

  const PixelRect rc(0, 0, canvas.GetWidth() - 1, canvas.GetHeight() - 1);

  canvas.DrawOutlineRectangle(rc.left, rc.top, rc.right, rc.bottom, COLOR_BLACK);

  if (value.empty())
    return;

  canvas.SetBackgroundTransparent();

  const PixelScalar x = Layout::GetTextPadding();
  const PixelScalar canvas_height = canvas.GetHeight();
  const PixelScalar text_height = canvas.GetFontHeight();
  const PixelScalar y = (canvas_height - text_height) / 2;

  canvas.TextAutoClipped(x, y, value.c_str());
}

bool
EditWindow::OnCharacter(unsigned ch)
{
  if (IsReadOnly())
    return false;

  if (ch == '\b') {
    /* backspace */
    if (!value.empty()) {
      value.erase(value.end() - 1);
      Invalidate();
    }

    return true;
  }

  if (value.length() >= max_length)
    return false;

  if (ch < 0x20)
    return false;

  if (ch >= 0x80)
    // TODO: ignoring non-ASCII characters for now
    return false;

  if (IsAlphaNumericASCII((TCHAR)ch) || ch == ' ') {
    value.push_back((TCHAR)ch);
    Invalidate();
    return true;
  }

  return false;
}

void
EditWindow::SetText(const TCHAR *text)
{
  AssertNoneLocked();

  if (text != nullptr)
    value = text;
  else
    value.clear();
  Invalidate();
}
