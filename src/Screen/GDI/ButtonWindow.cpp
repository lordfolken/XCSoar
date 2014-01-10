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

#include "Screen/ButtonWindow.hpp"
#include "Util/Macros.hpp"

#include <commctrl.h>

void
BaseButtonWindow::Create(ContainerWindow &parent,
                         const TCHAR *text, unsigned id,
                         const PixelRect &rc,
                         const WindowStyle style)
{
  Window::Create(&parent, WC_BUTTON, text, rc, style);

  SetWindowLong(GWL_ID, id);

  InstallWndProc();
}

bool
BaseButtonWindow::OnClicked()
{
  return false;
}

void
BaseButtonWindow::Click()
{
  if (OnClicked())
    return;

  unsigned id = GetID();
  if (id != 0)
    ::SendMessage(::GetParent(hWnd), WM_COMMAND, id, 0);
}

void
ButtonWindow::SetText(const TCHAR *_text)
{
  AssertNoneLocked();
  AssertThread();

  if (GetCustomPainting() || _tcschr(_text, _T('&')) == nullptr) {
    ::SetWindowText(hWnd, _text);
    return;
  }

  TCHAR buffer[256]; /* should be large enough for buttons */
  static unsigned const int buffer_size = ARRAY_SIZE(buffer);

  TCHAR const *s=_text;
  TCHAR *d=buffer;

  // Workaround WIN32 special use of '&' (replace every '&' with "&&")
  // Note: Terminates loop two chars before the buffer_size. This might prevent
  // potential char copies but assures that there is always room for
  // two '&'s and the 0-terminator.
  while (*s && d < buffer + buffer_size - 2) {
    if (*s == _T('&'))
      *d++ = *s;
    *d++ = *s++;
  }
  *d=0;

  ::SetWindowText(hWnd, buffer);
}

const tstring
ButtonWindow::GetText() const
{
  AssertNoneLocked();
  AssertThread();

  TCHAR buffer[256]; /* should be large enough for buttons */

  int length = GetWindowText(hWnd, buffer, ARRAY_SIZE(buffer));
  return tstring(buffer, length);
}

bool
ButtonWindow::OnKeyCheck(unsigned key_code) const
{
  switch (key_code) {
  case VK_RETURN:
    return true;

  default:
    return BaseButtonWindow::OnKeyCheck(key_code);
  }
}

bool
ButtonWindow::OnKeyDown(unsigned key_code)
{
  switch (key_code) {
#ifdef GNAV
  case VK_F4:
    // using F16 also as Enter-Key. This allows to use the RemoteStick of Altair to do a "click" on the focused button
  case VK_F16:
#endif
  case VK_RETURN:
    Click();
    return true;

  default:
    return BaseButtonWindow::OnKeyDown(key_code);
  }
}
