// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

// src/Widget/LargeTextWidget.hpp

#pragma once

#include "WindowWidget.hpp"
#include "ui/control/ScrollBar.hpp"
#include "Look/ButtonLook.hpp"  // Make sure to include ButtonLook.hpp

#include <tchar.h>

struct DialogLook;

/**
 * A #Widget implementation that displays multi-line text.
 */
class LargeTextWidget : public WindowWidget {
  const DialogLook &look;
  const TCHAR *text;
  ButtonLook buttonLook = buttons.GetButtonLook();      // Declare ButtonLook for ScrollBar initialization
  ScrollBar scrollbar(const ButtonLook);

public:
  explicit LargeTextWidget(const DialogLook &_look, const TCHAR *_text = nullptr) noexcept
    : look(_look), text(_text) {

    // Initialize buttonLook by passing the required font from DialogLook
    buttonLook.Initialise(_look.font); // Assuming DialogLook has a font member

    // Initialize the scrollbar with the buttonLook (or another appropriate initialization)
    scrollbar = ScrollBar(buttonLook); // Use buttonLook for ScrollBar
  }

  void SetText(const TCHAR *text) noexcept;

  /* virtual methods from class Widget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  bool SetFocus() noexcept override;

  /* virtual methods from class Scrollbar */
  void UpdateScrollbar() noexcept;
  void HandleDragMove(PaintWindow *w, unsigned y) noexcept;
  void HandleDragBegin(PaintWindow *w, unsigned y) noexcept;
  void HandleDragEnd(PaintWindow *w) noexcept;
  void Paint(Canvas &canvas) noexcept;
};

