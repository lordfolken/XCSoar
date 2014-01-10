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

#ifndef XCSOAR_SCREEN_LARGE_TEXT_WINDOW_HPP
#define XCSOAR_SCREEN_LARGE_TEXT_WINDOW_HPP

#include "Screen/Window.hpp"

#ifndef USE_GDI
#include "Util/tstring.hpp"

#include <winuser.h>
#endif

#include <tchar.h>

class LargeTextWindowStyle : public WindowStyle {
public:
  LargeTextWindowStyle() {
    VerticalScroll();
#ifdef USE_GDI
    style |= ES_LEFT | ES_MULTILINE | ES_READONLY;
#else
    text_style |= DT_LEFT | DT_WORDBREAK;
#endif
  }

  LargeTextWindowStyle(const WindowStyle other):WindowStyle(other) {
    VerticalScroll();
#ifdef USE_GDI
    style |= ES_LEFT | ES_MULTILINE | ES_READONLY;
#else
    text_style |= DT_LEFT | DT_WORDBREAK;
#endif
  }

  void SetCenter() {
#ifndef USE_GDI
    text_style &= ~DT_LEFT;
    text_style |= DT_CENTER;
#else
    style &= ~ES_LEFT;
    style |= ES_CENTER;
#endif
  }
};

/**
 * A window showing large multi-line text.
 */
class LargeTextWindow : public Window {
#ifndef USE_GDI
  tstring value;

  /**
   * The first visible line.
   */
  unsigned origin;
#endif

public:
  void Create(ContainerWindow &parent, PixelRect rc,
              const LargeTextWindowStyle style=LargeTextWindowStyle());

#ifndef USE_GDI
  gcc_pure
  unsigned GetVisibleRows() const;
#endif

#ifndef USE_GDI
  gcc_pure
  unsigned GetRowCount() const;
#else
  gcc_pure
  unsigned GetRowCount() const {
    AssertNoneLocked();

    return ::SendMessage(hWnd, EM_GETLINECOUNT, 0, 0);
  }
#endif

  void SetText(const TCHAR *text);

  /**
   * Scroll the contents of a multi-line control by the specified
   * number of lines.
   */
  void ScrollVertically(int delta_lines);

#ifndef USE_GDI
protected:
  virtual void OnResize(PixelSize new_size) override;
  virtual void OnPaint(Canvas &canvas) override;
  virtual bool OnKeyCheck(unsigned key_code) const override;
  virtual bool OnKeyDown(unsigned key_code) override;
#endif /* !USE_GDI */
};

#endif
