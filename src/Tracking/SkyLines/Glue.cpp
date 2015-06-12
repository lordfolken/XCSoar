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

#include "Glue.hpp"
#include "Settings.hpp"
#include "Queue.hpp"
#include "Assemble.hpp"
#include "NMEA/Info.hpp"
#include "Net/State.hpp"
#include "Net/IPv4Address.hpp"

#ifdef HAVE_POSIX
#include "IO/Async/GlobalIOThread.hpp"
#endif

#include <assert.h>

SkyLinesTracking::Glue::Glue()
  :interval(0),
#ifdef HAVE_SKYLINES_TRACKING_HANDLER
   traffic_enabled(false),
#endif
   roaming(true),
   queue(nullptr)
{
#ifdef HAVE_SKYLINES_TRACKING_HANDLER
  assert(io_thread != nullptr);
  client.SetIOThread(io_thread);
#endif
}

SkyLinesTracking::Glue::~Glue()
{
  delete queue;
}

void
SkyLinesTracking::Glue::Tick(const NMEAInfo &basic)
{
  if (!client.IsDefined())
    return;

  if (!basic.time_available) {
    clock.Reset();
#ifdef HAVE_SKYLINES_TRACKING_HANDLER
    traffic_clock.Reset();
#endif
    return;
  }

  bool connected = false;
  switch (GetNetState()) {
  case NetState::UNKNOWN:
  case NetState::DISCONNECTED:
    break;

  case NetState::CONNECTED:
    connected = true;
    break;

  case NetState::ROAMING:
    connected = roaming;
    break;
  }

  if (!connected) {
    /* queue the packet, send it later */
    if (queue == nullptr)
      queue = new Queue();
    queue->Push(ToFix(client.GetKey(), basic));
    return;
  }

  if (queue != nullptr) {
    /* send queued fix packets, 8 at a time */
    unsigned n = 8;
    while (n-- > 0) {
      const auto &packet = queue->Peek();
      if (!client.SendPacket(packet))
        break;

      queue->Pop();
      if (queue->IsEmpty()) {
        delete queue;
        queue = nullptr;
      }
    }

    return;
  } else if (clock.CheckAdvance(basic.time, fixed(interval)))
    client.SendFix(basic);

#ifdef HAVE_SKYLINES_TRACKING_HANDLER
  if (traffic_enabled && traffic_clock.CheckAdvance(basic.time, fixed(60)))
    client.SendTrafficRequest(true, true);
#endif
}

void
SkyLinesTracking::Glue::SetSettings(const Settings &settings)
{
  if (!settings.enabled || settings.key == 0) {
    client.Close();
    return;
  }

  client.SetKey(settings.key);

  interval = settings.interval;

  if (!client.IsDefined())
    // TODO: fix hard-coded IP address:
    client.Open(IPv4Address(95, 128, 34, 172, Client::GetDefaultPort()));

#ifdef HAVE_SKYLINES_TRACKING_HANDLER
  traffic_enabled = settings.traffic_enabled;
#endif

  roaming = settings.roaming;
}
