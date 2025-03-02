// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/shell_browser_context.h"

#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/environment.h"
#include "base/files/file_util.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/threading/thread.h"
#include "build/build_config.h"
#include "content/public/browser/background_sync_controller.h"
#include "content/public/browser/browser_plugin_guest_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "content/shell/browser/shell_download_manager_delegate.h"
#include "content/shell/browser/shell_permission_manager.h"
#include "content/shell/common/shell_switches.h"

#if defined(OS_WIN)
#include "base/base_paths_win.h"
#elif defined(OS_LINUX)
#include "base/nix/xdg_util.h"
#elif defined(OS_MACOSX)
#include "base/base_paths_mac.h"
#endif

namespace content {

ShellBrowserContext::ShellResourceContext::ShellResourceContext()
    : getter_(NULL) {
}

ShellBrowserContext::ShellResourceContext::~ShellResourceContext() {
}

net::HostResolver*
ShellBrowserContext::ShellResourceContext::GetHostResolver() {
  CHECK(getter_);
  return getter_->host_resolver();
}

net::URLRequestContext*
ShellBrowserContext::ShellResourceContext::GetRequestContext() {
  CHECK(getter_);
  return getter_->GetURLRequestContext();
}

ShellBrowserContext::ShellBrowserContext(bool off_the_record,
                                         net::NetLog* net_log)
    : resource_context_(new ShellResourceContext),
      ignore_certificate_errors_(false),
      off_the_record_(off_the_record),
      net_log_(net_log),
      guest_manager_(NULL) {
  InitWhileIOAllowed();
}

ShellBrowserContext::~ShellBrowserContext() {
  ShutdownStoragePartitions();
  if (resource_context_) {
    BrowserThread::DeleteSoon(
      BrowserThread::IO, FROM_HERE, resource_context_.release());
  }
}

void ShellBrowserContext::InitWhileIOAllowed() {
  base::CommandLine* cmd_line = base::CommandLine::ForCurrentProcess();
  if (cmd_line->HasSwitch(switches::kIgnoreCertificateErrors))
    ignore_certificate_errors_ = true;
  if (cmd_line->HasSwitch(switches::kContentShellDataPath)) {
    path_ = cmd_line->GetSwitchValuePath(switches::kContentShellDataPath);
    if (base::DirectoryExists(path_) || base::CreateDirectory(path_))  {
      // BrowserContext needs an absolute path, which we would normally get via
      // PathService. In this case, manually ensure the path is absolute.
      if (!path_.IsAbsolute())
        path_ = base::MakeAbsoluteFilePath(path_);
      if (!path_.empty()) {
        BrowserContext::Initialize(this, path_);
        return;
      }
    } else {
      LOG(WARNING) << "Unable to create data-path directory: " << path_.value();
    }
  }

#if defined(OS_WIN)
  CHECK(PathService::Get(base::DIR_LOCAL_APP_DATA, &path_));
  path_ = path_.Append(std::wstring(L"content_shell"));
#elif defined(OS_LINUX)
  std::unique_ptr<base::Environment> env(base::Environment::Create());
  base::FilePath config_dir(
      base::nix::GetXDGDirectory(env.get(),
                                 base::nix::kXdgConfigHomeEnvVar,
                                 base::nix::kDotConfigDir));
  path_ = config_dir.Append("content_shell");
#elif defined(OS_MACOSX)
  CHECK(PathService::Get(base::DIR_APP_DATA, &path_));
  path_ = path_.Append("Chromium Content Shell");
#elif defined(OS_ANDROID)
  CHECK(PathService::Get(base::DIR_ANDROID_APP_DATA, &path_));
  path_ = path_.Append(FILE_PATH_LITERAL("content_shell"));
#else
  NOTIMPLEMENTED();
#endif

  if (!base::PathExists(path_))
    base::CreateDirectory(path_);
  BrowserContext::Initialize(this, path_);
}

std::unique_ptr<ZoomLevelDelegate> ShellBrowserContext::CreateZoomLevelDelegate(
    const base::FilePath&) {
  return std::unique_ptr<ZoomLevelDelegate>();
}

base::FilePath ShellBrowserContext::GetPath() const {
  return path_;
}

bool ShellBrowserContext::IsOffTheRecord() const {
  return off_the_record_;
}

DownloadManagerDelegate* ShellBrowserContext::GetDownloadManagerDelegate()  {
  if (!download_manager_delegate_.get()) {
    download_manager_delegate_.reset(new ShellDownloadManagerDelegate());
    download_manager_delegate_->SetDownloadManager(
        BrowserContext::GetDownloadManager(this));
  }

  return download_manager_delegate_.get();
}

ShellURLRequestContextGetter*
ShellBrowserContext::CreateURLRequestContextGetter(
    ProtocolHandlerMap* protocol_handlers,
    URLRequestInterceptorScopedVector request_interceptors) {
  return new ShellURLRequestContextGetter(
      ignore_certificate_errors_, GetPath(),
      BrowserThread::GetTaskRunnerForThread(BrowserThread::IO),
      BrowserThread::GetTaskRunnerForThread(BrowserThread::FILE),
      protocol_handlers, std::move(request_interceptors), net_log_);
}

net::URLRequestContextGetter* ShellBrowserContext::CreateRequestContext(
    ProtocolHandlerMap* protocol_handlers,
    URLRequestInterceptorScopedVector request_interceptors) {
  DCHECK(!url_request_getter_.get());
  url_request_getter_ = CreateURLRequestContextGetter(
      protocol_handlers, std::move(request_interceptors));
  resource_context_->set_url_request_context_getter(url_request_getter_.get());
  return url_request_getter_.get();
}

net::URLRequestContextGetter*
ShellBrowserContext::CreateRequestContextForStoragePartition(
    const base::FilePath& partition_path,
    bool in_memory,
    ProtocolHandlerMap* protocol_handlers,
    URLRequestInterceptorScopedVector request_interceptors) {
  return nullptr;
}

net::URLRequestContextGetter*
    ShellBrowserContext::CreateMediaRequestContext()  {
  DCHECK(url_request_getter_.get());
  return url_request_getter_.get();
}

net::URLRequestContextGetter*
    ShellBrowserContext::CreateMediaRequestContextForStoragePartition(
        const base::FilePath& partition_path,
        bool in_memory) {
  return nullptr;
}

ResourceContext* ShellBrowserContext::GetResourceContext()  {
  return resource_context_.get();
}

BrowserPluginGuestManager* ShellBrowserContext::GetGuestManager() {
  return guest_manager_;
}

storage::SpecialStoragePolicy* ShellBrowserContext::GetSpecialStoragePolicy() {
  return NULL;
}

PushMessagingService* ShellBrowserContext::GetPushMessagingService() {
  return NULL;
}

SSLHostStateDelegate* ShellBrowserContext::GetSSLHostStateDelegate() {
  return NULL;
}

PermissionManager* ShellBrowserContext::GetPermissionManager() {
  if (!permission_manager_.get())
    permission_manager_.reset(new ShellPermissionManager());
  return permission_manager_.get();
}

BackgroundSyncController* ShellBrowserContext::GetBackgroundSyncController() {
  // if (!background_sync_controller_)
  //   background_sync_controller_.reset(new MockBackgroundSyncController());
  return background_sync_controller_.get();
}

}  // namespace content
