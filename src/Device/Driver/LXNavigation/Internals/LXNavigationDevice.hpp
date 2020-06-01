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

#ifndef XCSOAR_DEVICE_DRIVER_LXNAVIGATION_DEVICE_HPP
#define XCSOAR_DEVICE_DRIVER_LXNAVIGATION_DEVICE_HPP

#include "Device/Driver.hpp"
#include "Device/SettingsMap.hpp"
#include "Thread/Mutex.hxx"
#include <atomic>

namespace LXNavigation
{
class LXNavigationDevice: public AbstractDevice
{
  enum class State
  {
    UNKNOWN,
    DEVICE_INIT,
    PROCESSING_NMEA,
    DOWNLOADING_FLIGHT,
    SHUTDOWN,
    NOT_SUPPORTED
  };

  Port &port;

  std::atomic<unsigned> device_bulk_baud_rate;
  std::atomic<unsigned> device_baud_rate;
  std::atomic<State> state;
  Mutex mutex;

public:
  LXNavigationDevice(Port &communication_port, unsigned baud_rate, unsigned bulk_baud_rate);

public:
  // Device interface
  void LinkTimeout() override;
  bool EnableNMEA(OperationEnvironment &env) override;

  bool ParseNMEA(const char *line, struct NMEAInfo &info) override;

  bool PutBallast(double fraction, double overload,
                  OperationEnvironment &env) override;
  bool PutBugs(double bugs, OperationEnvironment &env) override;
  bool PutMacCready(double mc, OperationEnvironment &env) override;
  bool PutQNH(const AtmosphericPressure &pres,
              OperationEnvironment &env) override;
  bool PutVolume(unsigned volume, OperationEnvironment &env) override;

  bool PutActiveFrequency(RadioFrequency frequency, const TCHAR *name, OperationEnvironment &env) override;
  bool PutStandbyFrequency(RadioFrequency frequency, const TCHAR *name, OperationEnvironment &env) override;

  bool Declare(const Declaration &declaration, const Waypoint *home,
               OperationEnvironment &env) override;

  void OnSysTicker() override;

  bool ReadFlightList(RecordedFlightList &flight_list,
                      OperationEnvironment &env) override;
  bool DownloadFlight(const RecordedFlightInfo &flight,
                      Path path,
                      OperationEnvironment &env) override;

  void OnCalculatedUpdate(const MoreData &basic,
                  const DerivedInfo &calculated) override;
};
}

#endif