// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "SkysightControlsModel.hpp"

#include "DataGlobals.hpp"
#include "PageActions.hpp"
#include "Language/Language.hpp"
#include "Weather/Skysight/FieldControls.hpp"
#include "Weather/Skysight/Skysight.hpp"
#include <chrono>

namespace WeatherMapOverlay {

SkysightControlsModel::SkysightControlsModel(
  std::shared_ptr<Skysight> _skysight) noexcept
  :skysight(std::move(_skysight)) {}

void
SkysightControlsModel::OnShow() noexcept
{
  SkySight::ApplyCursorFromPageLayout(PageActions::GetCurrentLayout());
  countdown_timer.Schedule(std::chrono::seconds{1});
}

void
SkysightControlsModel::OnHide() noexcept
{
  countdown_timer.Cancel();
  dynamic_status_visible = false;
}

const SkySight::Layer *
SkysightControlsModel::GetLayer() const noexcept
{
  const auto &page = PageActions::GetCurrentLayout();
  return skysight != nullptr && page.UsesSkysightOverlay()
    ? skysight->GetSelectedLayer(page.skysight_overlay.c_str())
    : nullptr;
}

void
SkysightControlsModel::FormatPrimaryLabel(StaticString<64> &text) const noexcept
{
  if (skysight != nullptr && skysight->IsThrottled()) {
    text.Format(_("Download limit: retry in %u s"),
                unsigned(skysight->GetThrottleRemainingSeconds()));
    return;
  }

  if (skysight != nullptr) {
    const auto retry = skysight->GetDatafilesRetryRemainingSeconds();
    if (retry > 0) {
      text.Format(_("Retry download in %u s"), unsigned(retry));
      return;
    }
  }

  SkySight::FormatTimeLabelForPage(text, PageActions::GetCurrentLayout());
  if (const auto *layer = GetLayer();
      layer != nullptr && layer->SupportsLiveTiles() &&
      skysight->IsLiveViewUpdating(layer->id))
    text = _("Live (updating...)");
}

void
SkysightControlsModel::FormatSecondaryLabel(StaticString<64> &text) const noexcept
{
  SkySight::FormatLayerLabelForPage(text, PageActions::GetCurrentLayout());
}

bool
SkysightControlsModel::HasPrimaryData() const noexcept
{
  return SkySight::HasSelectedTimeData();
}

bool
SkysightControlsModel::IsPrimaryEnabled() const noexcept
{
  return SkySight::IsTimeSelectable();
}

bool
SkysightControlsModel::HasSecondaryData() const noexcept
{
  return SkySight::HasSelectedLayer();
}

bool
SkysightControlsModel::StepPrimary(int delta) noexcept
{
  return SkySight::StepTime(delta);
}

bool
SkysightControlsModel::StepSecondary(int delta) noexcept
{
  return SkySight::StepLayer(delta);
}

void
SkysightControlsModel::UpdateCountdownLabel() noexcept
{
  const auto *layer = GetLayer();
  const bool waiting = skysight != nullptr &&
    (skysight->IsThrottled() ||
     skysight->GetDatafilesRetryRemainingSeconds() > 0 ||
     (layer != nullptr && skysight->IsLiveViewUpdating(layer->id)));

  if (waiting || dynamic_status_visible)
    Notify(ControlsUpdate::LABELS);

  dynamic_status_visible = waiting;
}

bool
SkysightControlsModel::GetPrimaryAutoAdvance() const noexcept
{
  return SkySight::GetTimeAutoAdvance();
}

void
SkysightControlsModel::SetPrimaryAutoAdvance(bool auto_advance) noexcept
{
  SkySight::SetTimeAutoAdvance(auto_advance);
}

void
SkysightControlsModel::ApplyPrimaryAutoAdvance() noexcept
{
  SkySight::ApplyAutoAdvanceTime();
}

void
SkysightControlsModel::EnablePrimaryAutoFromInput() noexcept
{
  SkySight::EnableTimeAutoFromInput();
  NotifyOverlay();
}

PrimaryLabelAction
SkysightControlsModel::GetPrimaryLabelAction() const noexcept
{
  if (!SkySight::HasSelectedLayer())
    return PrimaryLabelAction::OPEN_SETUP;
  return SkySight::IsTimeSelectable()
    ? PrimaryLabelAction::OPEN_PICKER
    : PrimaryLabelAction::NONE;
}

SecondaryLabelAction
SkysightControlsModel::GetSecondaryLabelAction() const noexcept
{
  return SecondaryLabelAction::OPEN_PICKER;
}

void
SkysightControlsModel::OpenPrimaryPicker() noexcept
{
  SkySight::OpenTimePicker();
  NotifyOverlay();
}

SecondaryPickerResult
SkysightControlsModel::OpenSecondaryPicker() noexcept
{
  return HandleSecondaryFieldPicker(SkySight::OpenLayerPicker(true), [this] {
    Notify(ControlsUpdate::OVERLAY);
  });
}

void
SkysightControlsModel::ResumePrimaryAuto() noexcept
{
  if (GetPrimaryAutoAdvance())
    return;

  SetPrimaryAutoAdvance(true);
  Notify(ControlsUpdate::OVERLAY);
}

void
SkysightControlsModel::RefreshOverlay() noexcept
{
}

} // namespace WeatherMapOverlay
