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
#include "LXEra.hpp"
#include "NMEA/Checksum.hpp"
#include "NMEA/InputLine.hpp"
#include "NMEA/Info.hpp"
#include "Geo/SpeedVector.hpp"
#include "Units/System.hpp"
#include "Util/Macros.hpp"
#include "Util/StringCompare.hxx"
#include "LogFile.hpp"

static bool
ReadSpeedVector(NMEAInputLine &line, SpeedVector &value_r)
{
  double bearing, norm;

  bool bearing_valid = line.ReadChecked(bearing);
  bool norm_valid = line.ReadChecked(norm);

  if (bearing_valid && norm_valid) {
    value_r.bearing = Angle::Degrees(bearing);
    value_r.norm = Units::ToSysUnit(norm, Unit::KILOMETER_PER_HOUR);
    return true;
  } else
    return false;
}

static bool
LXWP0(NMEAInputLine &line, NMEAInfo &info)
{
  /*
  $LXWP0,Y,222.3,1665.5,1.71,,,,,,239,174,10.1

   0 loger_stored (Y/N)
   1 IAS (kph) ----> Condor uses TAS!
   2 baroaltitude (m) ----> LXNavigation (LXERA+) True Altitude
   3-8 vario (m/s) (last 6 measurements in last second)
   9 heading of plane
  10 windcourse (deg)
  11 windspeed (kph)
  */

  line.Skip();

  double airspeed;
  bool tas_available = line.ReadChecked(airspeed);
  if (tas_available && (airspeed < -50 || airspeed > 250))
    /* implausible */
    return false;
  double value;
  if (line.ReadChecked(value)) {
    /* a dump on a LX7007 has confirmed that the LX sends uncorrected
       altitude above 1013.25hPa here */
    if (LXEra::is_lxera) {
       info.ProvideBaroAltitudeTrue(value);
    } else { 
       info.ProvidePressureAltitude(value);
    }
  }

  if (tas_available)
    /*
     * Call ProvideTrueAirspeed() after ProvidePressureAltitude() to use
     * the provided altitude (if available)
     */
    info.ProvideTrueAirspeed(Units::ToSysUnit(airspeed, Unit::KILOMETER_PER_HOUR));

  if (line.ReadChecked(value))
    info.ProvideTotalEnergyVario(value);

  line.Skip(6);

  SpeedVector wind;
  if (ReadSpeedVector(line, wind))
    info.ProvideExternalWind(wind);

  return true;
}

template<size_t N>
static void
ReadString(NMEAInputLine &line, NarrowString<N> &value)
{
  line.Read(value.buffer(), value.capacity());
}

static void
LXWP1(NMEAInputLine &line, DeviceInfo &device)
{
  /*
   * $LXWP1,
   * instrument ID,
   * serial number,
   * software version,
   * hardware version,
   * license string
   */

  ReadString(line, device.product);
  ReadString(line, device.serial);
  ReadString(line, device.software_version);
  ReadString(line, device.hardware_version);
}

static bool
LXWP2(NMEAInputLine &line, NMEAInfo &info)
{
  /*
   * $LXWP2,
   * maccready value, (m/s)
   * ballast, (1.0 - 1.5)
   * bugs, (0 - 100%)
   * polar_a,
   * polar_b,
   * polar_c,
   * audio volume
   */

  double value;
  // MacCready value
  if (line.ReadChecked(value))
    info.settings.ProvideMacCready(value, info.clock);

  // Ballast
  if (line.ReadChecked(value))
    info.settings.ProvideBallastOverload(value, info.clock);

  // Bugs
  if (line.ReadChecked(value)) {
    if (value <= 1.5 && value >= 1.0)
      // LX160 (sw 3.04) reports bugs as 1.00, 1.05 or 1.10 (#2167)
      info.settings.ProvideBugs(2 - value, info.clock);
    else
      // All other known LX devices report bugs as 0, 5, 10, 15, ...
      info.settings.ProvideBugs((100 - value) / 100., info.clock);
  }

  line.Skip(3);

  unsigned volume;
  if (line.ReadChecked(volume))
    info.settings.ProvideVolume(volume, info.clock);

  return true;
}

static bool
LXWP3(NMEAInputLine &line, NMEAInfo &info)
{
  /*
   * $LXWP3,
   * altioffset
   * scmode
   * variofil
   * tefilter
   * televel
   * varioavg
   * variorange
   * sctab
   * sclow
   * scspeed
   * SmartDiff
   * glider name
   * time offset
   */

  double value;

  // Altitude offset -> QNH
  if (line.ReadChecked(value)) {
    value = Units::ToSysUnit(-value, Unit::FEET);
    auto qnh = AtmosphericPressure::PressureAltitudeToStaticPressure(value);
    info.settings.ProvideQNH(qnh, info.clock);
  }

  return true;
}

bool
LXEraDevice::ParseNMEA(const char *String, NMEAInfo &info)
{
  if (!VerifyNMEAChecksum(String))
    return false;

  NMEAInputLine line(String);
  char type[16];
  line.Read(type, 16);

  if (StringIsEqual(type, "$LXWP0"))
    return LXWP0(line, info);

  if (StringIsEqual(type, "$LXWP1")) {
    DeviceInfo &device_info = mode == Mode::NMEA
      ? info.secondary_device
      : info.device;
    LXWP1(line, device_info);

    const bool saw_lxera = device_info.product.equals("LX Eos");
    LXEra::is_lxera = saw_lxera;
    if (LXEra::is_lxera) {
      LogFormat("LXEra Detected");
    }

    return true;
  }

  if (StringIsEqual(type, "$LXWP2")) {
    return LXWP2(line, info);
  } 

  if (StringIsEqual(type, "$LXWP3"))
    return LXWP3(line, info);

  return false;
}
