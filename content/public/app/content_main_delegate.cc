// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/app/content_main_delegate.h"

#include "build/build_config.h"

#include "content/public/browser/content_browser_client.h"
#include "content/public/gpu/content_gpu_client.h"
#include "content/public/renderer/content_renderer_client.h"
#include "content/public/utility/content_utility_client.h"

namespace content {

bool ContentMainDelegate::BasicStartupComplete(int* exit_code) {
  return false;
}

int ContentMainDelegate::RunProcess(
    const std::string& process_type,
    const content::MainFunctionParams& main_function_params) {
  return -1;
}

#if defined(OS_MACOSX)

bool ContentMainDelegate::ProcessRegistersWithSystemProcess(
    const std::string& process_type) {
  return false;
}

bool ContentMainDelegate::ShouldSendMachPort(const std::string& process_type) {
  return true;
}

bool ContentMainDelegate::DelaySandboxInitialization(
    const std::string& process_type) {
  return false;
}

#elif defined(OS_POSIX) && !defined(OS_ANDROID)

void ContentMainDelegate::ZygoteStarting(
    ScopedVector<ZygoteForkDelegate>* delegates) {
}

#endif

bool ContentMainDelegate::ShouldEnableProfilerRecording() {
  return false;
}

ContentBrowserClient* ContentMainDelegate::CreateContentBrowserClient() {
  return new ContentBrowserClient();
}

ContentGpuClient* ContentMainDelegate::CreateContentGpuClient() {
  return new ContentGpuClient();
}

ContentRendererClient* ContentMainDelegate::CreateContentRendererClient() {
  return new ContentRendererClient();
}

ContentUtilityClient* ContentMainDelegate::CreateContentUtilityClient() {
  return new ContentUtilityClient();
}

}  // namespace content
