// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#pragma once

#include "event/SocketEvent.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "event/net/cares/SimpleResolver.hxx"
#include "event/net/ConnectSocket.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/StaticString.hxx"
#include "Geo/GeoPoint.hpp"
#include "util/tstring.hpp"
#include "FLARM/Traffic.hpp"

#include <memory>
#include <string>
#include <optional>

class EventLoop;
class OGNTrafficContainer;
namespace Cares { class Channel; }

/**
 * Client for connecting to OGN APRS-IS servers and parsing traffic data.
 */
class OGNClient final
  : public Cares::SimpleHandler, public ConnectSocketHandler
{
  EventLoop &event_loop;
  OGNTrafficContainer &traffic_container;
  Cares::Channel *cares_channel = nullptr;

  std::optional<Cares::SimpleResolver> resolver;
  std::optional<ConnectSocket> connect;
  AllocatedSocketAddress address;
  SocketEvent socket_event;

  std::string input_buffer;
  CoarseTimerEvent reconnect_timer;
  CoarseTimerEvent keepalive_timer;  // Send keepalives every 240s (as per python-ogn-client)

  bool enabled = false;
  const char *server = nullptr;
  unsigned port = 0;

  StaticString<64> filter;
  StaticString<16> callsign;
  int passcode = -1;  // -1 = read-only, or use valid APRS passcode

public:
  OGNClient(EventLoop &_event_loop, Cares::Channel &_cares,
            OGNTrafficContainer &_traffic_container);
  ~OGNClient();

  auto &GetEventLoop() const noexcept {
    return event_loop;
  }

  void SetEnabled(bool _enabled) {
    enabled = _enabled;
    if (!enabled)
      Close();
    else if (server != nullptr)
      Open();
  }

  void SetServer(const char *_server, unsigned _port) {
    server = _server;
    port = _port;
    if (enabled)
      Open();
  }

  void SetFilter(const char *_filter) {
    if (filter != _filter) {
      filter = _filter;
      UpdateFilter();
    }
  }

  void SetCallsign(const char *_callsign) {
    callsign = _callsign;
  }

  void SetPasscode(int _passcode) {
    passcode = _passcode;
  }

  /**
   * Calculate APRS passcode from callsign.
   * This implements the standard APRS passcode algorithm.
   * @param callsign The callsign (e.g., "XC0AR")
   * @return The calculated passcode, or -1 on error
   */
  static int CalculatePasscode(const char *callsign);

  void UpdateFilter();

  void OnKeepaliveTimer() noexcept;

  bool IsConnected() const {
    return socket_event.IsDefined();
  }

  void Open();
  void Close();

private:
  void OnSocketReady(unsigned events) noexcept;

  /* virtual methods from Cares::SimpleHandler */
  void OnResolverSuccess(std::forward_list<AllocatedSocketAddress> addresses)
    noexcept override;
  void OnResolverError(std::exception_ptr error) noexcept override;

  /* virtual methods from ConnectSocketHandler */
  void OnSocketConnectSuccess(UniqueSocketDescriptor fd) noexcept override;
  void OnSocketConnectError(std::exception_ptr ep) noexcept override;

  void OnReconnectTimer() noexcept;

  void ProcessLine(const char *line);
  void ParseAPRSPacket(const char *packet, const tstring &device_id);
  bool ParseOGNPosition(const char *packet, GeoPoint &location, int &altitude,
                        unsigned &track, unsigned &speed, int &climb_rate,
                        double &turn_rate,
                        FlarmTraffic::AircraftType &aircraft_type);
};

