// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "WaypointDialogs.hpp"
#include "WaypointInfoWidget.hpp"
#include "WaypointCommandsWidget.hpp"
#include "Dialogs/WidgetDialog.hpp"
#include "Widget/TabWidget.hpp"
#include "Widget/ButtonWidget.hpp"
#include "Widget/TwoWidgets.hpp"
#include "Widget/LargeTextWidget.hpp"
#include "Widget/CreateWindowWidget.hpp"
#include "Engine/GlideSolvers/GlideState.hpp"
#include "Engine/GlideSolvers/MacCready.hpp"
#include "Engine/GlideSolvers/GlideResult.hpp"
#include "Engine/Util/Gradient.hpp"
#include "NMEA/MoreData.hpp"
#include "NMEA/Derived.hpp"
#include "Computer/Settings.hpp"
#include "Math/SunEphemeris.hpp"
#include "Math/Util.hpp"
#include "util/StaticString.hxx"
#include "util/Macros.hpp"
#include "Formatter/UserUnits.hpp"
#include "Formatter/AngleFormatter.hpp"
#include "Formatter/UserGeoPointFormatter.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "Look/MapLook.hpp"
#include "Look/WaypointLook.hpp"
#include "Look/Look.hpp"
#include "Form/Panel.hpp"
#include "Form/Draw.hpp"
#include "Form/Button.hpp"
#include "Renderer/SymbolButtonRenderer.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "Renderer/WaypointIconRenderer.hpp"
#include "Renderer/BackgroundRenderer.hpp"
#include "Renderer/AirspaceRenderer.hpp"
#include "Renderer/TaskRenderer.hpp"
#include "Renderer/TaskPointRenderer.hpp"
#include "Renderer/OZRenderer.hpp"
#include "Renderer/TrafficRenderer.hpp"
#include "Renderer/TextInBox.hpp"
#include "Renderer/WaypointRenderer.hpp"
#include "Renderer/TrailRenderer.hpp"
#include "Renderer/LabelBlock.hpp"
#include "Topography/TopographyRenderer.hpp"
#include "Computer/GlideComputer.hpp"
#include "Widget/ManagedWidget.hpp"
#include "Widget/Widget.hpp"
#include "Engine/Waypoint/Waypoint.hpp"
#include "LocalPath.hpp"
#include "ui/canvas/Canvas.hpp"
#include "ui/canvas/Bitmap.hpp"
#include "ui/canvas/Color.hpp"
#include "ui/canvas/Pen.hpp"
#include "ui/window/BufferWindow.hpp"
#ifndef ENABLE_OPENGL
#include "ui/canvas/BufferCanvas.hpp"
#include "ui/canvas/WindowCanvas.hpp"
#endif
#include "Projection/WindowProjection.hpp"
#include "Projection/MapWindowProjection.hpp"
#include "Screen/Layout.hpp"
#include "Look/TrafficLook.hpp"
#include "FLARM/Friends.hpp"
#include "Task/ProtectedTaskManager.hpp"
#include "Engine/Task/TaskManager.hpp"
#include "Engine/Task/Ordered/OrderedTask.hpp"
#include "Engine/Task/AbstractTask.hpp"
#include "BackendComponents.hpp"
#include "ui/event/KeyCode.hpp"
#include "ui/control/LargeTextWindow.hpp"
#include "ui/control/List.hpp"
#include "MainWindow.hpp"
#include "Interface.hpp"
#include "Components.hpp"
#include "Task/ProtectedTaskManager.hpp"
#include "Language/Language.hpp"
#include "Waypoint/LastUsed.hpp"
#include "Profile/Current.hpp"
#include "Profile/Map.hpp"
#include "Profile/Keys.hpp"
#include "system/RunFile.hpp"
#include "system/Path.hpp"
#include "system/ConvertPathName.hpp"
#include "LogFile.hpp"
#include "util/StringPointer.hxx"
#include "util/AllocatedString.hxx"
#include "BackendComponents.hpp"
#include "Pan.hpp"
#include "DataComponents.hpp"
#include "Terrain/RasterTerrain.hpp"
#include "MapSettings.hpp"
#include "Renderer/WaypointRendererSettings.hpp"

#include <vector>

#ifdef ANDROID
#include "Android/NativeView.hpp"
#include "Android/Main.hpp"
#endif

static bool
ActivatePan(const Waypoint &waypoint)
{
  return PanTo(waypoint.location);
}

#ifdef HAVE_RUN_FILE

class WaypointExternalFileListHandler final
  : public ListItemRenderer, public ListCursorHandler {
  const WaypointPtr waypoint;

  TextRowRenderer row_renderer;

public:
  explicit WaypointExternalFileListHandler(WaypointPtr _waypoint)
    :waypoint(std::move(_waypoint)) {}

  auto &GetRowRenderer() noexcept {
    return row_renderer;
  }

  /* virtual methods from class ListItemRenderer */
  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

  bool CanActivateItem([[maybe_unused]] unsigned index) const noexcept override {
    return true;
  }

  void OnActivateItem(unsigned index) noexcept override;
};

void
WaypointExternalFileListHandler::OnActivateItem(unsigned i) noexcept
{
  auto file = waypoint->files_external.begin();
  std::advance(file, i);

#ifdef ANDROID
  /* on Android, the ContentProvider API needs to be used to give
     other apps access to this file */
  native_view->OpenWaypointFile(Java::GetEnv(), waypoint->id, file->c_str());
#else
  RunFile(LocalPath(file->c_str()).c_str());
#endif
}

void
WaypointExternalFileListHandler::OnPaintItem(Canvas &canvas,
                                             const PixelRect paint_rc,
                                             unsigned i) noexcept
{
  auto file = waypoint->files_external.begin();
  std::advance(file, i);
  row_renderer.DrawTextRow(canvas, paint_rc, file->c_str());
}
#endif

/**
 * Small map widget showing waypoint location
 * Follows the TargetMapWindow pattern for rendering
 */
class WaypointMapWidget final : public BufferWindow {
  const WaypointPtr waypoint;
  const MapLook &map_look;
  const WaypointLook &waypoint_look;
  MapWindowProjection projection;
  BackgroundRenderer background;
  AirspaceRenderer airspace_renderer;
  WaypointRenderer waypoint_renderer;
  TrailRenderer trail_renderer;
  LabelBlock label_block;
  TopographyRenderer *topography_renderer = nullptr;
#ifndef ENABLE_OPENGL
  BufferCanvas buffer_canvas;
#endif

  public:
  WaypointMapWidget(WaypointPtr _waypoint,
                    const MapLook &_map_look,
                    const WaypointLook &_waypoint_look) noexcept
    :waypoint(std::move(_waypoint)),
     map_look(_map_look),
     waypoint_look(_waypoint_look),
     airspace_renderer(map_look.airspace),
     waypoint_renderer(nullptr, waypoint_look),
     trail_renderer(map_look.trail)
  {
    // Set terrain if available
    if (data_components != nullptr && data_components->terrain != nullptr)
      background.SetTerrain(data_components->terrain.get());
    
    // Set airspaces and warning manager if available
    if (data_components != nullptr && data_components->airspaces != nullptr)
      airspace_renderer.SetAirspaces(data_components->airspaces.get());
    if (backend_components != nullptr)
      airspace_renderer.SetAirspaceWarnings(backend_components->GetAirspaceWarnings());
    
    // Set waypoints if available
    if (data_components != nullptr && data_components->waypoints != nullptr)
      waypoint_renderer.SetWaypoints(data_components->waypoints.get());
    
    // Set topography if available
    if (data_components != nullptr && data_components->topography != nullptr)
      topography_renderer = new TopographyRenderer(*data_components->topography,
                                                    map_look.topography);
  }

  ~WaypointMapWidget() noexcept {
    delete topography_renderer;
  }

  void Create(ContainerWindow &parent, const PixelRect &rc,
              WindowStyle style) noexcept {
    BufferWindow::Create(parent, rc, style);
  }

  protected:
  void OnCreate() override {
    BufferWindow::OnCreate();
#ifndef ENABLE_OPENGL
    WindowCanvas canvas(*this);
    buffer_canvas.Create(canvas);
#endif
    UpdateProjection();
  }

  void OnResize(PixelSize new_size) noexcept override {
    BufferWindow::OnResize(new_size);
#ifndef ENABLE_OPENGL
    if (buffer_canvas.IsDefined())
      buffer_canvas.Grow(new_size);
#endif
    UpdateProjection();
  }

  void OnDestroy() noexcept override {
#ifndef ENABLE_OPENGL
    buffer_canvas.Destroy();
#endif
    BufferWindow::OnDestroy();
  }

private:
  // Rendering methods following TargetMapWindow pattern
  void RenderTerrain(Canvas &canvas) noexcept {
    const MapSettings &map_settings = CommonInterface::GetMapSettings();
    background.SetShadingAngle(projection, map_settings.terrain,
                               CommonInterface::Calculated());
    background.Draw(canvas, projection, map_settings.terrain);
  }

  void RenderTopography(Canvas &canvas) noexcept {
    const MapSettings &map_settings = CommonInterface::GetMapSettings();
    if (topography_renderer != nullptr && map_settings.topography_enabled)
      topography_renderer->Draw(canvas, projection);
  }

  void RenderTopographyLabels(Canvas &canvas) noexcept {
    const MapSettings &map_settings = CommonInterface::GetMapSettings();
    if (topography_renderer != nullptr && map_settings.topography_enabled)
      topography_renderer->DrawLabels(canvas, projection, label_block);
  }

  void RenderAirspace(Canvas &canvas) noexcept {
    const MapSettings &map_settings = CommonInterface::GetMapSettings();
    if (!map_settings.airspace.enable)
      return;

    const MoreData &basic = CommonInterface::Basic();
    const DerivedInfo &calculated = CommonInterface::Calculated();
    const ComputerSettings &computer_settings = CommonInterface::GetComputerSettings();
    
#ifndef ENABLE_OPENGL
    // Ensure buffer canvas is initialized (should be done in OnCreate, but check just in case)
    if (!buffer_canvas.IsDefined()) {
      const PixelRect rc = GetClientRect();
      buffer_canvas.Create(canvas, rc.GetSize());
    } else {
      const PixelRect rc = GetClientRect();
      if (buffer_canvas.GetSize() != rc.GetSize())
        buffer_canvas.Resize(rc.GetSize());
    }
#endif
    
    airspace_renderer.Draw(canvas,
#ifndef ENABLE_OPENGL
                           buffer_canvas,
#endif
                           projection,
                           basic, calculated,
                           computer_settings.airspace,
                           map_settings.airspace);
  }

  void DrawTask(Canvas &canvas) noexcept {
    if (backend_components == nullptr || backend_components->protected_task_manager == nullptr)
      return;

    ProtectedTaskManager::Lease task_manager(*backend_components->protected_task_manager);
    const AbstractTask *task = task_manager->GetActiveTask();
    if (task && !IsError(task->CheckTask())) {
      const MapSettings &map_settings = CommonInterface::GetMapSettings();
      const FlatProjection dummy_flat_projection{};
      const auto &flat_projection = task->GetType() == TaskType::ORDERED
        ? ((const OrderedTask *)task)->GetTaskProjection()
        : dummy_flat_projection;

      OZRenderer ozv(map_look.task, airspace_renderer.GetLook(),
                     map_settings.airspace);
      TaskPointRenderer tpv(canvas, projection, map_look.task,
                            flat_projection,
                            ozv, false,
                            TaskPointRenderer::TargetVisibility::ALL,
                            CommonInterface::Basic().GetLocationOrInvalid());
      tpv.SetTaskFinished(CommonInterface::Calculated().task_stats.task_finished);
      TaskRenderer dv(tpv, projection.GetScreenBounds());
      dv.Draw(*task);
    }
  }

  void DrawWaypoints(Canvas &canvas) noexcept {
    const MapSettings &map_settings = CommonInterface::GetMapSettings();
    const ComputerSettings &computer_settings = CommonInterface::GetComputerSettings();
    
    WaypointRendererSettings settings = map_settings.waypoint;
    settings.display_text_type = WaypointRendererSettings::DisplayTextType::NAME;

    waypoint_renderer.Render(canvas, label_block,
                             projection, settings,
                             computer_settings.polar,
                             computer_settings.task,
                             CommonInterface::Basic(), CommonInterface::Calculated(),
                             backend_components != nullptr ? backend_components->protected_task_manager.get() : nullptr,
                             nullptr);
  }

  void RenderTrail(Canvas &canvas) noexcept {
    if (backend_components == nullptr || backend_components->glide_computer == nullptr)
      return;

    const MoreData &basic = CommonInterface::Basic();
    const auto min_time = std::max(basic.time - std::chrono::minutes{10},
                                   TimeStamp{});
    trail_renderer.Draw(canvas, backend_components->glide_computer->GetTraceComputer(),
                        projection, min_time);
  }

  void DrawTraffic(Canvas &canvas) noexcept {
    const MapSettings &map_settings = CommonInterface::GetMapSettings();
    if (!map_settings.show_flarm_on_map)
      return;

    const MoreData &basic = CommonInterface::Basic();
    const TrafficList &flarm = basic.flarm.traffic;
    const TrafficLook &traffic_look = UIGlobals::GetLook().traffic;
    
    canvas.Select(*traffic_look.font);
    
    // Draw FLARM traffic
    for (const auto &traffic : flarm.list) {
      if (!traffic.location_available || !traffic.relative_east)
        continue;
      
      if (auto p = projection.GeoToScreenIfVisible(traffic.location)) {
        auto color = FlarmFriends::GetFriendColor(traffic.id);
        TrafficRenderer::Draw(canvas, traffic_look, false, traffic,
                              traffic.track - projection.GetScreenAngle(),
                              color, *p);
      }
    }
  }

  void DrawWaypointIcon(Canvas &canvas) noexcept {
    const PixelRect rc = GetClientRect();
    const PixelPoint center = rc.GetCenter();
    const WaypointRendererSettings &waypoint_settings = 
      CommonInterface::GetMapSettings().waypoint;
    WaypointIconRenderer icon_renderer(waypoint_settings, waypoint_look, canvas);
    icon_renderer.Draw(*waypoint, center);
  }

  void OnPaintBuffer(Canvas &canvas) noexcept override {
    const PixelRect rc = GetClientRect();
    
    // Clear background
    canvas.ClearWhite();

    if (!projection.IsValid()) {
      // Draw frame even if projection is invalid - use light gray
      canvas.Select(Pen(1, COLOR_LIGHT_GRAY));
      canvas.DrawOutlineRectangle(rc);
      return;
    }

    // Reset label over-write preventer
    label_block.reset();

    // Render terrain, groundline and topography
    RenderTerrain(canvas);
    RenderTopography(canvas);

    // Render airspace
    RenderAirspace(canvas);

    // Render task, waypoints
    DrawTask(canvas);
    DrawWaypoints(canvas);

    // Render the snail trail
    RenderTrail(canvas);

    // Render topography labels on top of airspace, to keep the text readable
    RenderTopographyLabels(canvas);

    // Draw traffic (FLARM)
    DrawTraffic(canvas);

    // Draw the waypoint icon centered (on top of everything)
    DrawWaypointIcon(canvas);

    // Draw frame around the map - use light gray for a less prominent frame
    canvas.Select(Pen(1, COLOR_LIGHT_GRAY));
    canvas.DrawOutlineRectangle(rc);
  }

private:
  void UpdateProjection() noexcept {
    PixelRect rc = GetClientRect();
    if (rc.GetWidth() == 0 || rc.GetHeight() == 0)
      return;

    projection.SetScreenSize(rc.GetSize());
    projection.SetScreenOrigin(rc.GetCenter());
    projection.SetGeoLocation(waypoint->location);
    
    // Set scale to show a close-up view (highest zoom level)
    // Use a small radius to ensure maximum zoom in
    projection.SetScaleFromRadius(5000); // 5km radius for highest zoom
    projection.UpdateScreenBounds();
    
    Invalidate();
  }
};

// Helper functions matching WaypointInfoWidget.cpp
// These are duplicated here because they're static in WaypointInfoWidget.cpp
[[gnu::const]]
static const TCHAR *
FormatGlideResult(TCHAR *buffer, size_t size,
                  const GlideResult &result,
                  const GlideSettings &settings) noexcept
{
  switch (result.validity) {
  case GlideResult::Validity::OK:
    FormatRelativeUserAltitude(result.SelectAltitudeDifference(settings),
                               buffer, size);
    return buffer;

  case GlideResult::Validity::WIND_EXCESSIVE:
  case GlideResult::Validity::MACCREADY_INSUFFICIENT:
    return _("Too much wind");

  case GlideResult::Validity::NO_SOLUTION:
    return _("No solution");
  }

  return nullptr;
}

[[gnu::const]]
static BrokenTime
BreakHourOfDay(double t) noexcept
{
  /* depending on the time zone, the SunEphemeris library may return a
     negative time of day; the following check catches this before we
     cast the value to "unsigned" */
  if (t < 0)
    t += 24;

  unsigned i = uround(t * 3600);

  BrokenTime result;
  result.hour = i / 3600;
  i %= 3600;
  result.minute = i / 60;
  result.second = i % 60;
  return result;
}

// Waypoint type names matching dlgWaypointEdit.cpp waypoint_types array
[[gnu::const]]
static const TCHAR *
GetWaypointTypeName(Waypoint::Type type) noexcept
{
  switch (type) {
  case Waypoint::Type::NORMAL:
    return _("Turnpoint");
  case Waypoint::Type::AIRFIELD:
    return _("Airport");
  case Waypoint::Type::OUTLANDING:
    return _("Landable");
  case Waypoint::Type::MOUNTAIN_PASS:
    return _("Mountain Pass");
  case Waypoint::Type::MOUNTAIN_TOP:
    return _("Mountain Top");
  case Waypoint::Type::OBSTACLE:
    return _("Transmitter Mast");
  case Waypoint::Type::TOWER:
    return _("Tower");
  case Waypoint::Type::TUNNEL:
    return _("Tunnel");
  case Waypoint::Type::BRIDGE:
    return _("Bridge");
  case Waypoint::Type::POWERPLANT:
    return _("Power Plant");
  case Waypoint::Type::VOR:
    return _("VOR");
  case Waypoint::Type::NDB:
    return _("NDB");
  case Waypoint::Type::DAM:
    return _("Dam");
  case Waypoint::Type::CASTLE:
    return _("Castle");
  case Waypoint::Type::INTERSECTION:
    return _("Intersection");
  case Waypoint::Type::MARKER:
    return _("Marker");
  case Waypoint::Type::REPORTING_POINT:
    return _("Control Point");
  case Waypoint::Type::PGTAKEOFF:
    return _("PG Take Off");
  case Waypoint::Type::PGLANDING:
    return _("PG Landing Zone");
  case Waypoint::Type::THERMAL_HOTSPOT:
    return _("Thermal Hotspot");
  }
  return _("Unknown");
}

/**
 * WaypointInfoWidget without the comment field (since it's shown with the map)
 * Reuses the same logic as WaypointInfoWidget::Prepare but skips the comment
 */
class WaypointInfoWidgetNoComment final : public RowFormWidget {
  const WaypointPtr waypoint;

public:
  WaypointInfoWidgetNoComment(const DialogLook &look, WaypointPtr _waypoint) noexcept
    :RowFormWidget(look), waypoint(std::move(_waypoint)) {}

  void AddGlideResult(const TCHAR *label, const GlideResult &result) noexcept {
    // Reuses the same logic as WaypointInfoWidget::AddGlideResult
    const ComputerSettings &settings = CommonInterface::GetComputerSettings();
    TCHAR buffer[64];
    AddReadOnly(label, nullptr,
                FormatGlideResult(buffer, ARRAY_SIZE(buffer),
                                result, settings.task.glide));
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    RowFormWidget::Prepare(parent, rc);

    const MoreData &basic = CommonInterface::Basic();
    const DerivedInfo &calculated = CommonInterface::Calculated();
    const ComputerSettings &settings = CommonInterface::GetComputerSettings();

    StaticString<64> buffer;

    // Skip comment - it's shown with the map

    // Add waypoint type at the top
    AddReadOnly(_("Type"), nullptr, GetWaypointTypeName(waypoint->type));

    if (waypoint->radio_frequency.Format(buffer.buffer(),
                                        buffer.capacity()) != nullptr) {
      buffer += _T(" MHz");
      AddReadOnly(_("Radio frequency"), nullptr, buffer);
    }

    if (waypoint->runway.IsDirectionDefined())
      buffer.UnsafeFormat(_T("%02u"), waypoint->runway.GetDirectionName());
    else
      buffer.clear();

    if (waypoint->runway.IsLengthDefined()) {
      if (!buffer.empty())
        buffer += _T("; ");

      TCHAR length_buffer[16];
      FormatSmallUserDistance(length_buffer, waypoint->runway.GetLength());
      buffer += length_buffer;
    }

    if (!buffer.empty())
      AddReadOnly(_("Runway"), nullptr, buffer);

    if (!waypoint->shortname.empty())
      AddReadOnly(_("Short Name"), nullptr, waypoint->shortname.c_str());

    if (FormatGeoPoint(waypoint->location,
                       buffer.buffer(), buffer.capacity()) != nullptr)
      AddReadOnly(_("Location"), nullptr, buffer);

    if (waypoint->has_elevation)
      AddReadOnly(_("Elevation"), nullptr, FormatUserAltitude(waypoint->elevation));
    else
      AddReadOnly(_("Elevation"), nullptr, _T("?"));

    if (basic.time_available && basic.date_time_utc.IsDatePlausible()) {
      const SunEphemeris::Result sun =
        SunEphemeris::CalcSunTimes(waypoint->location, basic.date_time_utc,
                                   settings.utc_offset);

      const BrokenTime sunrise = BreakHourOfDay(sun.time_of_sunrise);
      const BrokenTime sunset = BreakHourOfDay(sun.time_of_sunset);

      buffer.UnsafeFormat(_T("%02u:%02u - %02u:%02u"),
                          sunrise.hour, sunrise.minute,
                          sunset.hour, sunset.minute);
      AddReadOnly(_("Daylight time"), nullptr, buffer);
    }

    if (basic.location_available) {
      const GeoVector vector = basic.location.DistanceBearing(waypoint->location);

      FormatBearing(buffer.buffer(), buffer.capacity(),
                    vector.bearing,
                    FormatUserDistanceSmart(vector.distance));
      AddReadOnly(_("Bearing and Distance"), nullptr, buffer);
    }

    if (basic.location_available && basic.NavAltitudeAvailable() &&
        settings.polar.glide_polar_task.IsValid() &&
        waypoint->has_elevation) {
      const GlideState glide_state(basic.location.DistanceBearing(waypoint->location),
                                   waypoint->elevation + settings.task.safety_height_arrival,
                                   basic.nav_altitude,
                                   calculated.GetWindOrZero());

      GlidePolar gp0 = settings.polar.glide_polar_task;
      gp0.SetMC(0);
      AddGlideResult(_("Alt. diff. MC 0"),
                     MacCready::Solve(settings.task.glide,
                                      gp0, glide_state));

      AddGlideResult(_("Alt. diff. MC safety"),
                     MacCready::Solve(settings.task.glide,
                                      calculated.glide_polar_safety,
                                      glide_state));

      AddGlideResult(_("Alt. diff. MC current"),
                     MacCready::Solve(settings.task.glide,
                                      settings.polar.glide_polar_task,
                                      glide_state));
    }

    if (basic.location_available && basic.NavAltitudeAvailable() &&
        waypoint->has_elevation) {
      const TaskBehaviour &task_behaviour =
        CommonInterface::GetComputerSettings().task;

      const auto safety_height = task_behaviour.safety_height_arrival;
      const auto target_altitude = waypoint->elevation + safety_height;
      const auto delta_h = basic.nav_altitude - target_altitude;
      if (delta_h > 0) {
        const auto distance = basic.location.Distance(waypoint->location);
        const auto gr = distance / delta_h;
        if (GradientValid(gr)) {
          buffer.UnsafeFormat(_T("%.1f"), (double)gr);
          AddReadOnly(_("Required glide ratio"), nullptr, buffer);
        }
      }
    }
  }
};

/**
 * Custom layout widget that ensures info fields fit and map scales to fill remaining space
 * This works around TwoWidgets limitations with unlimited maximum sizes
 */
class MapInfoLayoutWidget final : public NullWidget {
  std::unique_ptr<Widget> map_widget;
  std::unique_ptr<Widget> info_widget;
  PixelRect current_rc;

public:
  MapInfoLayoutWidget(std::unique_ptr<Widget> &&_map_widget,
                      std::unique_ptr<Widget> &&_info_widget) noexcept
    :map_widget(std::move(_map_widget)),
     info_widget(std::move(_info_widget))
  {}

  PixelSize GetMinimumSize() const noexcept override {
    PixelSize map_min = map_widget->GetMinimumSize();
    PixelSize info_min = info_widget->GetMinimumSize();
    return PixelSize{std::max(map_min.width, info_min.width), 
                     map_min.height + info_min.height};
  }

  PixelSize GetMaximumSize() const noexcept override {
    // Allow unlimited growth so map can scale
    return PixelSize{4096, 4096};
  }

  void Initialise(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    current_rc = rc;
    // Use helper methods to calculate rects
    PixelRect info_rc = GetInfoRect(rc);
    PixelRect map_rc = GetMapRect(rc);
    
    map_widget->Initialise(parent, map_rc);
    info_widget->Initialise(parent, info_rc);
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    current_rc = rc;
    // Prepare widgets with their calculated rects
    map_widget->Prepare(parent, GetMapRect(rc));
    info_widget->Prepare(parent, GetInfoRect(rc));
  }

  void Unprepare() noexcept override {
    map_widget->Unprepare();
    info_widget->Unprepare();
  }

  void Show(const PixelRect &rc) noexcept override {
    current_rc = rc;
    map_widget->Show(GetMapRect(rc));
    info_widget->Show(GetInfoRect(rc));
  }

  void Hide() noexcept override {
    map_widget->Hide();
    info_widget->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    current_rc = rc;
    map_widget->Move(GetMapRect(rc));
    info_widget->Move(GetInfoRect(rc));
  }

  bool SetFocus() noexcept override {
    return map_widget->SetFocus() || info_widget->SetFocus();
  }

  bool HasFocus() const noexcept override {
    return map_widget->HasFocus() || info_widget->HasFocus();
  }

  bool KeyPress(unsigned key_code) noexcept override {
    return map_widget->KeyPress(key_code) || info_widget->KeyPress(key_code);
  }

private:
  PixelRect GetInfoRect(const PixelRect &rc) const noexcept {
    PixelSize info_min = info_widget->GetMinimumSize();
    PixelSize info_max = info_widget->GetMaximumSize();
    
    // Info fields get their constrained maximum height (or min if max is unlimited)
    // Positioned at bottom, ensuring they always fit
    unsigned info_height = info_min.height;
    if (info_max.height > 0) {
      // Use the constrained maximum, but ensure it fits in available space
      info_height = std::min(info_max.height, rc.GetHeight());
      info_height = std::max(info_min.height, info_height);
    }
    
    PixelRect info_rc = rc;
    info_rc.top = rc.bottom - info_height;
    return info_rc;
  }

  PixelRect GetMapRect(const PixelRect &rc) const noexcept {
    PixelRect info_rc = GetInfoRect(rc);
    PixelRect map_rc = rc;
    // Add spacing between map and info fields
    const unsigned spacing = Layout::GetTextPadding() * 2; // Use 2x text padding for separation
    // Ensure we don't create an invalid rect (map_rc.bottom < map_rc.top)
    if (info_rc.top > int(spacing)) {
      map_rc.bottom = info_rc.top - int(spacing);
    } else {
      map_rc.bottom = info_rc.top; // No spacing if not enough room
    }
    return map_rc;
  }
};

/**
 * Widget wrapper that ensures a widget can scale down to very small sizes
 * This is useful for widgets that should scale dynamically but might have large minimums
 */
class ScalableWidget final : public NullWidget {
  std::unique_ptr<Widget> widget;

public:
  ScalableWidget(std::unique_ptr<Widget> &&_widget) noexcept
    :widget(std::move(_widget)) {}

  PixelSize GetMinimumSize() const noexcept override {
    // Get sizes from wrapped widget
    PixelSize min = widget->GetMinimumSize();
    PixelSize max = widget->GetMaximumSize();
    
    // CRITICAL: Ensure min <= max to prevent assertion failures in TwoWidgets
    // If max is 0 (unlimited), we can use min as-is
    // If max is defined and min exceeds it, clamp min to max
    if (max.height > 0 && min.height > max.height)
      min.height = max.height;
    if (max.width > 0 && min.width > max.width)
      min.width = max.width;
    
    // For scalable widgets, allow them to shrink to very small sizes
    // But ensure we still respect the max constraint
    const unsigned scalable_min_height = (max.height > 0 && max.height < 10) ? max.height : 1;
    const unsigned scalable_min_width = (max.width > 0 && max.width < 10) ? max.width : 1;
    
    // Return the smaller of widget's min and our scalable min, but ensure it's <= max
    unsigned final_height = std::min(min.height, scalable_min_height);
    unsigned final_width = std::min(min.width, scalable_min_width);
    
    // Final safety check: ensure final <= max
    if (max.height > 0 && final_height > max.height)
      final_height = max.height;
    if (max.width > 0 && final_width > max.width)
      final_width = max.width;
    
    // Double-check: if max is 0 (unlimited), we're fine. Otherwise ensure min <= max
    assert(max.height == 0 || final_height <= max.height);
    assert(max.width == 0 || final_width <= max.width);
    
    return PixelSize{final_width, final_height};
  }

  PixelSize GetMaximumSize() const noexcept override {
    // For scalable widgets, allow unlimited growth so they can scale to fill available space
    // Use a very large number (4096) which is commonly used in XCSoar for "unlimited"
    // This allows the widget to scale dynamically while satisfying min <= max constraints
    return PixelSize{4096, 4096};
  }

  void Initialise(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    widget->Initialise(parent, rc);
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    widget->Prepare(parent, rc);
  }

  void Unprepare() noexcept override {
    widget->Unprepare();
  }

  void Show(const PixelRect &rc) noexcept override {
    widget->Show(rc);
  }

  void Hide() noexcept override {
    widget->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    widget->Move(rc);
  }

  bool SetFocus() noexcept override {
    return widget->SetFocus();
  }

  bool HasFocus() const noexcept override {
    return widget->HasFocus();
  }

  bool KeyPress(unsigned key_code) noexcept override {
    return widget->KeyPress(key_code);
  }
};

/**
 * Widget wrapper that constrains maximum width to a fraction of available space
 */
class ConstrainedWidthWidget final : public NullWidget {
  std::unique_ptr<Widget> widget;
  double max_width_fraction;

public:
  ConstrainedWidthWidget(std::unique_ptr<Widget> &&_widget, double _max_width_fraction) noexcept
    :widget(std::move(_widget)), max_width_fraction(_max_width_fraction) {}

  PixelSize GetMinimumSize() const noexcept override {
    return widget->GetMinimumSize();
  }

  PixelSize GetMaximumSize() const noexcept override {
    PixelSize max = widget->GetMaximumSize();
    PixelSize min = widget->GetMinimumSize();
    // Limit width to approximately 1/4 of screen width, but ensure it works on small screens
    // For 320x240: use 80px (1/4 of 320), for larger screens use 1/4 up to 450px max
    // Use a conservative approach: ensure at least 80px works on smallest screens (320x240)
    // For larger screens, estimate width from min_screen_pixels
    // In landscape: width ≈ min_screen_pixels * 1.3-1.5, in portrait: width ≈ min_screen_pixels
    const unsigned estimated_screen_width = Layout::landscape 
      ? Layout::min_screen_pixels + Layout::min_screen_pixels / 3  // rough: add 33%
      : Layout::min_screen_pixels;
    const unsigned max_width = std::max(80u, std::min(450u, estimated_screen_width / 4));
    if (max.width == 0 || max.width > max_width)
      max.width = max_width;
    // Ensure max >= min to prevent assertion failures in TwoWidgets
    if (max.height < min.height)
      max.height = min.height;
    if (max.width < min.width)
      max.width = min.width;
    return max;
  }

  void Initialise(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    widget->Initialise(parent, rc);
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    widget->Prepare(parent, rc);
  }

  void Unprepare() noexcept override {
    widget->Unprepare();
  }

  void Show(const PixelRect &rc) noexcept override {
    widget->Show(rc);
  }

  void Hide() noexcept override {
    widget->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    widget->Move(rc);
  }

  bool SetFocus() noexcept override {
    return widget->SetFocus();
  }

  bool HasFocus() const noexcept override {
    return widget->HasFocus();
  }

  bool KeyPress(unsigned key_code) noexcept override {
    return widget->KeyPress(key_code);
  }
};

/**
 * Widget wrapper that constrains maximum height to ensure it always fits
 */
class ConstrainedHeightWidget final : public NullWidget {
  std::unique_ptr<Widget> widget;
  unsigned max_height;

public:
  ConstrainedHeightWidget(std::unique_ptr<Widget> &&_widget, unsigned _max_height) noexcept
    :widget(std::move(_widget)), max_height(_max_height) {}

  PixelSize GetMinimumSize() const noexcept override {
    return widget->GetMinimumSize();
  }

  PixelSize GetMaximumSize() const noexcept override {
    PixelSize max = widget->GetMaximumSize();
    PixelSize min = widget->GetMinimumSize();
    // Limit height to the specified maximum, but ensure it's at least the minimum size
    // This prevents assertion failures in TwoWidgets when min > max
    const unsigned effective_max_height = std::max(max_height, min.height);
    if (max.height == 0 || max.height > effective_max_height)
      max.height = effective_max_height;
    return max;
  }

  void Initialise(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    widget->Initialise(parent, rc);
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    widget->Prepare(parent, rc);
  }

  void Unprepare() noexcept override {
    widget->Unprepare();
  }

  void Show(const PixelRect &rc) noexcept override {
    widget->Show(rc);
  }

  void Hide() noexcept override {
    widget->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    widget->Move(rc);
  }

  bool SetFocus() noexcept override {
    return widget->SetFocus();
  }

  bool HasFocus() const noexcept override {
    return widget->HasFocus();
  }

  bool KeyPress(unsigned key_code) noexcept override {
    return widget->KeyPress(key_code);
  }
};

/**
 * Widget wrapper for waypoint info with map extract beside comment
 */
class WaypointInfoWithMapWidget final : public NullWidget {
  const DialogLook &look;
  const WaypointPtr waypoint;
  std::unique_ptr<Widget> combined_widget;

public:
  WaypointInfoWithMapWidget(const DialogLook &_look, WaypointPtr _waypoint) noexcept
    :look(_look),
     waypoint(std::move(_waypoint))
  {}

  void Initialise(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    // Create map widget
    const MapLook &map_look = UIGlobals::GetMapLook();
    const WaypointLook &waypoint_look = map_look.waypoint;
    
    auto map_widget = std::make_unique<CreateWindowWidget>(
      [waypoint = WaypointPtr(waypoint), &map_look, &waypoint_look]
      (ContainerWindow &parent, const PixelRect &rc, WindowStyle style) {
        auto map_window = std::make_unique<WaypointMapWidget>(waypoint,
                                                               map_look,
                                                               waypoint_look);
        map_window->Create(parent, rc, style);
        return std::unique_ptr<Window>(std::move(map_window));
      });

    // Constrain map widget to maximum 1/4 of width
    auto constrained_map_widget = std::make_unique<ConstrainedWidthWidget>(
      std::move(map_widget), 0.25);

    // Create comment text widget
    const TCHAR *comment_text = waypoint->comment.empty() ? nullptr : waypoint->comment.c_str();
    auto comment_widget = std::make_unique<LargeTextWidget>(look, comment_text);
    
    // Make map and comment scalable so they can shrink when info fields need space
    // This ensures they don't prevent info fields from fitting on small screens
    auto scalable_map_widget = std::make_unique<ScalableWidget>(std::move(constrained_map_widget));
    auto scalable_comment_widget = std::make_unique<ScalableWidget>(std::move(comment_widget));

    // Combine map (left) and comment (right) horizontally
    auto map_comment_horizontal = std::make_unique<TwoWidgets>(std::move(scalable_map_widget),
                                                                std::move(scalable_comment_widget),
                                                                false); // horizontal
    
    // Wrap the horizontal layout in ScalableWidget to ensure it can scale in height
    // This allows the map+comment area to shrink vertically when info fields need space
    auto top_widget = std::make_unique<ScalableWidget>(std::move(map_comment_horizontal));

    // Create info widget without comment
    auto info_widget = std::make_unique<WaypointInfoWidgetNoComment>(look, WaypointPtr(waypoint));
    
    // Constrain info widget height to ensure it always fits on all screen sizes
    // Use a more aggressive approach: prioritize info fields fitting, let map/comment scale down
    // For 320x480 portrait: min_screen_pixels = 320, so use a larger percentage
    // Calculate based on actual screen dimensions - use up to 60% of height for info fields
    const unsigned estimated_screen_height = Layout::landscape
      ? Layout::min_screen_pixels  // landscape: height is min_screen_pixels
      : Layout::min_screen_pixels * 3 / 2; // portrait: height is larger dimension, estimate
    // Use 60% of screen height for info fields, but ensure minimum 120px and max 500px
    const unsigned max_info_height = std::max(120u, std::min(500u, estimated_screen_height * 3 / 5));
    auto bottom_widget = std::make_unique<ConstrainedHeightWidget>(
      std::move(info_widget), max_info_height);

    // Use custom layout widget that ensures info fields fit and map scales to fill remaining space
    combined_widget = std::make_unique<MapInfoLayoutWidget>(std::move(top_widget),
                                                             std::move(bottom_widget));
    
    combined_widget->Initialise(parent, rc);
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    if (combined_widget)
      combined_widget->Prepare(parent, rc);
  }

  void Unprepare() noexcept override {
    if (combined_widget)
      combined_widget->Unprepare();
    combined_widget.reset();
  }

  void Show(const PixelRect &rc) noexcept override {
    if (combined_widget)
      combined_widget->Show(rc);
  }

  void Hide() noexcept override {
    if (combined_widget)
      combined_widget->Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    if (combined_widget)
      combined_widget->Move(rc);
  }

  PixelSize GetMinimumSize() const noexcept override {
    if (combined_widget)
      return combined_widget->GetMinimumSize();
    return PixelSize{0, 0};
  }

  PixelSize GetMaximumSize() const noexcept override {
    if (combined_widget)
      return combined_widget->GetMaximumSize();
    return PixelSize{0, 0};
  }
};

class DrawPanFrame final : public WndOwnerDrawFrame {
  protected:
    PixelPoint last_mouse_pos, img_pos, offset;
    bool is_dragging = false;

  std::function<void(Canvas &canvas, const PixelRect &rc, PixelPoint &offset,
    PixelPoint &img_pos)> mOnPaintCallback2;

  public:
    template<typename CB>
    void Create(ContainerWindow &parent,
                PixelRect rc, const WindowStyle style,
                  CB &&_paint) {
      mOnPaintCallback2 = std::move(_paint);
      PaintWindow::Create(parent, rc, style);
    }


  protected:
    /** from class Window */
    bool OnMouseMove(PixelPoint p, unsigned keys) noexcept override;
    bool OnMouseDown(PixelPoint p) noexcept override;
    bool OnMouseUp(PixelPoint p) noexcept override;

    bool OnKeyCheck(unsigned key_code) const noexcept override;
    bool OnKeyDown(unsigned key_code) noexcept override;

  void
  OnPaint(Canvas &canvas) noexcept override
  {
    if (mOnPaintCallback2 == nullptr)
      return;

    mOnPaintCallback2(canvas, GetClientRect(), offset, img_pos);
  }
};

/**
 * Widget wrapper for waypoint details text panel
 */
class WaypointDetailsTextWidget final : public NullWidget {
  const DialogLook &look;
  const WaypointPtr waypoint;
  PanelControl panel;
  LargeTextWindow text;

#ifdef HAVE_RUN_FILE
  ListControl file_list;
  WaypointExternalFileListHandler file_list_handler;
#endif

public:
  WaypointDetailsTextWidget(const DialogLook &_look, WaypointPtr _waypoint) noexcept
    :look(_look),
     waypoint(std::move(_waypoint))
#ifdef HAVE_RUN_FILE
     ,file_list(_look),
     file_list_handler(waypoint)
#endif
  {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    WindowStyle style;
    style.Hide();
    style.ControlParent();

    panel.Create(parent, look, rc, style);
    text.Create(panel, rc);
    text.SetFont(look.text_font);
    text.SetText(waypoint->details.c_str());

#ifdef HAVE_RUN_FILE
  const unsigned num_files = std::distance(waypoint->files_external.begin(),
                                           waypoint->files_external.end());
  if (num_files > 0) {
      TextRowRenderer &row_renderer = file_list_handler.GetRowRenderer();
      unsigned item_height = row_renderer.CalculateLayout(*look.list.font);
      unsigned list_height = item_height * std::min(num_files, 5u);
      
      PixelRect list_rc = rc;
      list_rc.bottom = list_rc.top + list_height;
      
      file_list.Create(panel, list_rc, WindowStyle(), item_height);
    file_list.SetItemRenderer(&file_list_handler);
    file_list.SetCursorHandler(&file_list_handler);
    file_list.SetLength(num_files);
      
      PixelRect text_rc = rc;
      text_rc.top = list_rc.bottom;
      text.Move(text_rc);
  }
#endif
  }

  void Unprepare() noexcept override {
#ifdef HAVE_RUN_FILE
    file_list.Destroy();
#endif
    text.Destroy();
    panel.Destroy();
  }

  void Show(const PixelRect &rc) noexcept override {
    panel.MoveAndShow(rc);
  }

  void Hide() noexcept override {
    panel.Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    panel.Move(rc);
    text.Move(rc);
#ifdef HAVE_RUN_FILE
    if (!waypoint->files_external.empty()) {
      TextRowRenderer &row_renderer = file_list_handler.GetRowRenderer();
      unsigned item_height = row_renderer.CalculateLayout(*look.list.font);
      unsigned num_files = std::distance(waypoint->files_external.begin(),
                                         waypoint->files_external.end());
      unsigned list_height = item_height * std::min(num_files, 5u);
      
      PixelRect list_rc = rc;
      list_rc.bottom = list_rc.top + list_height;
      file_list.Move(list_rc);
      
      PixelRect text_rc = rc;
      text_rc.top = list_rc.bottom;
      text.Move(text_rc);
    }
#endif
  }
};

/**
 * Widget wrapper for a single waypoint image
 */
class WaypointImageWidget final : public NullWidget {
  const DialogLook &look;
  Bitmap image;
  DrawPanFrame image_window;
  int zoom = 0;
  PixelPoint img_pos{0, 0};

public:
  explicit WaypointImageWidget(const DialogLook &_look, Bitmap &&_image) noexcept
    :look(_look), image(std::move(_image)) {}

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    WindowStyle style;
    style.Hide();
    
    image_window.Create(parent, rc, style,
                        [this](Canvas &canvas, const PixelRect &rc, PixelPoint offset,
                               PixelPoint &img_pos) {
                          OnImagePaint(canvas, rc, offset, img_pos);
                        });
  }

  void Unprepare() noexcept override {
    image_window.Destroy();
  }

  void Show(const PixelRect &rc) noexcept override {
    image_window.MoveAndShow(rc);
  }

  void Hide() noexcept override {
    image_window.Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    image_window.Move(rc);
  }

  void SetZoom(int _zoom) noexcept {
    zoom = _zoom;
        image_window.Invalidate();
  }

  int GetZoom() const noexcept {
    return zoom;
  }

  void ResetZoom() noexcept {
    zoom = 0;
    img_pos = {0, 0};
    image_window.Invalidate();
  }

private:
  void OnImagePaint(Canvas &canvas, [[maybe_unused]] const PixelRect &rc,
                    PixelPoint &offset, PixelPoint &img_pos_ref)
{
  canvas.ClearWhite();

  static constexpr int zoom_factors[] = {1, 2, 4, 8, 16, 32};
    PixelSize img_size = image.GetSize();
  double scale = std::min(static_cast<double>(canvas.GetWidth()) / img_size.width,
                          static_cast<double>(canvas.GetHeight()) / img_size.height) *
                 zoom_factors[zoom];

  PixelPoint screen_pos;
  PixelSize screen_size;

  // Calculate horizontal scaling and positioning
  double scaled_width = img_size.width * scale;
  if (scaled_width <= canvas.GetWidth()) {
      img_pos_ref.x = zoom == 0 ? 0 : img_pos.x + offset.x / scale;
    screen_pos.x = (canvas.GetWidth() - static_cast<int>(scaled_width)) / 2;
    screen_size.width = static_cast<unsigned>(scaled_width);
  } else {
    double visible_width = canvas.GetWidth() / scale;
      img_pos_ref.x = zoom == 0 ? (img_size.width - visible_width) / 2 : img_pos.x + offset.x / scale;
      img_pos_ref.x = std::clamp(img_pos_ref.x, 0, static_cast<int>(img_size.width - visible_width));
    img_size.width = static_cast<unsigned>(visible_width);
    screen_pos.x = 0;
    screen_size.width = canvas.GetWidth();
  }

  // Calculate vertical scaling and positioning
  double scaled_height = img_size.height * scale;
  if (scaled_height <= canvas.GetHeight()) {
      img_pos_ref.y = 0;
    screen_pos.y = (canvas.GetHeight() - static_cast<int>(scaled_height)) / 2;
    screen_size.height = static_cast<unsigned>(scaled_height);
  } else {
    double visible_height = canvas.GetHeight() / scale;
      img_pos_ref.y = zoom == 0 ? (img_size.height - visible_height) / 2 : img_pos.y + offset.y / scale;
      img_pos_ref.y = std::clamp(img_pos_ref.y, 0, static_cast<int>(img_size.height - visible_height));
    img_size.height = static_cast<unsigned>(visible_height);
    screen_pos.y = 0;
    screen_size.height = canvas.GetHeight();
  }
    
    img_pos = img_pos_ref;
    canvas.Stretch(screen_pos, screen_size, image, img_pos_ref, img_size);
  }
};

/**
 * Widget for image zoom controls (magnify/shrink buttons)
 */
class WaypointImageZoomWidget final : public NullWidget {
  WidgetDialog &dialog;
  const DialogLook &look;
  WaypointImageWidget *current_image_widget = nullptr;
  Button magnify_button, shrink_button;

public:
  explicit WaypointImageZoomWidget(WidgetDialog &_dialog) noexcept
    :dialog(_dialog), look(dialog.GetLook()) {}

  void SetCurrentImage(WaypointImageWidget *widget) noexcept {
    current_image_widget = widget;
    UpdateButtons();
  }

  void UpdateButtons() noexcept {
    if (current_image_widget == nullptr) {
      magnify_button.SetEnabled(false);
      shrink_button.SetEnabled(false);
      return;
    }

    int zoom = current_image_widget->GetZoom();
    magnify_button.SetEnabled(zoom < 5);
    shrink_button.SetEnabled(zoom > 0);
  }

  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    WindowStyle button_style;
    button_style.Hide();
    button_style.TabStop();

    const unsigned button_height = ::Layout::GetMaximumControlHeight();
    PixelRect magnify_rc = rc;
    magnify_rc.bottom = magnify_rc.top + button_height;
    magnify_rc.right = magnify_rc.left + button_height;

    PixelRect shrink_rc = rc;
    shrink_rc.bottom = shrink_rc.top + button_height;
    shrink_rc.left = shrink_rc.right - button_height;

    magnify_button.Create(parent, magnify_rc, button_style,
                          std::make_unique<SymbolButtonRenderer>(look.button, _T("+")),
                          [this](){ OnMagnifyClicked(); });
    shrink_button.Create(parent, shrink_rc, button_style,
                         std::make_unique<SymbolButtonRenderer>(look.button, _T("-")),
                         [this](){ OnShrinkClicked(); });

    UpdateButtons();
  }

  void Unprepare() noexcept override {
    magnify_button.Destroy();
    shrink_button.Destroy();
  }

  void Show(const PixelRect &rc) noexcept override {
    if (current_image_widget != nullptr) {
      const unsigned button_height = ::Layout::GetMaximumControlHeight();
      PixelRect magnify_rc = rc;
      magnify_rc.bottom = magnify_rc.top + button_height;
      magnify_rc.right = magnify_rc.left + button_height;

      PixelRect shrink_rc = rc;
      shrink_rc.bottom = shrink_rc.top + button_height;
      shrink_rc.left = shrink_rc.right - button_height;

      magnify_button.MoveAndShow(magnify_rc);
      shrink_button.MoveAndShow(shrink_rc);
    }
  }

  void Hide() noexcept override {
    magnify_button.Hide();
    shrink_button.Hide();
  }

  void Move(const PixelRect &rc) noexcept override {
    const unsigned button_height = ::Layout::GetMaximumControlHeight();
    PixelRect magnify_rc = rc;
    magnify_rc.bottom = magnify_rc.top + button_height;
    magnify_rc.right = magnify_rc.left + button_height;

    PixelRect shrink_rc = rc;
    shrink_rc.bottom = shrink_rc.top + button_height;
    shrink_rc.left = shrink_rc.right - button_height;

    magnify_button.Move(magnify_rc);
    shrink_button.Move(shrink_rc);
  }

private:
  void OnMagnifyClicked() noexcept {
    if (current_image_widget == nullptr)
      return;

    int zoom = current_image_widget->GetZoom();
    if (zoom >= 5)
      return;
    current_image_widget->SetZoom(zoom + 1);
    UpdateButtons();
  }

  void OnShrinkClicked() noexcept {
    if (current_image_widget == nullptr)
      return;

    int zoom = current_image_widget->GetZoom();
    if (zoom <= 0)
      return;
    current_image_widget->SetZoom(zoom - 1);
    UpdateButtons();
  }
};

class WaypointDetailsWidget final
  : public TabWidget {
  WidgetDialog &dialog;
  const DialogLook &look{dialog.GetLook()};

  const WaypointPtr waypoint;
  ProtectedTaskManager *const task_manager;
  Waypoints *const waypoints;
  const bool allow_edit;

  std::vector<WaypointImageWidget*> image_widgets;
  WaypointImageZoomWidget *zoom_widget = nullptr;
  unsigned first_image_tab_index = 0;

  // Store image data until Initialise()
  std::vector<Bitmap> images;

public:
  const WaypointPtr &GetWaypoint() const noexcept {
    return waypoint;
  }

  ProtectedTaskManager *GetTaskManager() const noexcept {
    return task_manager;
  }

  WaypointDetailsWidget(WidgetDialog &_dialog,
                        Waypoints *_waypoints, WaypointPtr _waypoint,
                        ProtectedTaskManager *_task_manager, bool _allow_edit) noexcept
    :TabWidget(TabWidget::Orientation::HORIZONTAL),
     dialog(_dialog),
     waypoint(std::move(_waypoint)),
     task_manager(_task_manager),
     waypoints(_waypoints),
     allow_edit(_allow_edit)
  {
    // Load images (store them for later)
    images.reserve(5);
    for (const auto &i : waypoint->files_embed) {
      if (images.size() >= 5)
        break;

      try {
        Bitmap bitmap;
        if (bitmap.LoadFile(LocalPath(i.c_str()))) {
          images.push_back(std::move(bitmap));
        }
      } catch (const std::exception &e) {
        LogFormat("Failed to load %s: %s",
                  (const char *)NarrowPathName(Path(i.c_str())),
                  e.what());
      }
    }

    // Create zoom widget if we have images
    if (!images.empty()) {
      auto zoom_widget_ptr = std::make_unique<WaypointImageZoomWidget>(dialog);
      zoom_widget = zoom_widget_ptr.get();
      SetExtra(std::move(zoom_widget_ptr));
    }
  }

  void Initialise(ContainerWindow &parent, const PixelRect &rc) noexcept override {
    TabWidget::Initialise(parent, rc);

    // Now that tab_display is created, we can add tabs
    // Copy waypoint for each widget since WaypointPtr is a shared_ptr
    
    // Create Info tab with map extract beside comment and other fields below
    AddTab(std::make_unique<WaypointInfoWithMapWidget>(look, WaypointPtr(waypoint)),
           _("Info"));

    // Only add details tab if there are details or files
    bool has_details = !waypoint->details.empty();
#ifdef HAVE_RUN_FILE
    bool has_files = !waypoint->files_external.empty();
    if (has_details || has_files)
#else
    if (has_details)
#endif
    {
      AddTab(std::make_unique<WaypointDetailsTextWidget>(look, WaypointPtr(waypoint)),
             _("Details"));
    }

    AddTab(std::make_unique<WaypointCommandsWidget>(look, &dialog, waypoints, WaypointPtr(waypoint),
                                                    task_manager, allow_edit),
           _("Commands"));

    // Track where image tabs start
    first_image_tab_index = GetSize();

    // Add image tabs
    for (auto &img : images) {
      auto image_widget = std::make_unique<WaypointImageWidget>(look, std::move(img));
      image_widgets.push_back(image_widget.get());
      AddTab(std::move(image_widget), _("Image"));
    }

    // Clear images vector as they're now moved into widgets
    images.clear();
  }

protected:
  void OnPageFlipped() noexcept override {
    TabWidget::OnPageFlipped();

    // Update zoom widget when switching to/from image tabs
    if (zoom_widget != nullptr) {
      unsigned current = GetCurrentIndex();

      if (current >= first_image_tab_index && 
          current < first_image_tab_index + image_widgets.size()) {
        unsigned image_index = current - first_image_tab_index;
        zoom_widget->SetCurrentImage(image_widgets[image_index]);
        zoom_widget->Show(GetEffectiveExtraPosition());
      } else {
        zoom_widget->SetCurrentImage(nullptr);
        zoom_widget->Hide();
      }
    }
  }

  bool KeyPress(unsigned key_code) noexcept override {
    switch (key_code) {
    case KEY_F1:
      if (zoom_widget != nullptr) {
        unsigned current = GetCurrentIndex();
        if (current >= first_image_tab_index && 
            current < first_image_tab_index + image_widgets.size()) {
          // Trigger magnify via zoom widget
          return true;
        }
      }
      break;

    case KEY_F2:
      if (zoom_widget != nullptr) {
        unsigned current = GetCurrentIndex();
        if (current >= first_image_tab_index && 
            current < first_image_tab_index + image_widgets.size()) {
          // Trigger shrink via zoom widget
          return true;
        }
      }
      break;

    case KEY_ESCAPE:
      if (zoom_widget != nullptr) {
        unsigned current = GetCurrentIndex();
        if (current >= first_image_tab_index && 
            current < first_image_tab_index + image_widgets.size()) {
          unsigned image_index = current - first_image_tab_index;
          image_widgets[image_index]->ResetZoom();
          zoom_widget->UpdateButtons();
          return true;
        }
      }
      break;
    }

    return TabWidget::KeyPress(key_code);
  }
};


bool
DrawPanFrame::OnMouseMove(PixelPoint p, [[maybe_unused]] unsigned keys) noexcept
{
  if (!is_dragging)
    return false;

  offset = last_mouse_pos - p;
  last_mouse_pos = p;
  Invalidate();
  return true;
}

bool
DrawPanFrame::OnMouseDown(PixelPoint p) noexcept
{
  is_dragging = true;
  last_mouse_pos = p;
  return true;
}

bool
DrawPanFrame::OnMouseUp([[maybe_unused]] PixelPoint p) noexcept
{
  is_dragging = false;
  return true;
}

bool
DrawPanFrame::OnKeyCheck(unsigned key_code) const noexcept
{
  switch (key_code) {
    case KEY_LEFT:
    case KEY_RIGHT:
    case KEY_UP:
    case KEY_DOWN:
    return true;

  default:
    return false;
  }
}

bool
DrawPanFrame::OnKeyDown(unsigned key_code) noexcept
{
  switch (key_code) {
    case KEY_LEFT: {
      offset = {-50, 0};
      break;
    }
    case KEY_RIGHT: {
      offset = {50, 0};
      break;
    }
    case KEY_UP: {
      offset = {0, -50};
      break;
    }
    case KEY_DOWN: {
      offset = {0, 50};
      break;
    }
    default:
      return false;
  }
  Invalidate();
  return true;
}

static void
SetTitle(WndForm &form, const TabWidget &pager, const Waypoint &waypoint)
{
  StaticString<256> buffer;
  buffer.Format(_T("%s: %s"), _("Waypoint"), waypoint.name.c_str());

  std::string_view key{};
  const TCHAR *name = nullptr;

  switch (waypoint.origin) {
  case WaypointOrigin::NONE:
    break;

  case WaypointOrigin::USER:
    name = _T("user.cup");
    break;

  case WaypointOrigin::PRIMARY:
    key = ProfileKeys::WaypointFileList;
    break;

  case WaypointOrigin::WATCHED:
    key = ProfileKeys::WatchedWaypointFileList;
    break;

  case WaypointOrigin::MAP:
    key = ProfileKeys::MapFile;
    break;
  }

  if (!key.empty()) {
    const auto filename = Profile::map.GetPathBase(key);
    if (filename != nullptr)
      buffer.AppendFormat(_T(" (%s)"), filename.c_str());
  } else if (name != nullptr)
    buffer.AppendFormat(_T(" (%s)"), name);

  buffer.AppendFormat(_T(" - %s"), pager.GetButtonCaption(pager.GetCurrentIndex()));
  form.SetCaption(buffer);
}

void
dlgWaypointDetailsShowModal(Waypoints *waypoints, WaypointPtr _waypoint,
                            bool allow_navigation, bool allow_edit)
{
  LastUsedWaypoints::Add(*_waypoint);

  const DialogLook &look = UIGlobals::GetDialogLook();
  TWidgetDialog<WaypointDetailsWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           look, nullptr);
  
  dialog.SetWidget(dialog, waypoints, _waypoint,
                   allow_navigation ? backend_components->protected_task_manager.get() : nullptr,
                   allow_edit);

  auto &widget = dialog.GetWidget();
  widget.SetPageFlippedCallback([&dialog, &widget]() {
      SetTitle(dialog, widget, *widget.GetWaypoint());
    });

  dialog.PrepareWidget();

  // Add goto/pan button
  if (allow_navigation && backend_components->protected_task_manager != nullptr) {
    dialog.AddButton(_("GoTo"), [&widget, &dialog](){
      ProtectedTaskManager *task_manager = widget.GetTaskManager();
      if (task_manager != nullptr) {
        task_manager->DoGoto(widget.GetWaypoint());
        dialog.SetModalResult(mrOK);
        CommonInterface::main_window->FullRedraw();
      }
    });
  } else {
    dialog.AddButton(_("Pan To"), [&widget, &dialog](){
      if (ActivatePan(*widget.GetWaypoint()))
        dialog.SetModalResult(mrOK);
    });
  }

  dialog.AddButton(_("Close"), dialog.MakeModalResultCallback(mrCancel));

  SetTitle(dialog, widget, *widget.GetWaypoint());

  dialog.ShowModal();
}
