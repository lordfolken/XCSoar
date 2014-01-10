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

#include "Form/Internal.hpp"
#include "Time/PeriodClock.hpp"
#include "Screen/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "Look/DialogLook.hpp"

// returns true if it is a long press,
// otherwise returns false
bool
KeyTimer(bool isdown, unsigned thekey)
{
  static PeriodClock fps_time_down;
  static unsigned savedKey = 0;

  if (thekey != savedKey) {
    savedKey = thekey;
    fps_time_down.Update();
    return false;
  }

  if (!isdown && fps_time_down.CheckUpdate(2000)) {
    savedKey = 0;
    return true;
  }

  return false;
}
