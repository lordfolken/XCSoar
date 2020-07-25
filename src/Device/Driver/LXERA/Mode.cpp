/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2016 The XCSoar Project
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

#include "Internal.hpp"
#include "Device/Port/Port.hpp"
#include "Operation/Operation.hpp"
#include "LXERA.hpp"

void
LXEraDevice::LinkTimeout()
{
  busy = false;

  std::lock_guard<Mutex> lock(mutex);

  ResetDeviceDetection();

  old_baud_rate = 0;
}

bool
LXEraDevice::EnableNMEA(OperationEnvironment &env)
{
  unsigned old_baud_rate;

  {
    std::lock_guard<Mutex> lock(mutex);
    if (mode == Mode::NMEA)
      return true;

    old_baud_rate = this->old_baud_rate;
    this->old_baud_rate = 0;
    mode = Mode::NMEA;
    busy = false;
  }

  LXEra::SetupNMEA(port, env);

  if (old_baud_rate != 0)
    port.SetBaudrate(old_baud_rate);

  port.Flush();

  return true;
}

void
LXEraDevice::OnSysTicker()
{
  std::lock_guard<Mutex> lock(mutex);
}
