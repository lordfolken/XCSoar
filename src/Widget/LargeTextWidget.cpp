// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "LargeTextWidget.hpp"
#include "ui/control/LargeTextWindow.hpp"
#include "ui/control/ScrollBar.hpp"
#include "Look/DialogLook.hpp"

void
LargeTextWidget::SetText(const TCHAR *text) noexcept
{
  LargeTextWindow &w = (LargeTextWindow &)GetWindow();
  w.SetText(text);
  
  // Update the scrollbar after setting the text
  UpdateScrollbar();
}

void
LargeTextWidget::Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept
{
  LargeTextWindowStyle style;
  style.Hide();
  style.TabStop();

  auto w = std::make_unique<LargeTextWindow>();
  w->Create(parent, rc, style);
  w->SetFont(look.text_font);
  
  if (text != nullptr) {
    w->SetText(text);
  }

  SetWindow(std::move(w));

  // Initialize the ScrollBar after the window is created
  scrollbar.SetSize(rc.GetSize());  // Set the scrollbar size to match the widget's size
  UpdateScrollbar();  // Update the scrollbar based on content size and visible size
}

bool
LargeTextWidget::SetFocus() noexcept
{
  GetWindow().SetFocus();
  return true;
}

void
LargeTextWidget::UpdateScrollbar() noexcept
{
  // Get the total number of rows (content size) and the number of visible rows
  LargeTextWindow &w = (LargeTextWindow &)GetWindow();
  unsigned content_size = w.GetRowCount();  // Total number of rows in the content
  unsigned visible_size = w.GetVisibleRows();  // Number of visible rows at a time

  // Update the scrollbar slider based on the content size and visible size
  scrollbar.SetSlider(content_size, visible_size, scroll_origin);
}

void
LargeTextWidget::HandleDragMove(PaintWindow *w, unsigned y) noexcept
{
  // Handle the drag movement and update the scroll position
  LargeTextWindow &w = (LargeTextWindow &)GetWindow();
  unsigned new_origin = scrollbar.DragMove(content_size, visible_size, y);
  w.ScrollTo(new_origin);  // Set the first visible line in the LargeTextWindow

  // Redraw the widget to reflect the new scroll position
  w->Invalidate();
}

void
LargeTextWidget::HandleDragBegin(PaintWindow *w, unsigned y) noexcept
{
  // Begin dragging: tell the scrollbar to start capturing the drag event
  scrollbar.DragBegin(w, y);
}

void
LargeTextWidget::HandleDragEnd(PaintWindow *w) noexcept
{
  // End dragging: stop the drag event
  scrollbar.DragEnd(w);
}

void
LargeTextWidget::Paint(Canvas &canvas) noexcept
{
  // Paint the content (text) of the LargeTextWindow
  LargeTextWindow &w = (LargeTextWindow &)GetWindow();
  w.OnPaint(canvas);  // Paint the text inside the window

  // Paint the scrollbar
  scrollbar.Paint(canvas);  // Paint the scrollbar over the content
}

