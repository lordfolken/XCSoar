// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "Glue.hpp"
#include "LocalPath.hpp"
#include "net/http/DownloadManager.hpp"
#include "system/Path.hpp"

#include <tchar.h>

#define REPOSITORY_URI "http://download.xcsoar.org/repository"

static bool repository_downloaded = false;

void
EnqueueRepositoryDownload(bool force)
{
  if (repository_downloaded && !force)
    return;

  const auto path = AllocatedPath::Build(
      RelativePath(MakeCacheDirectory(_T("repo"))), _T("repository"));
  repository_downloaded = true;
  Net::DownloadManager::Enqueue(REPOSITORY_URI, path);
}
