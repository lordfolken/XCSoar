// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "DownloadFilePicker.hpp"
#include "Error.hpp"
#include "WidgetDialog.hpp"
#include "ProgressDialog.hpp"
#include "Message.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "Renderer/TextRowRenderer.hpp"
#include "Form/Button.hpp"
#include "Widget/ListWidget.hpp"
#include "Language/Language.hpp"
#include "LocalPath.hpp"
#include "system/Path.hpp"
#include "system/FileUtil.hpp"
#include "io/FileLineReader.hpp"
#include "Repository/Glue.hpp"
#include "Repository/FileRepository.hpp"
#include "Repository/Parser.hpp"
#include "net/http/Features.hpp"
#include "net/http/DownloadManager.hpp"
#include "ui/event/Notify.hpp"
#include "ui/event/PeriodicTimer.hpp"
#include "thread/Mutex.hxx"
#include "Operation/ThreadedOperationEnvironment.hpp"
#include "util/ConvertString.hpp"
#include "LogFile.hpp"

#include <vector>
#include <thread>
#include <chrono>

#include <cassert>

/**
 * This class tracks a download and updates a #ProgressDialog.
 */
class DownloadProgress final : Net::DownloadListener {
  ProgressDialog &dialog;
  ThreadedOperationEnvironment env;
  const Path path_relative;

  UI::PeriodicTimer update_timer{[this]{ Net::DownloadManager::Enumerate(*this); }};

  UI::Notify download_complete_notify{[this]{ OnDownloadCompleteNotification(); }};

  std::exception_ptr error;

  bool got_size = false, complete = false, success;

public:
  DownloadProgress(ProgressDialog &_dialog,
                   const Path _path_relative)
    :dialog(_dialog), env(_dialog), path_relative(_path_relative) {
    update_timer.Schedule(std::chrono::seconds(1));
    Net::DownloadManager::AddListener(*this);
  }

  ~DownloadProgress() {
    Net::DownloadManager::RemoveListener(*this);
  }

  void Rethrow() const {
    if (error)
      std::rethrow_exception(error);
  }

private:
  /* virtual methods from class Net::DownloadListener */
  void OnDownloadAdded(Path _path_relative,
                       int64_t size, int64_t position) noexcept override {
    if (!complete && path_relative == _path_relative) {
      if (!got_size && size >= 0) {
        got_size = true;
        env.SetProgressRange(uint64_t(size) / 1024u);
      }

      if (got_size)
        env.SetProgressPosition(uint64_t(position) / 1024u);
    }
  }

  void OnDownloadComplete(Path _path_relative) noexcept override {
    if (!complete && path_relative == _path_relative) {
      complete = true;
      success = true;
      download_complete_notify.SendNotification();
    }
  }

  void OnDownloadError(Path _path_relative,
                       std::exception_ptr _error) noexcept override {
    if (!complete && path_relative == _path_relative) {
      complete = true;
      success = false;
      error = std::move(_error);
      download_complete_notify.SendNotification();
    }
  }

  void OnDownloadCompleteNotification() noexcept {
    assert(complete);
    dialog.SetModalResult(success ? mrOK : mrCancel);
  }
};

/**
 * Throws on error.
 */
static AllocatedPath
DownloadFile(const char *uri, const char *_base)
{
  assert(Net::DownloadManager::IsAvailable());

  const UTF8ToWideConverter base(_base);
  if (!base.IsValid())
    return nullptr;

  ProgressDialog dialog(UIGlobals::GetMainWindow(), UIGlobals::GetDialogLook(),
                        _("Download"));
  dialog.SetText(base);

  dialog.AddCancelButton();

  const DownloadProgress dp(dialog, Path(base));

  Net::DownloadManager::Enqueue(uri, Path(base));

  int result = dialog.ShowModal();
  if (result != mrOK) {
    Net::DownloadManager::Cancel(Path(base));
    dp.Rethrow();
    return nullptr;
  }

  return LocalPath(base);
}

class DownloadFilePickerWidget final
  : public ListWidget,
    Net::DownloadListener {

  WidgetDialog &dialog;

  UI::Notify download_complete_notify{[this]{ OnDownloadCompleteNotification(); }};

  const FileType file_type;

  unsigned font_height;

  Button *download_button;

  std::vector<AvailableFile> items;

  TextRowRenderer row_renderer;

  /**
   * This mutex protects the attribute "repository_modified".
   */
  mutable Mutex mutex;

  /**
   * Was the repository file modified, and needs to be reloaded by
   * RefreshList()?
   */
  bool repository_modified;

  /**
   * Has the repository file download failed?
   */
  bool repository_failed;

  std::exception_ptr repository_error;

  AllocatedPath path;

public:
  DownloadFilePickerWidget(WidgetDialog &_dialog, FileType _file_type)
    :dialog(_dialog), file_type(_file_type) {}

  AllocatedPath &&GetPath() {
    return std::move(path);
  }

  void CreateButtons();

protected:
  void RefreshList();

  void UpdateButtons() {
      download_button->SetEnabled(!items.empty());
  }

  void Download();
  void Cancel();

public:
  /* virtual methods from class Widget */
  void Prepare(ContainerWindow &parent, const PixelRect &rc) noexcept override;
  void Unprepare() noexcept override;

  /* virtual methods from class ListItemRenderer */
  void OnPaintItem(Canvas &canvas, const PixelRect rc,
                   unsigned idx) noexcept override;

  /* virtual methods from class ListCursorHandler */
  bool CanActivateItem([[maybe_unused]] unsigned index) const noexcept override {
    return true;
  }

  void OnActivateItem([[maybe_unused]] unsigned index) noexcept override {
    Download();
  }

  /* virtual methods from class Net::DownloadListener */
  void OnDownloadAdded(Path path_relative,
                       int64_t size, int64_t position) noexcept override;
  void OnDownloadComplete(Path path_relative) noexcept override;
  void OnDownloadError(Path path_relative,
                       std::exception_ptr error) noexcept override;

  void OnDownloadCompleteNotification() noexcept;
};

void
DownloadFilePickerWidget::Prepare(ContainerWindow &parent,
                                  const PixelRect &rc) noexcept
{
  const DialogLook &look = UIGlobals::GetDialogLook();

  CreateList(parent, look, rc,
             row_renderer.CalculateLayout(*look.list.font));

  Net::DownloadManager::AddListener(*this);
  
  /* Enqueue repository download before enumerate, so Enumerate() will see
     it as pending rather than triggering a premature OnDownloadComplete() */
  EnqueueRepositoryDownload();
  
  Net::DownloadManager::Enumerate(*this);

  /* Refresh list after download manager setup. If repository file already
     exists, it will be loaded. If repository is still downloading or just
     completed via Enumerate(), OnDownloadComplete() will trigger another
     RefreshList() when the file is ready.
     
     If RefreshList() fails here (e.g., repository file doesn't exist yet),
     don't set repository_failed - wait for the download to complete first.
     The error will be shown if RefreshList() fails after OnDownloadComplete()
     is called. */
  try {
    RefreshList();
  } catch (...) {
    /* Ignore errors during initial RefreshList() - the repository might
       not be downloaded yet. Errors will be shown if RefreshList() fails
       after the repository download completes. */
  }
}

void
DownloadFilePickerWidget::Unprepare() noexcept
{
  Net::DownloadManager::RemoveListener(*this);
}

void
DownloadFilePickerWidget::RefreshList()
try {
  {
    const std::lock_guard lock{mutex};
    repository_modified = false;
    repository_failed = false;
  }

  FileRepository repository;

  const auto path = LocalPath(_T("repository"));
  
  /* Retry logic: DownloadManager may report STATUS_SUCCESSFUL before the file
     is fully flushed to disk, or may create an empty placeholder file before
     writing content. Wait up to 500ms for the file to become accessible and
     contain data. This happens in native code (not blocking Java/UI thread). */
  std::exception_ptr last_exception;
  for (int attempt = 0; attempt < 10; ++attempt) {
    try {
      /* Check if file exists before trying to read it. If it doesn't exist,
         the download may not have started yet or may still be in progress. */
      if (!File::Exists(path)) {
        if (attempt < 9) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        } else {
          throw std::runtime_error("Repository file does not exist");
        }
      }
      
      FileLineReaderA reader(path);
      ParseFileRepository(repository, reader);
      
      /* If repository is empty, the download may not be complete yet.
         Treat as failure and retry (unless this is the last attempt). */
      if (repository.files.empty() && attempt < 9) {
        throw std::runtime_error("Repository file is empty");
      }
      
      goto success;
    } catch (...) {
      last_exception = std::current_exception();
      if (attempt < 9)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
  
  /* All retries failed, throw a new exception with error message */
  if (last_exception) {
    try {
      std::rethrow_exception(last_exception);
    } catch (const std::exception &e) {
      throw std::runtime_error(e.what());
    } catch (...) {
      throw std::runtime_error("Failed to load repository file after retries");
    }
  } else {
    throw std::runtime_error("Repository file does not exist");
  }
  
success:
  items.clear();
  for (auto &i : repository)
    if (i.type == file_type)
      items.emplace_back(std::move(i));

  ListControl &list = GetList();
  list.SetLength(items.size());
  list.Invalidate();

  UpdateButtons();
} catch (const std::runtime_error &e) {
  /* If RefreshList() fails (e.g., repository file is unreadable or empty
     after all retries), rethrow the exception so the caller can handle it.
     This allows OnDownloadCompleteNotification() to show an error if RefreshList()
     fails after a repository download completes, while Prepare() can silently
     ignore the error if the repository hasn't been downloaded yet. */
  throw;
}

void
DownloadFilePickerWidget::CreateButtons()
{
  download_button = dialog.AddButton(_("Download"), [this](){ Download(); });

  UpdateButtons();
}

void
DownloadFilePickerWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                      unsigned i) noexcept
{
  const auto &file = items[i];

  const UTF8ToWideConverter name(file.GetName());
  row_renderer.DrawTextRow(canvas, rc, name);
}

void
DownloadFilePickerWidget::Download()
{
  assert(Net::DownloadManager::IsAvailable());

  const unsigned current = GetList().GetCursorIndex();
  assert(current < items.size());

  const auto &file = items[current];

  try {
    path = DownloadFile(file.GetURI(), file.GetName());
    if (path != nullptr)
      dialog.SetModalResult(mrOK);
  } catch (...) {
    ShowError(std::current_exception(), _("Error"));
  }
}

void
DownloadFilePickerWidget::OnDownloadAdded([[maybe_unused]] Path path_relative,
                                          [[maybe_unused]] int64_t size,
                                          [[maybe_unused]] int64_t position) noexcept
{
}

void
DownloadFilePickerWidget::OnDownloadComplete(Path path_relative) noexcept
{
  const auto name = path_relative.GetBase();
  if (name == nullptr)
    return;

  if (name == Path(_T("repository"))) {
    const std::lock_guard lock{mutex};
    repository_failed = false;
    repository_modified = true;
    download_complete_notify.SendNotification();
  }
}

void
DownloadFilePickerWidget::OnDownloadError(Path path_relative,
                                          std::exception_ptr error) noexcept
{
  const auto name = path_relative.GetBase();
  if (name == nullptr)
    return;

  if (name == Path(_T("repository"))) {
    const std::lock_guard lock{mutex};
    /* LogError() calls GetFullMessage() which rethrows the exception.
       Wrap in try-catch to prevent exceptions from escaping noexcept function. */
    try {
      LogError(error);
    } catch (...) {
    }
    repository_failed = true;
    repository_error = std::move(error);
    download_complete_notify.SendNotification();
  }
}

void
DownloadFilePickerWidget::OnDownloadCompleteNotification() noexcept
{
  bool repository_modified2, repository_failed2;
  std::exception_ptr repository_error2;

  {
    const std::lock_guard lock{mutex};
    repository_modified2 = std::exchange(repository_modified, false);
    repository_failed2 = std::exchange(repository_failed, false);
    repository_error2 = std::move(repository_error);
  }

  if (repository_error2) {
    /* ShowError() calls GetFullMessage() which rethrows the exception.
       Wrap in try-catch to prevent exceptions from escaping noexcept function. */
    try {
      ShowError(std::move(repository_error2),
                _("Failed to download the repository index."));
    } catch (...) {
      ShowMessageBox(_("Failed to download the repository index."),
                     _("Error"), MB_OK);
    }
  } else if (repository_failed2) {
    try {
      ShowMessageBox(_("Failed to download the repository index."),
                     _("Error"), MB_OK);
    } catch (...) {
    }
  } else if (repository_modified2) {
    /* Try to refresh the list. If it fails (e.g., repository file is
       unreadable or empty), show an error. */
    try {
      RefreshList();
    } catch (const std::exception &e) {
      /* RefreshList() failed after repository download completed.
         This indicates the repository file is corrupted or unreadable. */
      try {
        ShowMessageBox(_("Failed to download the repository index."),
                       _("Error"), MB_OK);
      } catch (...) {
      }
    } catch (...) {
      /* Catch-all for any other exceptions */
      try {
        ShowMessageBox(_("Failed to download the repository index."),
                       _("Error"), MB_OK);
      } catch (...) {
      }
    }
  }
}

AllocatedPath
DownloadFilePicker(FileType file_type)
{
  if (!Net::DownloadManager::IsAvailable()) {
    const TCHAR *message =
      _("The file manager is not available on this device.");
    ShowMessageBox(message, _("File Manager"), MB_OK);
    return nullptr;
  }

  TWidgetDialog<DownloadFilePickerWidget>
    dialog(WidgetDialog::Full{}, UIGlobals::GetMainWindow(),
           UIGlobals::GetDialogLook(), _("Download"));
  dialog.AddButton(_("Cancel"), mrCancel);
  dialog.SetWidget(dialog, file_type);
  dialog.GetWidget().CreateButtons();
  dialog.ShowModal();

  return dialog.GetWidget().GetPath();
}
