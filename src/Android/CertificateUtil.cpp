// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The XCSoar Project

#include "CertificateUtil.hpp"
#include "Context.hpp"
#include "java/Class.hxx"
#include "java/String.hxx"
#include "java/Path.hxx"
#include "system/FileUtil.hpp"
#include "LogFile.hpp"

namespace CertificateUtil {

static Java::TrivialClass cls;
static jmethodID extractSystemCertificates_method;

void
Initialise(JNIEnv *env) noexcept
{
  cls.Find(env, "org/xcsoar/CertificateUtil");

  extractSystemCertificates_method =
    env->GetStaticMethodID(cls, "extractSystemCertificates",
                          "(Landroid/content/Context;)Ljava/lang/String;");
}

void
Deinitialise(JNIEnv *env) noexcept
{
  cls.Clear(env);
}

AllocatedPath
GetSystemCaCertificatesPath(JNIEnv *env, Context &context) noexcept
{
  assert(env != nullptr);
  assert(cls.IsDefined());

  Java::String path{env, (jstring)env->CallStaticObjectMethod(
    cls, extractSystemCertificates_method, context.Get())};

  if (!path) {
    LogFormat("CertificateUtil: Failed to extract system certificates");
    return nullptr;
  }

  AllocatedPath pem_path = Java::ToPathChecked(path);
  if (pem_path == nullptr) {
    LogFormat("CertificateUtil: Certificate path is null");
    return nullptr;
  }

  // Verify file exists
  if (!File::Exists(pem_path)) {
    LogFormat("CertificateUtil: PEM file does not exist: %s",
              pem_path.c_str());
    return nullptr;
  }

  return pem_path;
}

} // namespace CertificateUtil
