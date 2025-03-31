// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

package org.xcsoar;

import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothProfile;
import android.content.Context;
import android.os.Build;
import android.util.Log;
import java.io.IOException;

/**
 * An AndroidPort implementation for connecting to a Nordic UART Service (NUS).
 */
public class NordicUartPort
    extends BluetoothGattCallback implements AndroidPort {
  private static final String TAG = "NordicUartPort";

  private static final int MAX_WRITE_CHUNK_SIZE = 20; // Default chunk size
  private static final int DISCONNECT_TIMEOUT = 500;  // Timeout for disconnect

  private PortListener portListener;
  private volatile InputListener listener;
  private final SafeDestruct safeDestruct = new SafeDestruct();

  private final BluetoothGatt gatt;
  private BluetoothGattCharacteristic txCharacteristic;
  private BluetoothGattCharacteristic rxCharacteristic;
  private volatile boolean shutdown = false;

  private final NordicUartWriteBuffer writeBuffer =
      new NordicUartWriteBuffer();

  private volatile int portState = STATE_LIMBO;
  private final Object gattStateSync = new Object();
  private int gattState = BluetoothGatt.STATE_DISCONNECTED;

  private boolean setupCharacteristicsPending = false;

  public NordicUartPort(Context context, BluetoothDevice device)
      throws IOException
  {
    if (Build.VERSION.SDK_INT >= 23) {
      gatt = device.connectGatt(context, true, this,
                                BluetoothDevice.TRANSPORT_LE);
    } else {
      gatt = device.connectGatt(context, true, this);
    }

    if (gatt == null) {
      throw new IOException("Bluetooth GATT connect failed");
    }
  }

  private void findCharacteristics() throws Error
  {
    txCharacteristic = null;
    rxCharacteristic = null;

    BluetoothGattService service = gatt.getService(NordicUuids.NUS_SERVICE);
    if (service != null) {
      txCharacteristic =
          service.getCharacteristic(NordicUuids.NUS_TX_CHARACTERISTIC);
      rxCharacteristic =
          service.getCharacteristic(NordicUuids.NUS_RX_CHARACTERISTIC);
    }

    if (txCharacteristic == null) {
      throw new Error("NUS TX characteristic not found");
    }

    if (rxCharacteristic == null) {
      throw new Error("NUS RX characteristic not found");
    }
  }

  private void setupCharacteristics() throws Error
  {
    findCharacteristics();

    if (!gatt.setCharacteristicNotification(rxCharacteristic, true)) {
      throw new Error("Could not enable GATT characteristic notification");
    }

    BluetoothGattDescriptor descriptor = rxCharacteristic.getDescriptor(
        NordicUuids.CLIENT_CHARACTERISTIC_CONFIGURATION);
    descriptor.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
    gatt.writeDescriptor(descriptor);
    portState = STATE_READY;
    stateChanged();
  }

  @Override
  public void onConnectionStateChange(BluetoothGatt gatt, int status,
                                      int newState)
  {
    try {
      if (BluetoothProfile.STATE_CONNECTED == newState) {
        if (!gatt.discoverServices()) {
          throw new Error("Discovering GATT services request failed");
        }
      } else {
        txCharacteristic = null;
        rxCharacteristic = null;

        if ((BluetoothProfile.STATE_DISCONNECTED == newState) && !shutdown &&
            !gatt.connect()) {
          throw new Error(
              "Received GATT disconnected event, and reconnect attempt failed");
        }
      }

      portState = STATE_LIMBO;
    } catch (Error e) {
      error(e.getMessage());
    }

    writeBuffer.reset();
    stateChanged();
    synchronized (gattStateSync) {
      gattState = newState;
      gattStateSync.notifyAll();
    }
  }

  @Override public void onServicesDiscovered(BluetoothGatt gatt, int status)
  {
    try {
      if (BluetoothGatt.GATT_SUCCESS != status) {
        throw new Error("Discovering GATT services failed");
      }

      setupCharacteristicsPending = true;
      gatt.requestMtu(256); // Request a high MTU
    } catch (Error e) {
      error(e.getMessage());
      stateChanged();
    }
  }

  @Override
  public void onCharacteristicRead(BluetoothGatt gatt,
                                   BluetoothGattCharacteristic characteristic,
                                   int status)
  {
    writeBuffer.beginWriteNextChunk(gatt, txCharacteristic);
  }

    @Override
    public void onCharacteristicWrite(Bluetooth
