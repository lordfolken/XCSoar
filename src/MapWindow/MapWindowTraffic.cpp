// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapWindow.hpp"
#include "ui/canvas/Icon.hpp"
#include "Screen/Layout.hpp"
#include "Formatter/UserUnits.hpp"
#include "Look/TrafficLook.hpp"
#include "Renderer/TextInBox.hpp"
#include "Renderer/TrafficRenderer.hpp"
#include "FLARM/Friends.hpp"
#include "FLARM/Details.hpp"
#include "FLARM/FlarmNetRecord.hpp"
#include "Tracking/SkyLines/Data.hpp"
#include "util/StringCompare.hxx"
#include "Components.hpp"
#include "NetComponents.hpp"
#include "Traffic/Aggregator.hpp"
#include "Traffic/UnifiedTraffic.hpp"
#include "LogFile.hpp"

#include <cassert>

static void
DrawFlarmTraffic(Canvas &canvas, const WindowProjection &projection,
                 const TrafficLook &look, bool fading,
                 const PixelPoint aircraft_pos,
                 const FlarmTraffic &traffic,
                 const MoreData &basic,
                 UnifiedTraffic::Source source = UnifiedTraffic::Source::FLARM_DIRECT) noexcept
{
  assert(traffic.location_available);

  // Points for the screen coordinates for the icon, name and average climb
  PixelPoint sc;

  // If FLARM target not on the screen, move to the next one
  if (auto p = projection.GeoToScreenIfVisible(traffic.location))
    sc = *p;
  else
    return;

  TextInBoxMode mode;
  if (!fading)
    mode.shape = LabelShape::OUTLINED;

  // JMW TODO enhancement: decluttering of FLARM altitudes (sort by max lift)

  // only draw labels if not close to aircraft
  if ((sc - aircraft_pos).MagnitudeSquared() > Layout::Scale(30 * 30)) {
    // If FLARM callsign/name available draw it to the canvas
    if (traffic.HasName() && !StringIsEmpty(traffic.name)) {
      // Draw the name 16 points below the icon
      auto sc_name = sc;
      sc_name.y -= Layout::Scale(20);

      TextInBox(canvas, traffic.name, sc_name,
                mode, projection.GetScreenRect());
    }

    if (!fading && traffic.climb_rate_avg30s >= 0.1) {
      // If average climb data available draw it to the canvas

      // Draw the average climb value above the icon
      auto sc_av = sc;
      sc_av.y += Layout::Scale(5);

      TextInBox(canvas,
                FormatUserVerticalSpeed(traffic.climb_rate_avg30s, false),
                sc_av, mode,
                projection.GetScreenRect());
    }
  }

  auto color = FlarmFriends::GetFriendColor(traffic.id);

  // Calculate arrow angle: use track if available, otherwise point north
  // The arrow tip points upward (north) when angle=0
  // Track is in geographic coordinates (0°=north, 90°=east)
  // Screen angle is the rotation of the map (0°=north up)
  // So arrow_angle = track - screen_angle gives the relative angle
  // Note: PolygonRotateShift rotates counter-clockwise, so we use the angle directly
  Angle arrow_angle;
  if (traffic.track_received) {
    // Track is available - use it (even if 0, which means heading north)
    arrow_angle = traffic.track - projection.GetScreenAngle();
    LogFormat("Traffic arrow: track_received=true track=%d° screen_angle=%d° arrow_angle=%d°",
              (int)Angle(traffic.track).Degrees(),
              (int)projection.GetScreenAngle().Degrees(),
              (int)arrow_angle.Degrees());
  } else {
    // If track is unknown, point north (0 degrees relative to screen)
    arrow_angle = -projection.GetScreenAngle();
    LogFormat("Traffic arrow: track NOT received, pointing north");
  }

  TrafficRenderer::Draw(canvas, look, fading, traffic,
                        arrow_angle,
                        color, sc, source);

  // Draw corner labels: altitude, relative altitude, callsign/ICAO, climb rate
  // Icon is approximately 48x48 pixels, so offset by ~30 pixels from center
  const int label_offset = Layout::Scale(30);
  
  TextInBoxMode label_mode;
  if (!fading)
    label_mode.shape = LabelShape::OUTLINED;
  label_mode.align = TextInBoxMode::Alignment::CENTER;
  
  StaticString<32> label_buffer;
  
  // Top-left: Altitude
  if (traffic.altitude_available) {
    label_buffer = FormatUserAltitude((double)traffic.altitude).c_str();
    auto sc_alt = sc;
    sc_alt.x -= label_offset;
    sc_alt.y -= label_offset;
    TextInBox(canvas, label_buffer, sc_alt, label_mode, projection.GetScreenRect());
  }
  
  // Top-right: Relative altitude - always show when altitude data is available
  if (traffic.altitude_available && basic.gps_altitude_available) {
    // Calculate relative altitude: traffic altitude - our altitude
    // If relative_altitude is already set (from FLARM), use it; otherwise calculate it
    RoughAltitude rel_alt;
    if (traffic.relative_altitude != (RoughAltitude)0) {
      // Use relative altitude from FLARM (already calculated)
      rel_alt = traffic.relative_altitude;
    } else {
      // Calculate relative altitude from absolute altitudes
      rel_alt = traffic.altitude - RoughAltitude(basic.gps_altitude);
    }
    
    TCHAR rel_alt_buffer[32];
    FormatRelativeUserAltitude((double)rel_alt, rel_alt_buffer, false);
    label_buffer = rel_alt_buffer;
    auto sc_rel_alt = sc;
    sc_rel_alt.x += label_offset;
    sc_rel_alt.y -= label_offset;
    TextInBox(canvas, label_buffer, sc_rel_alt, label_mode, projection.GetScreenRect());
  }
  
  // Bottom-left: Competition ID -> callsign -> ICAO ID
  label_buffer.clear();
  const FlarmNetRecord *record = FlarmDetails::LookupRecord(traffic.id);
  if (record != nullptr && !StringIsEmpty(record->callsign)) {
    // Use callsign from FlarmNet
    label_buffer = record->callsign;
  } else if (traffic.HasName() && !StringIsEmpty(traffic.name)) {
    // Use name from traffic
    label_buffer = traffic.name;
  } else if (traffic.id.IsDefined()) {
    // Fall back to ICAO ID (formatted FlarmId)
    TCHAR id_buffer[16];
    traffic.id.Format(id_buffer);
    label_buffer = id_buffer;
  }
  if (!label_buffer.empty()) {
    auto sc_id = sc;
    sc_id.x -= label_offset;
    sc_id.y += label_offset;
    TextInBox(canvas, label_buffer, sc_id, label_mode, projection.GetScreenRect());
  }
  
  // Bottom-right: Climb rate
  if (traffic.climb_rate_received && traffic.climb_rate != 0) {
    label_buffer = FormatUserVerticalSpeed(traffic.climb_rate, false).c_str();
    auto sc_climb = sc;
    sc_climb.x += label_offset;
    sc_climb.y += label_offset;
    TextInBox(canvas, label_buffer, sc_climb, label_mode, projection.GetScreenRect());
  }
}

/**
 * Draws the FLARM traffic icons onto the given canvas
 * @param canvas Canvas for drawing
 */
void
MapWindow::DrawFLARMTraffic(Canvas &canvas,
                            const PixelPoint aircraft_pos) const noexcept
{
  // Return if FLARM icons on moving map are disabled
  if (!GetMapSettings().show_flarm_on_map)
    return;

  const WindowProjection &projection = render_projection;

  // if zoomed in too far out, dont draw traffic since it will be too close to
  // the glider and so will be meaningless (serves only to clutter, cant help
  // the pilot)
  if (projection.GetMapScale() > 7300)
    return;

  canvas.Select(*traffic_look.font);

  // Try to use unified traffic from aggregator first
  TrafficList traffic_list;
  bool use_unified = false;
  
  // In pan mode, use map center for traffic queries; otherwise use aircraft location
  GeoPoint reference_location;
  bool reference_valid = false;
  if (IsPanning()) {
    // Use map center in pan mode
    reference_location = projection.GetGeoScreenCenter();
    reference_valid = projection.IsValid();
  } else {
    // Use aircraft location when following aircraft
    reference_location = Basic().location;
    reference_valid = Basic().location_available;
  }
  
  if (net_components != nullptr && net_components->traffic_aggregator != nullptr &&
      reference_valid && Basic().time_available) {
    // Get unified traffic list using reference location (map center in pan mode, aircraft location otherwise)
    traffic_list = net_components->traffic_aggregator->GetUnifiedTrafficList(
      reference_location, Basic().time);
    use_unified = !traffic_list.IsEmpty();
    
    // Update fading unified traffic
    if (GetMapSettings().fade_traffic) {
      // Compare with previous unified traffic list
      if (traffic_list.modified.Modified(previous_unified_traffic.modified) || true) {
        // First add all items from the previous list
        for (const auto &traffic : previous_unified_traffic.list)
          if (traffic.location_available)
            fading_unified_traffic.try_emplace(traffic.id, traffic);
        
        // Now remove all items that are in the new list; now only items
        // remain that have disappeared
        for (const auto &traffic : traffic_list.list)
          if (auto i = fading_unified_traffic.find(traffic.id); i != fading_unified_traffic.end())
            fading_unified_traffic.erase(i);
      }
      
      // Remove all items that haven't been seen again for too long
      std::erase_if(fading_unified_traffic, [now = Basic().time](const auto &i){
        // Friends expire after 10 minutes, all others after one minute
        const auto max_age = FlarmFriends::GetFriendColor(i.first) != FlarmColor::NONE
          ? std::chrono::minutes{10}
          : std::chrono::minutes{1};
        
        return i.second.valid.IsOlderThan(now, max_age);
      });
    } else {
      fading_unified_traffic.clear();
    }
    
    // Store current list for next comparison
    // Use Complement to copy traffic items
    previous_unified_traffic.Clear();
    previous_unified_traffic.Complement(traffic_list);
    
    LogFormat("DrawFLARMTraffic: unified_traffic=%u items, use_unified=%d, pan_mode=%d",
              traffic_list.GetActiveTrafficCount(), use_unified ? 1 : 0, IsPanning() ? 1 : 0);
  } else {
    LogFormat("DrawFLARMTraffic: aggregator check failed - net_components=%p aggregator=%p reference_valid=%d time=%d",
              net_components,
              net_components ? net_components->traffic_aggregator.get() : nullptr,
              reference_valid ? 1 : 0,
              Basic().time_available ? 1 : 0);
  }

  // Fall back to FLARM traffic if unified traffic is not available
  if (!use_unified) {
    const TrafficList &flarm = Basic().flarm.traffic;
    if (flarm.IsEmpty()) {
      LogFormat("DrawFLARMTraffic: no FLARM traffic, returning");
      return;
    }
    traffic_list = flarm;
    LogFormat("DrawFLARMTraffic: using FLARM traffic, count=%u",
              flarm.GetActiveTrafficCount());
  }

  // Circle through the traffic targets
  unsigned drawn_count = 0;
  for (const auto &traffic : traffic_list.list) {
    if (!traffic.location_available) {
      LogFormat("DrawFLARMTraffic: skipping traffic (no location)");
      continue;
    }

    // For unified traffic, we always draw (it's already filtered)
    // For FLARM traffic, skip if relative_east is 0 (no position)
    if (!use_unified && !traffic.relative_east) {
      LogFormat("DrawFLARMTraffic: skipping FLARM traffic (relative_east=0)");
      continue;
    }

    // Get source information for unified traffic
    UnifiedTraffic::Source source = UnifiedTraffic::Source::FLARM_DIRECT;
    if (use_unified && net_components != nullptr && 
        net_components->traffic_aggregator != nullptr) {
      const auto &traffic_map = net_components->traffic_aggregator->GetTraffic();
      auto it = traffic_map.find(traffic.id);
      if (it != traffic_map.end()) {
        source = it->second.source;
      }
    }

          DrawFlarmTraffic(canvas, projection, traffic_look, false,
                            aircraft_pos, traffic, Basic(), source);
    drawn_count++;
  }
  
  LogFormat("DrawFLARMTraffic: drawn %u traffic items", drawn_count);

  // Draw fading unified traffic (only if using unified traffic)
  if (use_unified) {
    if (const auto &fading = GetFadingUnifiedTraffic(); !fading.empty()) {
      for (const auto &[id, traffic] : fading) {
        assert(traffic.location_available);

        // Get source information for fading unified traffic
        UnifiedTraffic::Source source = UnifiedTraffic::Source::FLARM_DIRECT;
        if (net_components != nullptr && 
            net_components->traffic_aggregator != nullptr) {
          const auto &traffic_map = net_components->traffic_aggregator->GetTraffic();
          auto it = traffic_map.find(id);
          if (it != traffic_map.end()) {
            source = it->second.source;
          }
        }

               DrawFlarmTraffic(canvas, projection, traffic_look, true,
                                 aircraft_pos, traffic, Basic(), source);
      }
    }
  }

  // Draw fading FLARM traffic (only if not using unified traffic)
  if (!use_unified) {
    if (const auto &fading = GetFadingFlarmTraffic(); !fading.empty()) {
      for (const auto &[id, traffic] : fading) {
        assert(traffic.location_available);

        // No position traffic (relative_east=0) does not make sense in map display
        if (traffic.relative_east)
          DrawFlarmTraffic(canvas, projection, traffic_look, true,
                           aircraft_pos, traffic, Basic(), UnifiedTraffic::Source::FLARM_DIRECT);
      }
    }
  }
}


/**
 * Draws the GliderLink traffic icons onto the given canvas
 * @param canvas Canvas for drawing
 */
void
MapWindow::DrawGLinkTraffic([[maybe_unused]] Canvas &canvas) const noexcept
{
#ifdef ANDROID

  // Return if FLARM icons on moving map are disabled
  if (!GetMapSettings().show_flarm_on_map)
    return;

  const GliderLinkTrafficList &traffic = Basic().glink_data.traffic;
  if (traffic.IsEmpty())
    return;

  const MoreData &basic = Basic();

  const WindowProjection &projection = render_projection;

  canvas.Select(*traffic_look.font);

  // Circle through the GliderLink targets
  for (const auto &traf : traffic.list) {

    // Points for the screen coordinates for the icon, name and average climb
    PixelPoint sc;

    // If FLARM target not on the screen, move to the next one
    if (auto p = projection.GeoToScreenIfVisible(traf.location))
      sc = *p;
    else
      continue;

    TextInBoxMode mode;
    mode.shape = LabelShape::OUTLINED;
    mode.align = TextInBoxMode::Alignment::RIGHT;

    // If callsign/name available draw it to the canvas
    if (traf.HasName() && !StringIsEmpty(traf.name)) {
      // Draw the callsign above the icon
      auto sc_name = sc;
      sc_name.x -= Layout::Scale(10);
      sc_name.y -= Layout::Scale(15);

      TextInBox(canvas, traf.name, sc_name,
                mode, GetClientRect());
    }

    if (traf.climb_rate_received) {

      // If average climb data available draw it to the canvas
      mode.align = TextInBoxMode::Alignment::LEFT;

      // Draw the average climb to the right of the icon
      auto sc_av = sc;
      sc_av.x += Layout::Scale(10);
      sc_av.y -= Layout::Scale(8);

      TextInBox(canvas,
                FormatUserVerticalSpeed(traf.climb_rate, false),
                sc_av, mode, GetClientRect());
    }

    // use GPS altitude to be consistent with GliderLink
    if(basic.gps_altitude_available && traf.altitude_received
        && fabs(double(traf.altitude) - basic.gps_altitude) >= 100.0) {
      // If average climb data available draw it to the canvas
      TCHAR label_alt[100];
      double alt = (double(traf.altitude) - basic.gps_altitude) / 100.0;
      FormatRelativeUserAltitude(alt, label_alt, false);

      // Location of altitude label
      auto sc_alt = sc;
      sc_alt.x -= Layout::Scale(10);
      sc_alt.y -= Layout::Scale(0);

      mode.align = TextInBoxMode::Alignment::RIGHT;
      TextInBox(canvas, label_alt, sc_alt, mode, GetClientRect());
    }

    TrafficRenderer::Draw(canvas, traffic_look, traf,
                          traf.track - projection.GetScreenAngle(), sc);
  }
#endif
}

/**
 * Draws the teammate icon to the given canvas
 * @param canvas Canvas for drawing
 */
void
MapWindow::DrawTeammate(Canvas &canvas) const noexcept
{
  const TeamInfo &teamcode_info = Calculated();

  if (teamcode_info.teammate_available) {
    if (auto p = render_projection.GeoToScreenIfVisible(teamcode_info.teammate_location))
      traffic_look.teammate_icon.Draw(canvas, *p);
  }
}

#ifdef HAVE_SKYLINES_TRACKING

void
MapWindow::DrawSkyLinesTraffic(Canvas &canvas) const noexcept
{
  if (DisplaySkyLinesTrafficMapMode::OFF == GetMapSettings().skylines_traffic_map_mode ||
      skylines_data == nullptr)
    return;

  canvas.Select(*traffic_look.font);

  const std::lock_guard lock{skylines_data->mutex};
  for (auto &i : skylines_data->traffic) {
    // Skip OGN traffic (identified by flarm_id != 0) - it's rendered via unified traffic system
    if (i.second.flarm_id != 0)
      continue;
    
    if (auto p = render_projection.GeoToScreenIfVisible(i.second.location)) {
      traffic_look.teammate_icon.Draw(canvas, *p);
      if (DisplaySkyLinesTrafficMapMode::SYMBOL_NAME == GetMapSettings().skylines_traffic_map_mode) {
        const auto name_i = skylines_data->user_names.find(i.first);
        const TCHAR *name = name_i != skylines_data->user_names.end()
          ? name_i->second.c_str()
          : _T("");

        StaticString<128> buffer;
        buffer.Format(_T("%s [%um]"), name, i.second.altitude);

        TextInBoxMode mode;
        mode.shape = LabelShape::OUTLINED;

        // Draw the name 16 points below the icon
        p->y -= Layout::Scale(10);
        TextInBox(canvas, buffer, *p, mode, GetClientRect());
      }
    }
  }
}

#endif
