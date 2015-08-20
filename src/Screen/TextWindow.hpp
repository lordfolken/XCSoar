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

#ifndef XCSOAR_SCREEN_TEXT_WINDOW_HXX
#define XCSOAR_SCREEN_TEXT_WINDOW_HXX

#include "NativeWindow.hpp"

#ifndef USE_GDI
#include "Util/tstring.hpp"
#include <algorithm>
#endif

#include <tchar.h>

class TextWindowStyle : public WindowStyle {
public:
  TextWindowStyle() = default;
  TextWindowStyle(const WindowStyle other):WindowStyle(other) {}

  void left() {
#ifdef USE_GDI
    style |= SS_LEFT;
#endif
  }

  void right() {
#ifdef USE_GDI
    style |= SS_RIGHT;
#endif
  }

  void center() {
#ifdef USE_GDI
    style |= SS_CENTER;
#endif
  }

  void notify() {
#ifdef USE_GDI
    style |= SS_NOTIFY;
#endif
  }
};

/**
 * A window which renders static text.
 */
class TextWindow : public NativeWindow {
#ifndef USE_GDI
  const Font *font = nullptr;

  tstring text;
#endif

public:
  void Create(ContainerWindow &parent, const TCHAR *text, PixelRect rc,
              const TextWindowStyle style=TextWindowStyle());

#ifndef USE_GDI
  void SetFont(const Font &_font) {
    AssertNoneLocked();
    AssertThread();

    font = &_font;
  }

  const Font &GetFont() const {
    AssertThread();
    assert(font != nullptr);

    return *font;
  }
#endif

  void set_text(const TCHAR *_text) {
    AssertNoneLocked();
    AssertThread();

#ifndef USE_GDI
    if (_text != nullptr)
      text = _text;
    else
      text.clear();
    Invalidate();
#else /* USE_GDI */
    ::SetWindowText(hWnd, _text);
#endif /* USE_GDI */
  }

#ifndef USE_GDI
protected:
  void OnPaint(Canvas &canvas) override;
#endif /* !USE_GDI */
};

#endif
