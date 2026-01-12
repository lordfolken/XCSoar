// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "MapWindow.hpp"
#include "Overlay.hpp"
#include "Look/MapLook.hpp"
#include "Weather/Rasp/RaspRenderer.hpp"
#include "Weather/Rasp/RaspCache.hpp"
#include "Topography/CachedTopographyRenderer.hpp"
#include "Renderer/AircraftRenderer.hpp"
#include "Renderer/WaveRenderer.hpp"
#include "Operation/Operation.hpp"
#include "Tracking/SkyLines/Data.hpp"
#include "Geo/GeoVector.hpp"

#ifdef HAVE_NOAA
#include "Weather/NOAAStore.hpp"
#endif

void
MapWindow::RenderTrackBearing(Canvas &canvas,
                              const PixelPoint aircraft_pos) noexcept
{
  // default rendering option assumes circling is off, so ground-relative
  DrawTrackBearing(canvas, aircraft_pos, false);
}

inline void
MapWindow::RenderTerrain(Canvas &canvas) noexcept
{
  background.SetShadingAngle(render_projection, GetMapSettings().terrain,
                             Calculated());
  background.Draw(canvas, render_projection, GetMapSettings().terrain);
}

inline void
MapWindow::RenderRasp(Canvas &canvas) noexcept
{
  if (rasp_store == nullptr)
    return;

  const WeatherUIState &state = GetUIState().weather;
  if (rasp_renderer && state.map != (int)rasp_renderer->GetParameter()) {
#ifndef ENABLE_OPENGL
    const std::lock_guard lock{mutex};
#endif

    rasp_renderer.reset();
  }

  if (state.map < 0)
    return;

  if (!rasp_renderer) {
#ifndef ENABLE_OPENGL
    const std::lock_guard lock{mutex};
#endif
    rasp_renderer.reset(new RaspRenderer(*rasp_store, state.map));
  }

  rasp_renderer->SetTime(state.time);

  {
    QuietOperationEnvironment operation;
    rasp_renderer->Update(Calculated().date_time_local, operation);
  }

  const auto &terrain_settings = GetMapSettings().terrain;
  if (rasp_renderer->Generate(render_projection, terrain_settings))
    rasp_renderer->Draw(canvas, render_projection);
}

inline void
MapWindow::RenderTopography(Canvas &canvas) noexcept
{
  if (topography_renderer != nullptr && GetMapSettings().topography_enabled)
    topography_renderer->Draw(canvas, render_projection);
}

inline void
MapWindow::RenderTopographyLabels(Canvas &canvas) noexcept
{
  if (topography_renderer != nullptr && GetMapSettings().topography_enabled)
    topography_renderer->DrawLabels(canvas, render_projection, label_block);
}

inline void
MapWindow::RenderOverlays([[maybe_unused]] Canvas &canvas) noexcept
{
#ifdef ENABLE_OPENGL
  if (overlay)
    overlay->Draw(canvas, render_projection);
#endif
}

inline void
MapWindow::RenderFinalGlideShading(Canvas &canvas) noexcept
{
  if (terrain != nullptr &&
      Calculated().terrain_valid)
      DrawTerrainAbove(canvas);
}

inline void
MapWindow::RenderAirspace(Canvas &canvas) noexcept
{
  if (GetMapSettings().airspace.enable) {
    airspace_renderer.Draw(canvas,
#ifndef ENABLE_OPENGL
                           buffer_canvas,
#endif
                           render_projection,
                           Basic(), Calculated(),
                           GetComputerSettings().airspace,
                           GetMapSettings().airspace);

    airspace_label_renderer.Draw(canvas,
                                 render_projection,
                                 Basic(), Calculated(),
                                 GetComputerSettings().airspace,
                                 GetMapSettings().airspace);
  }
}

inline void
MapWindow::RenderNOAAStations(Canvas &canvas) noexcept
{
#ifdef HAVE_NOAA
  if (noaa_store == nullptr)
    return;

  for (auto it = noaa_store->begin(), end = noaa_store->end(); it != end; ++it)
    if (it->parsed_metar_available && it->parsed_metar.location_available)
      if (auto pt = render_projection.GeoToScreenIfVisible(it->parsed_metar.location))
        look.noaa.icon.Draw(canvas, *pt);
#else
  (void)canvas;
#endif
}

inline void
MapWindow::DrawWaves(Canvas &canvas) noexcept
{
  const WaveRenderer renderer(look.wave);

#ifdef HAVE_SKYLINES_TRACKING
  if (skylines_data != nullptr) {
    const std::lock_guard lock{skylines_data->mutex};
    renderer.Draw(canvas, render_projection, *skylines_data);
  }
#endif

  renderer.Draw(canvas, render_projection, Calculated().wave);
}

inline void
MapWindow::RenderGlide(Canvas &canvas) noexcept
{
  // draw red cross on glide through terrain marker
  if (Calculated().terrain_valid)
    DrawGlideThroughTerrain(canvas);
}

void
MapWindow::Render(Canvas &canvas, const PixelRect &rc) noexcept
{
  const NMEAInfo &basic = Basic();

  // reset label over-write preventer
  label_block.reset();

  render_projection = visible_projection;

  if (!render_projection.IsValid()) {
    canvas.ClearWhite();
    return;
  }

  // Calculate screen position of the aircraft with smooth interpolation
  // Interpolates between GPS fixes (typically 1 Hz) for smooth movement at 10 Hz
  PixelPoint aircraft_pos{0,0};
  Angle aircraft_heading = Angle::Zero();
  if (basic.location_available) {
    GeoPoint aircraft_location = basic.location;
    
    // GPS fixes are updated in the main thread (UpdateProjection), not here in the draw thread
    // We just use the stored fixes for interpolation - don't try to detect new fixes here
    // The stored fixes (prev_gps_location, current_gps_location) are updated by the main thread
    // when location changes are detected in UpdateProjection()
    
    // Check if we have valid stored GPS fixes for interpolation
    const bool has_valid_fixes = has_prev_gps && prev_gps_location.IsValid() && current_gps_location.IsValid();
    
    if (!has_valid_fixes) {
      // No stored fixes yet - use current location directly
      // This happens on first GPS fix before UpdateProjection() has stored the fixes
      aircraft_location = basic.location;
      aircraft_heading = basic.attitude.heading_available ? basic.attitude.heading : Angle::Zero();
    } else {
      // Use stored GPS fixes for interpolation (updated by main thread in UpdateProjection)
      // Always interpolate between prev and current - don't detect new fixes here
      // Between GPS fixes - smoothly interpolate from previous GPS location to current GPS location
      // Use wall-clock time for smooth constant-speed interpolation
      // Render "one fix behind" - interpolate between prev and current, not use current directly
      
      // Default to prev location (one fix behind) - this ensures smooth movement
      aircraft_location = prev_gps_location;
      aircraft_heading = has_prev_gps_heading ? prev_gps_heading : 
                        (basic.attitude.heading_available ? basic.attitude.heading : Angle::Zero());
      
      // Try to interpolate if clock is available
      if (last_gps_fix_clock.IsDefined()) {
        // Calculate elapsed wall-clock time since prev was set (when clock was last updated)
        const auto elapsed_wall = last_gps_fix_clock.Elapsed();
        // Elapsed() can return negative if clock was never updated, so check for that
        if (elapsed_wall.count() >= 0) {
          const double elapsed_seconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed_wall).count();
        
          // Calculate total GPS time between previous and current GPS fixes
          // Use estimated interval if GPS time isn't available
          double gps_interval = 1.0; // Default to 1 second (typical GPS update rate)
          if (prev_gps_time.IsDefined() && current_gps_time.IsDefined()) {
            const auto total_time = current_gps_time - prev_gps_time;
            double interval = total_time.count();
            if (interval > 0 && interval <= 5.0) {
              gps_interval = interval;
            }
          }
          
          if (elapsed_seconds >= 0 && elapsed_seconds < gps_interval * 1.5) {
            // Calculate interpolation factor (0 = at prev location, 1 = at current location)
            // Use wall-clock time for smooth constant-speed movement
            double t = elapsed_seconds / gps_interval;
            t = std::clamp(t, 0.0, 1.0);
            
            // Interpolate between previous GPS fix location and current GPS fix location
            // This provides smooth 10Hz movement between 1Hz GPS fixes
            aircraft_location = prev_gps_location.Interpolate(current_gps_location, t);
            
            // Interpolate heading angle smoothly
            if (has_prev_gps_heading) {
              aircraft_heading = prev_gps_heading.Fraction(current_gps_heading, t);
            } else {
              aircraft_heading = basic.attitude.heading_available ? basic.attitude.heading : Angle::Zero();
            }
          }
          // If elapsed_seconds >= gps_interval * 1.5, we already set aircraft_location to prev_gps_location above
        }
        // If elapsed_wall.count() < 0, we already set aircraft_location to prev_gps_location above
      }
      // If clock not defined, we already set aircraft_location to prev_gps_location above
    }
    
    // Convert interpolated location to screen coordinates
    aircraft_pos = render_projection.GeoToScreen(aircraft_location);
    
    // Update state for next frame
    last_aircraft_pos = aircraft_pos;
    has_last_aircraft_pos = true;
  } else {
    // No GPS fix - reset state
    has_last_aircraft_pos = false;
    if (!basic.time_available) {
      last_gps_time = TimeStamp::Undefined();
      has_oldest_gps = false;
      has_prev_gps = false;
      has_oldest_gps_heading = false;
      has_prev_gps_heading = false;
      has_current_gps_heading = false;
    }
    aircraft_heading = basic.attitude.heading_available ? basic.attitude.heading : Angle::Zero();
  }

  // General layout principles:
  // - lower elevation drawn on bottom layers
  // - increasing elevation drawn above
  // - increasing importance drawn above
  // - attempt to not obscure text

  //////////////////////////////////////////////// items on ground

  // Render terrain, groundline and topography
  draw_sw.Mark("RenderTerrain");
  RenderTerrain(canvas);

  draw_sw.Mark("RenderRasp");
  RenderRasp(canvas);

  draw_sw.Mark("RenderTopography");
  RenderTopography(canvas);

  draw_sw.Mark("RenderOverlays");
  RenderOverlays(canvas);

  draw_sw.Mark("DrawNOAAStations");
  RenderNOAAStations(canvas);

  //////////////////////////////////////////////// glide range info

  draw_sw.Mark("RenderFinalGlideShading");
  RenderFinalGlideShading(canvas);

  //////////////////////////////////////////////// airspace

  // Render airspace
  draw_sw.Mark("RenderAirspace");
  RenderAirspace(canvas);

  //////////////////////////////////////////////// task

  // Render task, waypoints
  draw_sw.Mark("DrawContest");
  DrawContest(canvas);

  draw_sw.Mark("DrawTask");
  DrawTask(canvas);

  draw_sw.Mark("DrawWaypoints");
  DrawWaypoints(canvas);

  //////////////////////////////////////////////// aircraft level items
  // Render the snail trail
  RenderTrail(canvas, aircraft_pos);

  DrawWaves(canvas);

  // Render estimate of thermal location
  DrawThermalEstimate(canvas);

  //////////////////////////////////////////////// text items
  // Render topography on top of airspace, to keep the text readable
  draw_sw.Mark("RenderTopographyLabels");
  RenderTopographyLabels(canvas);

  //////////////////////////////////////////////// navigation overlays
  // Render glide through terrain range
  draw_sw.Mark("RenderGlide");
  RenderGlide(canvas);

  // Render track bearing (projected track ground/air relative)
  draw_sw.Mark("DrawTrackBearing");
  RenderTrackBearing(canvas, aircraft_pos);
  
  // Render Detour cost markers
  draw_sw.Mark("RenderMisc1");
  DrawTaskOffTrackIndicator(canvas);

  draw_sw.Mark("RenderMisc2");
  DrawBestCruiseTrack(canvas, aircraft_pos);

  // Draw wind vector at aircraft
  if (basic.location_available)
    DrawWind(canvas, aircraft_pos, rc);

  // Render compass
  DrawCompass(canvas, rc);

  //////////////////////////////////////////////// traffic
  // Draw traffic

#ifdef HAVE_SKYLINES_TRACKING
  DrawSkyLinesTraffic(canvas);
#endif

  DrawGLinkTraffic(canvas);

  DrawTeammate(canvas);

  DrawFLARMTraffic(canvas, aircraft_pos);

  //////////////////////////////////////////////// own aircraft
  // Finally, draw you!
  if (basic.location_available) {
    AircraftRenderer::Draw(canvas, GetMapSettings(), look.aircraft,
                           aircraft_heading - render_projection.GetScreenAngle(),
                           aircraft_pos);
  }

  //////////////////////////////////////////////// important overlays
  // Draw intersections on top of aircraft
  airspace_renderer.DrawIntersections(canvas, render_projection);
}
