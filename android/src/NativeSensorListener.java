/* Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2021 The XCSoar Project
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

package org.xcsoar;

/**
 * An #SensorListener implementation that passes method calls to
 * native code.
 */
public class NativeSensorListener implements SensorListener {
  /**
   * A native pointer.
   */
  private final long ptr;

  NativeSensorListener(long ptr) {
    this.ptr = ptr;
  }

  public native void onConnected(int connected);

  public native void onLocationSensor(long time, int n_satellites,
                                      double longitude, double latitude,
                                      boolean hasAltitude, double altitude,
                                      boolean hasBearing, double bearing,
                                      boolean hasSpeed, double speed,
                                      boolean hasAccuracy, double accuracy,
                                      boolean hasAcceleration,
                                      double acceleration);

  public native void onAccelerationSensor(float ddx, float ddy, float ddz);
  public native void onRotationSensor(float dtheta_x, float dtheta_y,
                                      float dtheta_z);
  public native void onMagneticFieldSensor(float h_x, float h_y, float h_z);
  public native void onBarometricPressureSensor(float pressure,
                                                float sensor_noise_variance);
  public native void onPressureAltitudeSensor(float altitude);
  public native void onVarioSensor(float vario);
  public native void onHeartRateSensor(int bpm);
}
