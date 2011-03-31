/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
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

#include "Pages.hpp"

#include "Interface.hpp"
#include "MainWindow.hpp"
#include "InfoBoxes/InfoBoxManager.hpp"
#include "Profile/Profile.hpp"
#include "Profile/InfoBoxConfig.hpp"
#include "Language.hpp"

#include <stdio.h>

namespace Pages
{
  unsigned Current = 0;
  PageLayout pages[MAX_PAGES];
  const unsigned MAX_VALID_LAYOUTS =
    1 + // Nothing
    1 + // Map & Auto InfoBoxes
    1 + // Map (Full Screen)
    InfoBoxManagerConfig::MAX_INFOBOX_PANELS;
  unsigned validLayoutsCnt = 0;
  PageLayout validLayouts[MAX_VALID_LAYOUTS];

  void addValidLayout(const PageLayout& pl);
}


void
Pages::Update()
{
  while (pages[Current].topLayout == PageLayout::tlEmpty)
    Current = (Current + 1) % MAX_PAGES;

  OpenLayout(pages[Current]);
}


void
Pages::Next()
{
  do {
    Current = (Current + 1) % MAX_PAGES;
  } while (pages[Current].topLayout == PageLayout::tlEmpty);

  Update();
}


void
Pages::Prev()
{
  do {
    Current = (Current == 0) ? MAX_PAGES - 1 : Current - 1;
  } while (pages[Current].topLayout == PageLayout::tlEmpty);

  Update();
}

const Pages::PageLayout& 
Pages::get_current()
{
  assert(Current < MAX_PAGES);
  return pages[Current];
}

void
Pages::Open(unsigned page)
{
  if (page >= MAX_PAGES)
    return;

  if (pages[page].topLayout == PageLayout::tlEmpty)
    return;

  Current = page;
  Update();
}


void
Pages::OpenLayout(PageLayout &layout)
{
  switch (layout.topLayout) {
    case PageLayout::tlMap:
      XCSoarInterface::main_window.SetFullScreen(true);
      XCSoarInterface::SetSettingsMap().EnableAuxiliaryInfo = false;
      break;
    case PageLayout::tlMapAndInfoBoxes:
      if (!layout.infoBoxConfig.autoSwitch &&
          layout.infoBoxConfig.panel < InfoBoxManagerConfig::MAX_INFOBOX_PANELS) {
        XCSoarInterface::main_window.SetFullScreen(false);
        XCSoarInterface::SetSettingsMap().EnableAuxiliaryInfo = true;
        XCSoarInterface::SetSettingsMap().AuxiliaryInfoBoxPanel = layout.infoBoxConfig.panel;
      }
      else {
        XCSoarInterface::main_window.SetFullScreen(false);
        XCSoarInterface::SetSettingsMap().EnableAuxiliaryInfo = false;
        XCSoarInterface::SetSettingsMap().AuxiliaryInfoBoxPanel = 0;
      }
      break;
    case PageLayout::tlEmpty:
      return;
  }

  InfoBoxManager::SetDirty();
  XCSoarInterface::SendSettingsMap(true);
}


void
Pages::SetLayout(unsigned page, const PageLayout &layout)
{
  if (page >= MAX_PAGES)
    return;

  if (pages[page] != layout) {
    pages[page] = layout;
    SavePageToProfile(page);
  }

  if (page == Current)
    Update();
}


Pages::PageLayout*
Pages::GetLayout(unsigned page)
{
  if (page >= MAX_PAGES)
    return NULL;

  return &pages[page];
}


void
Pages::PageLayout::MakeTitle(TCHAR* buffer, const bool concise) const
{
  switch (topLayout) {
    case PageLayout::tlMap:
      if (concise)
        _tcscpy(buffer, _("Info hide"));
      else 
        _tcscpy(buffer, _("Map (Full screen)"));
      break;
    case PageLayout::tlMapAndInfoBoxes:
      if (!infoBoxConfig.autoSwitch &&
          infoBoxConfig.panel < InfoBoxManagerConfig::MAX_INFOBOX_PANELS) {
        if (concise) {
          _tcscpy(buffer, _("Info "));
          _tcscat(buffer, InfoBoxManager::GetPanelName(infoBoxConfig.panel));
        } else {
          _tcscpy(buffer, _("Map and InfoBoxes "));
          _tcscat(buffer, InfoBoxManager::GetPanelName(infoBoxConfig.panel));
        }
      }
      else {
        if (concise) {
          _tcscpy(buffer, _("Info auto"));
        } else {
          _tcscpy(buffer, _("Map and InfoBoxes (Auto)"));
        }
      }
      break;
    default:
      _tcscpy(buffer, _T("---"));
      break;
  }
}


void
Pages::SavePageToProfile(unsigned page)
{
  TCHAR profileKey[32];
  unsigned prefixLen = _stprintf(profileKey, _T("Page%u"), page);
  if (prefixLen <= 0)
    return;
  _tcscpy(profileKey + prefixLen, _T("InfoBoxMode"));
  Profile::Set(profileKey, pages[page].infoBoxConfig.autoSwitch);
  _tcscpy(profileKey + prefixLen, _T("InfoBoxPanel"));
  Profile::Set(profileKey, pages[page].infoBoxConfig.panel);
  _tcscpy(profileKey + prefixLen, _T("Layout"));
  Profile::Set(profileKey, pages[page].topLayout);
}


void
Pages::SaveToProfile()
{
  for (unsigned i = 0; i < MAX_PAGES; i++)
    SavePageToProfile(i);
}


void
Pages::LoadPageFromProfile(unsigned page)
{
  TCHAR profileKey[32];
  unsigned prefixLen = _stprintf(profileKey, _T("Page%u"), page);
  if (prefixLen <= 0)
    return;

  PageLayout pl;
  _tcscpy(profileKey + prefixLen, _T("InfoBoxMode"));
  if (!Profile::Get(profileKey, pl.infoBoxConfig.autoSwitch))
    return;
  _tcscpy(profileKey + prefixLen, _T("InfoBoxPanel"));
  if (!Profile::Get(profileKey, pl.infoBoxConfig.panel))
    return;
  _tcscpy(profileKey + prefixLen, _T("Layout"));
  unsigned temp = 0;
  if (!Profile::Get(profileKey, temp))
    return;
  pl.topLayout = (PageLayout::eTopLayout) temp;
  if (pl.topLayout > PageLayout::tlLAST)
    return;
  if (pl.infoBoxConfig.panel >= InfoBoxManagerConfig::MAX_INFOBOX_PANELS)
    return;
  if (page == 0 && pl.topLayout == PageLayout::tlEmpty)
    return;

  pages[page] = pl;
}


void
Pages::LoadFromProfile()
{
  LoadDefault();
  for (unsigned i = 0; i < MAX_PAGES; i++)
    LoadPageFromProfile(i);
  Update();
}


void
Pages::addValidLayout(const PageLayout& pl)
{
  assert(validLayoutsCnt < MAX_VALID_LAYOUTS);
  validLayouts[validLayoutsCnt++] = pl;
}


void
Pages::LoadDefault()
{
  pages[0]=PageLayout(PageLayout::tlMapAndInfoBoxes);
  pages[1]=PageLayout(PageLayout::tlMapAndInfoBoxes, InfoBoxConfig(false, 3));
  pages[2]=PageLayout(PageLayout::tlMap);

  addValidLayout(PageLayout());
  addValidLayout(PageLayout(PageLayout::tlMapAndInfoBoxes));
  addValidLayout(PageLayout(PageLayout::tlMap));
  for (unsigned i = 0; i < InfoBoxManagerConfig::MAX_INFOBOX_PANELS; i++)
    addValidLayout(PageLayout(PageLayout::tlMapAndInfoBoxes, InfoBoxConfig(false, i)));
}


const Pages::PageLayout*
Pages::PossiblePageLayout(unsigned i) {
  return (i < validLayoutsCnt) ? &validLayouts[i] : NULL;
}

