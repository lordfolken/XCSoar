// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "../LargeTextWindow.hpp"
#include "ui/canvas/Canvas.hpp"
#include "Screen/Layout.hpp"
#include "ui/event/KeyCode.hpp"
#include "util/StringAPI.hxx"

#include <algorithm>

void
LargeTextWindow::Create(ContainerWindow &parent, PixelRect rc,
                        const LargeTextWindowStyle style)
{
  origin = 0;

  NativeWindow::Create(&parent, rc, style);
}

unsigned
LargeTextWindow::GetVisibleRows() const
{
  return GetSize().height / GetFont().GetLineSpacing();
}

unsigned
LargeTextWindow::GetRowCount() const
{
  const TCHAR *str = value.c_str();
  unsigned row_count = 1;
  while ((str = StringFind(str, _T('\n'))) != nullptr) {
    str++;
    row_count++;
  }

  return row_count;
}

void
LargeTextWindow::ScrollVertically(int delta_lines)
{
  const unsigned visible_rows = GetVisibleRows();
  const unsigned row_count = GetRowCount();

  if (visible_rows >= row_count)
    /* all rows are visible at a time, no scrolling needed/possible */
    return;

  unsigned new_origin = origin + delta_lines;
  if ((int)new_origin < 0)
    new_origin = 0;
  else if (new_origin > row_count - visible_rows)
    new_origin = row_count - visible_rows;

  ScrollTo(new_origin);
}

void
LargeTextWindow::ScrollTo(unsigned new_origin) noexcept
{
  if (new_origin != origin) {
    origin = new_origin;
    Invalidate();
  }
}

void
LargeTextWindow::OnResize(PixelSize new_size) noexcept
{
  NativeWindow::OnResize(new_size);

  if (!value.empty()) {
    /* revalidate the scroll position */
    const unsigned visible_rows = GetVisibleRows();
    const unsigned row_count = GetRowCount();
    if (visible_rows >= row_count)
      origin = 0;
    else if (origin > row_count - visible_rows)
      origin = row_count - visible_rows;
    Invalidate();
  }
}

void
LargeTextWindow::OnSetFocus() noexcept
{
  NativeWindow::OnSetFocus();
  Invalidate();
}

void
LargeTextWindow::OnKillFocus() noexcept
{
  NativeWindow::OnKillFocus();
  Invalidate();
}

void
LargeTextWindow::OnPaint(Canvas &canvas) noexcept
{
  canvas.ClearWhite();

  auto rc = canvas.GetRect();
  
  // Draw 3D inset border for read-only text field
  // Scale pen width and border offset for DPI
  const unsigned pen_width = std::max(1u, Layout::ScalePenWidth(1u));
  const unsigned border_offset = std::max(1u, Layout::Scale(1u));
  
  // Create inset (sunken) effect: dark on top/left, bright on bottom/right
  Pen dark(pen_width, Color(128, 128, 128));
  Pen bright(pen_width, Color(240, 240, 240));
  
  // Draw dark shadow lines (top and left)
  canvas.Select(dark);
  canvas.DrawTwoLinesExact({rc.left, rc.bottom - (int)border_offset},
                           {rc.left, rc.top},
                           {rc.right - (int)border_offset, rc.top});
  
  // Draw bright highlight lines (bottom and right)
  canvas.Select(bright);
  canvas.DrawTwoLinesExact({rc.left + (int)border_offset, rc.bottom - (int)border_offset},
                           {rc.right - (int)border_offset, rc.bottom - (int)border_offset},
                           {rc.right - (int)border_offset, rc.top + (int)border_offset});

  if (HasFocus())
    canvas.DrawFocusRectangle(rc.WithPadding(1));

  if (value.empty())
    return;

  const int padding = Layout::GetTextPadding() * 2;
  rc.Grow(-padding);

  canvas.SetBackgroundTransparent();
  canvas.SetTextColor(COLOR_BLACK);

  rc.top -= origin * GetFont().GetHeight();

  canvas.Select(GetFont());
  renderer.Draw(canvas, rc, value.c_str());
}

bool
LargeTextWindow::OnKeyCheck(unsigned key_code) const noexcept
{
  switch (key_code) {
  case KEY_UP:
    return origin > 0;

  case KEY_DOWN:
    return GetRowCount() > GetVisibleRows() &&
      origin < GetRowCount() - GetVisibleRows();
  }

  return false;
}

bool
LargeTextWindow::OnKeyDown(unsigned key_code) noexcept
{
  switch (key_code) {
  case KEY_UP:
    ScrollVertically(-1);
    return true;

  case KEY_DOWN:
    ScrollVertically(1);
    return true;

  case KEY_HOME:
    ScrollTo(0);
    return true;

  case KEY_END:
    if (unsigned visible_rows = GetVisibleRows(), row_count = GetRowCount();
        visible_rows < row_count)
      ScrollTo(row_count - visible_rows);
    return true;

  case KEY_PRIOR:
    ScrollVertically(-(int)GetVisibleRows());
    return true;

  case KEY_NEXT:
    ScrollVertically(GetVisibleRows());
    return true;
  }

  return NativeWindow::OnKeyDown(key_code);
}

bool
LargeTextWindow::OnMouseDown([[maybe_unused]] PixelPoint p) noexcept
{
  if (IsTabStop())
    SetFocus();

  return true;
}

void
LargeTextWindow::SetText(const TCHAR *text)
{
  if (text != nullptr)
    value = text;
  else
    value.clear();
  Invalidate();
}
