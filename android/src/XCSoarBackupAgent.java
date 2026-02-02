// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

package org.xcsoar;

import android.app.backup.BackupAgent;
import android.app.backup.FullBackupDataOutput;

import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;

/**
 * Custom BackupAgent that includes getExternalMediaDirs() in Android Auto
 * Backup. By default Android backs up getExternalFilesDir() but not
 * getExternalMediaDirs(); XCSoar uses the latter for new users, so we add
 * it here. Default backup (internal + getExternalFilesDir) is performed by
 * super.onFullBackup().
 *
 * Subdirectories in EXCLUDED_PATHS (e.g. maps/) and file extensions in
 * EXCLUDED_EXTENSIONS (e.g. xcm) are skipped to stay within the backup quota.
 */
public class XCSoarBackupAgent extends BackupAgent {

  /** Directory names to exclude from backup (e.g. maps to stay under quota). */
  private static final Set<String> EXCLUDED_PATHS = new HashSet<>(
    Arrays.asList("maps"));

  /** File extensions to exclude from backup (e.g. xcm for map files). */
  private static final Set<String> EXCLUDED_EXTENSIONS = new HashSet<>(
    Arrays.asList("xcm"));

  @Override
  public void onFullBackup(FullBackupDataOutput data) throws IOException {
    super.onFullBackup(data);

    @SuppressWarnings("deprecation")
    File[] mediaDirs = getExternalMediaDirs();
    if (mediaDirs != null) {
      for (File dir : mediaDirs) {
        if (dir != null && dir.exists() && dir.isDirectory()) {
          fullBackupDir(dir, data);
        }
      }
    }
  }

  private void fullBackupDir(File dir, FullBackupDataOutput data)
    throws IOException {
    File[] children = dir.listFiles();
    if (children == null) {
      return;
    }
    for (File f : children) {
      if (f.isFile() && f.canRead() && !isExcludedExtension(f.getName())) {
        fullBackupFile(f, data);
      } else if (f.isDirectory() && !EXCLUDED_PATHS.contains(f.getName())) {
        fullBackupDir(f, data);
      }
    }
  }

  private static boolean isExcludedExtension(String fileName) {
    int dot = fileName.lastIndexOf('.');
    if (dot < 0 || dot == fileName.length() - 1) {
      return false;
    }
    String ext = fileName.substring(dot + 1).toLowerCase();
    return EXCLUDED_EXTENSIONS.contains(ext);
  }

  @Override
  public void onBackup(android.os.ParcelFileDescriptor oldState,
                       android.app.backup.BackupDataOutput data,
                       android.os.ParcelFileDescriptor newState)
    throws IOException {
    /* Key-value backup not used; we use full backup only. */
  }

  @Override
  public void onRestore(android.app.backup.BackupDataInput data,
                        int appVersionCode,
                        android.os.ParcelFileDescriptor newState)
    throws IOException {
    /* Key-value restore not used; full restore uses onRestoreFile. */
  }
}
