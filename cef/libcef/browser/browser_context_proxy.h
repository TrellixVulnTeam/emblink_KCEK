// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_PROXY_H_
#define CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_PROXY_H_
#pragma once

#include "libcef/browser/browser_context.h"
#include "libcef/browser/browser_context_impl.h"

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"

class CefDownloadManagerDelegate;
class CefStoragePartitionProxy;

// BrowserContext implementation for a particular CefRequestContext. Life span
// is controlled by CefRequestContextImpl. Only accessed on the UI thread. See
// browser_context.h for an object relationship diagram.
class CefBrowserContextProxy : public CefBrowserContext {
 public:
  CefBrowserContextProxy(CefRefPtr<CefRequestContextHandler> handler,
                         scoped_refptr<CefBrowserContextImpl> parent);

  // Must be called immediately after this object is created.
  void Initialize() override;

  // BrowserContext methods.
  base::FilePath GetPath() const override;
  std::unique_ptr<content::ZoomLevelDelegate> CreateZoomLevelDelegate(
      const base::FilePath& partition_path) override;
  bool IsOffTheRecord() const override;
  content::DownloadManagerDelegate* GetDownloadManagerDelegate() override;
  content::BrowserPluginGuestManager* GetGuestManager() override;
  storage::SpecialStoragePolicy* GetSpecialStoragePolicy() override;
  content::PushMessagingService* GetPushMessagingService() override;
  content::SSLHostStateDelegate* GetSSLHostStateDelegate() override;
  content::PermissionManager* GetPermissionManager() override;
  content::BackgroundSyncController* GetBackgroundSyncController() override;
  net::URLRequestContextGetter* CreateRequestContext(
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      override;
  net::URLRequestContextGetter* CreateRequestContextForStoragePartition(
      const base::FilePath& partition_path,
      bool in_memory,
      content::ProtocolHandlerMap* protocol_handlers,
      content::URLRequestInterceptorScopedVector request_interceptors)
      override;

  // Profile methods.
  PrefService* GetPrefs() override;
  const PrefService* GetPrefs() const override;

  // CefBrowserContext methods.
  const CefRequestContextSettings& GetSettings() const override;
  CefRefPtr<CefRequestContextHandler> GetHandler() const override;
  HostContentSettingsMap* GetHostContentSettingsMap() override;
  void AddVisitedURLs(const std::vector<GURL>& urls) override;

  content::StoragePartition* GetOrCreateStoragePartitionProxy(
    content::StoragePartition* partition_impl);

  scoped_refptr<CefBrowserContextImpl> parent() const {
    return parent_;
  }

 private:
  // Only allow deletion via scoped_refptr().
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;
  friend class base::DeleteHelper<CefBrowserContextProxy>;

  ~CefBrowserContextProxy() override;

  // Members initialized during construction are safe to access from any thread.
  CefRefPtr<CefRequestContextHandler> handler_;
  scoped_refptr<CefBrowserContextImpl> parent_;

  std::unique_ptr<CefDownloadManagerDelegate> download_manager_delegate_;
  std::unique_ptr<CefStoragePartitionProxy> storage_partition_proxy_;

  DISALLOW_COPY_AND_ASSIGN(CefBrowserContextProxy);
};

#endif  // CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_PROXY_H_
