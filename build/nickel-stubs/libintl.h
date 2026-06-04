// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project
//
// Stub header for KOBO_NICKEL cross-builds: libstdc++ locale pulls
// <libintl.h>, but XCSoar uses its built-in MO parser on Kobo.  Provide
// enough for libstdc++ while leaving gettext() to Language.hpp.

#ifndef _LIBINTL_H
#define _LIBINTL_H

#ifdef __cplusplus
extern "C" {
#endif

static inline char *
bindtextdomain(const char *domainname, const char *dirname)
{
  (void)dirname;
  return (char *)domainname;
}

static inline char *
textdomain(const char *domainname)
{
  return (char *)domainname;
}

#ifdef __cplusplus
}
#endif

#endif
