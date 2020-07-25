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
#include "Device/Util/NMEAWriter.hpp"
#include "LXERA.hpp"

#include <cstdio>

bool
LXEraDevice::PutBallast(gcc_unused double fraction, double overload,
                     OperationEnvironment &env)
{
  if (!EnableNMEA(env))
    return false;

  return LXEra::SetBallast(port, env, overload);
}

bool
LXEraDevice::PutBugs(double bugs, OperationEnvironment &env)
{
  if (!EnableNMEA(env))
    return false;

  int transformed_bugs_value = 100 - (int)(bugs*100);

  return LXEra::SetBugs(port, env, transformed_bugs_value);
}

bool
LXEraDevice::PutMacCready(double mac_cready, OperationEnvironment &env)
{
  if (!EnableNMEA(env))
    return false;

  return LXEra::SetMacCready(port, env, mac_cready);
}

bool
LXEraDevice::PutQNH(const AtmosphericPressure &pres, OperationEnvironment &env)
{
  if (!EnableNMEA(env))
    return false;

  return LXEra::SetQNH(port, env, pres);
}

bool
LXEraDevice::PutVolume(unsigned volume, OperationEnvironment &env)
{
  return LXEra::SetVolume(port, env, volume);
}
