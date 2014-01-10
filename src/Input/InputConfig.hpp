/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2015 The XCSoar Project
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

#ifndef XCSOAR_INPUT_CONFIG_HPP
#define XCSOAR_INPUT_CONFIG_HPP

#include "InputQueue.hpp"
#include "Menu/MenuData.hpp"
#include "Util/RadixTree.hpp"
#include "Util/StaticString.hpp"
#include "Util/TrivialArray.hpp"

#include <assert.h>
#include <tchar.h>

#ifdef ENABLE_SDL
#include <SDL_version.h>
#if SDL_MAJOR_VERSION >= 2
#include <SDL_keycode.h>
#endif
#endif

struct InputConfig {
  // Sensible maximums

  static constexpr unsigned MAX_MODE = 32;
  static constexpr unsigned MAX_MODE_STRING = 24;
#ifdef ENABLE_SDL
  static constexpr unsigned MAX_KEY = 400;
#elif defined(USE_CONSOLE) || defined(NON_INTERACTIVE)
  static constexpr unsigned MAX_KEY = 0600;
#else
  static constexpr unsigned MAX_KEY = 255;
#endif
  static constexpr unsigned MAX_EVENTS = 2048;

  typedef void (*pt2Event)(const TCHAR *);

  // Events - What do you want to DO
  struct Event {
    // Which function to call (can be any, but should be here)
    pt2Event event;
    // Parameters
    const TCHAR *misc;
    // Next in event list - eg: Macros
    unsigned next;
  };

  /** Map mode to location */
  TrivialArray<StaticString<MAX_MODE_STRING>, MAX_MODE> modes;

  // Key map to Event - Keys (per mode) mapped to events
  unsigned short Key2Event[MAX_MODE][MAX_KEY];		// Points to Events location
#if defined(ENABLE_SDL) && (SDL_MAJOR_VERSION >= 2)
  /* In SDL2, keycodes without character representations are large values,
  AND-ed with SDLK_SCANCODE_MASK (0x40000000). A seperate array is therefore
  used here and the keycode is stored here with an index without
  SDLK_SCANCODE_MASK. */
  unsigned short Key2EventNonChar[MAX_MODE][MAX_KEY];
#endif

  RadixTree<unsigned> Gesture2Event;

  // Glide Computer Events
  unsigned short GC2Event[GCE_COUNT];

  // NMEA Triggered Events
  unsigned short N2Event[NE_COUNT];

  TrivialArray<Event, MAX_EVENTS> events;

  Menu menus[MAX_MODE];

  void SetDefaults();

  gcc_pure
  int LookupMode(const TCHAR *name) const {
    for (unsigned i = 0, size = modes.size(); i < size; ++i)
      if (modes[i] == name)
        return i;

    return -1;
  }

  int AppendMode(const TCHAR *name) {
    if (modes.full())
      return -1;

    modes.append() = name;
    return modes.size() - 1;
  }

  gcc_pure
  int MakeMode(const TCHAR *name) {
    int mode = LookupMode(name);
    if (mode < 0)
      mode = AppendMode(name);

    return mode;
  }

  unsigned AppendEvent(pt2Event handler, const TCHAR *misc,
                       unsigned next) {
    if (events.full())
      return 0;

    Event &event = events.append();
    event.event = handler;
    event.misc = misc;
    event.next = next;

    return events.size() - 1;
  }

  void AppendMenu(unsigned mode_id, const TCHAR* label,
                  unsigned location, unsigned event_id) {
    assert(mode_id < MAX_MODE);

    menus[mode_id].Add(label, location, event_id);
  }

  gcc_pure
  unsigned GetKeyEvent(unsigned mode, unsigned key_code) const {
    assert(mode < MAX_MODE);

    unsigned key_code_idx = key_code;
    auto key_2_event = Key2Event;
#if defined(ENABLE_SDL) && (SDL_MAJOR_VERSION >= 2)
    if (key_code & SDLK_SCANCODE_MASK) {
      key_code_idx = key_code & ~SDLK_SCANCODE_MASK;
      key_2_event = Key2EventNonChar;
    }
#endif

    if (key_code_idx >= MAX_KEY)
      return 0;

    if (mode > 0 && key_2_event[mode][key_code_idx] != 0)
      return key_2_event[mode][key_code_idx];

    /* fall back to the default mode */
    return key_2_event[0][key_code_idx];
  }

  gcc_pure
  const MenuItem &GetMenuItem(unsigned mode, unsigned location) const {
    assert(mode < MAX_MODE);
    assert(location < Menu::MAX_ITEMS);

    return menus[mode][location];
  }
};

#endif
