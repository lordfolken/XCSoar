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

#include "File.hpp"
#include "ComboList.hpp"
#include "LocalPath.hpp"
#include "Util/StringUtil.hpp"
#include "Util/StringAPI.hpp"
#include "OS/PathName.hpp"
#include "OS/FileUtil.hpp"

#if defined(_WIN32_WCE) && !defined(GNAV)
#include "OS/FlashCardEnumerator.hpp"
#endif

#include <algorithm>

#include <windef.h> /* for MAX_PATH */
#include <assert.h>
#include <stdlib.h>

/**
 * Checks whether the given string str equals a xcsoar internal file's filename
 * @param str The string to check
 * @return True if string equals a xcsoar internal file's filename
 */
static bool
IsInternalFile(const TCHAR* str)
{
  static const TCHAR *const ifiles[] = {
    _T("xcsoar-checklist.txt"),
    _T("xcsoar-flarm.txt"),
    _T("xcsoar-marks.txt"),
    _T("xcsoar-persist.log"),
    _T("xcsoar-startup.log"),
    _T("xcsoar.log"),
    _T("xcsoar-rasp.dat"),
    nullptr
  };

  for (unsigned i = 0; ifiles[i] != nullptr; i++)
    if (StringIsEqual(str, ifiles[i]))
      return true;

  return false;
}

class FileVisitor: public File::Visitor
{
private:
  FileDataField &datafield;

public:
  FileVisitor(FileDataField &_datafield) : datafield(_datafield) {}

  void
  Visit(const TCHAR* path, const TCHAR* filename)
  {
    if (!IsInternalFile(filename))
      datafield.AddFile(filename, path);
  }
};

FileDataField::Item::~Item()
{
  free(path);
}

inline void
FileDataField::Item::Set(const TCHAR *_path)
{
  free(path);
  path = _tcsdup(_path);
  filename = BaseName(path);
  if (filename == nullptr)
    filename = path;
}

FileDataField::FileDataField(DataFieldListener *listener)
  :DataField(Type::FILE, true, listener),
   // Set selection to zero
   current_index(0),
   loaded(false), postponed_sort(false),
   postponed_value(_T("")) {}

int
FileDataField::GetAsInteger() const
{
  if (!postponed_value.empty())
    EnsureLoadedDeconst();

  return current_index;
}

void
FileDataField::SetAsInteger(int new_value)
{
  Set(new_value);
}

void
FileDataField::ScanDirectoryTop(const TCHAR *filter)
{
  if (!loaded) {
    if (!postponed_patterns.full() &&
        _tcslen(filter) < PatternList::value_type::CAPACITY) {
      postponed_patterns.append() = filter;
      return;
    } else
      EnsureLoaded();
  }

  FileVisitor fv(*this);
  VisitDataFiles(filter, fv);

  Sort();
}

void
FileDataField::ScanMultiplePatterns(const TCHAR *patterns)
{
  size_t length;
  while ((length = _tcslen(patterns)) > 0) {
    ScanDirectoryTop(patterns);
    patterns += length + 1;
  }
}

int
FileDataField::Find(const TCHAR *text) const
{
  for (unsigned i = 0, n = files.size(); i < n; i++)
    if (StringIsEqual(text, files[i].path))
      return i;

  return -1;
}

void
FileDataField::Lookup(const TCHAR *text)
{
  if (!loaded) {
    if (_tcslen(text) < postponed_value.capacity()) {
      postponed_value = text;
      return;
    } else
      EnsureLoaded();
  }

  auto i = Find(text);
  if (i >= 0)
    current_index = i;
}

void
FileDataField::ForceModify(const TCHAR *path)
{
  EnsureLoaded();

  auto i = Find(path);
  if (i >= 0) {
    if (unsigned(i) == current_index)
      return;
  } else {
    auto &item = files.full() ? files.back() : files.append();
    item.Set(path);
    i = files.size() - 1;
  }

  current_index = i;
  Modified();
}

unsigned
FileDataField::GetNumFiles() const
{
  EnsureLoadedDeconst();

  return files.size();
}

const TCHAR *
FileDataField::GetPathFile() const
{
  if (!loaded)
    return postponed_value;

  if (current_index >= files.size())
    return _T("");

  const TCHAR *path = files[current_index].path;
  assert(path != nullptr);
  return path;
}

void
FileDataField::AddFile(const TCHAR *filename, const TCHAR *path)
{
  assert(loaded);

  // TODO enhancement: remove duplicates?

  // if too many files -> cancel
  if (files.full())
    return;

  Item &item = files.append();
  item.Set(path);
}

void
FileDataField::AddNull()
{
  assert(!files.full());

  Item &item = files.append();
  item.filename = _T("");
  item.path = _tcsdup(_T(""));
}

const TCHAR *
FileDataField::GetAsString() const
{
  if (!loaded)
    return postponed_value;

  if (current_index < files.size())
    return files[current_index].path;
  else
    return _T("");
}

const TCHAR *
FileDataField::GetAsDisplayString() const
{
  if (!loaded) {
    /* get basename from postponed_value */
    const TCHAR *p = BaseName(postponed_value);
    if (p == nullptr)
      p = postponed_value;

    return p;
  }

  if (current_index < files.size())
    return files[current_index].filename;
  else
    return _T("");
}

void
FileDataField::Set(unsigned new_value)
{
  if (new_value > 0)
    EnsureLoaded();
  else
    postponed_value.clear();

  if (new_value < files.size()) {
    current_index = new_value;
    Modified();
  }
}

void
FileDataField::Inc()
{
  EnsureLoaded();

  if (current_index < files.size() - 1) {
    current_index++;
    Modified();
  }
}

void
FileDataField::Dec()
{
  if (current_index > 0) {
    current_index--;
    Modified();
  }
}

void
FileDataField::Sort()
{
  if (!loaded) {
    postponed_sort = true;
    return;
  }

  // Sort the filelist (except for the first (empty) element)
  std::sort(files.begin(), files.end(), [](const Item &a,
                                           const Item &b) {
              // Compare by filename
              return StringCollate(a.filename, b.filename) < 0;
            });
}

ComboList
FileDataField::CreateComboList(const TCHAR *reference) const
{
  /* sorry for the const_cast .. this method keeps the promise of not
     modifying the object, given that one does not count filling the
     (cached) file list as "modification" */
  EnsureLoadedDeconst();

  ComboList combo_list;

  TCHAR buffer[MAX_PATH];

  for (unsigned i = 0; i < files.size(); i++) {
    const TCHAR *path = files[i].filename;
    assert(path != nullptr);

    /* is a file with the same base name present in another data
       directory? */

    bool found = false;
    for (unsigned j = 1; j < files.size(); j++) {
      if (j != i && StringIsEqual(path, files[j].filename)) {
        found = true;
        break;
      }
    }

    if (found) {
      /* yes - append the absolute path to allow the user to see the
         difference */
      _tcscpy(buffer, path);
      _tcscat(buffer, _T(" ("));
      _tcscat(buffer, files[i].path);
      _tcscat(buffer, _T(")"));
      path = buffer;
    }

    combo_list.Append(i, path);
  }

  combo_list.current_index = current_index;

  return combo_list;
}

unsigned
FileDataField::size() const
{
  EnsureLoadedDeconst();

  return files.size();
}

const TCHAR *
FileDataField::GetItem(unsigned index) const
{
  EnsureLoadedDeconst();

  return files[index].path;
}

void
FileDataField::EnsureLoaded()
{
  if (loaded)
    return;

  loaded = true;

  for (auto i = postponed_patterns.begin(), end = postponed_patterns.end();
       i != end; ++i)
    ScanDirectoryTop(*i);

  if (postponed_sort)
    Sort();

  if (!StringIsEmpty(postponed_value))
    Lookup(postponed_value);
}
