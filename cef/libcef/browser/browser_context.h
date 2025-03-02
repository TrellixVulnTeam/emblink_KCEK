// Copyright (c) 2013 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#ifndef CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_H_
#define CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_H_
#pragma once

#include "include/cef_request_context_handler.h"
#include "libcef/browser/net/url_request_context_getter_impl.h"
#include "libcef/browser/resource_context.h"

#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"

/*
// Classes used in request processing (network, storage, service, etc.):
//
// WC = WebContents
//  Content API representation of a browser. Created by BHI or the system (for
//  popups) and owned by BHI. Keeps a pointer to BCI/BCP.
//
// BHI = CefBrowserHostImpl
//  Implements the CefBrowser and CefBrowserHost interfaces which are exposed
//  to clients. References an RCI instance. Owns a WC. Life span is controlled
//  by client references and CefContentBrowserClient.
//
// RCI = CefRequestContextImpl
//  Implements the CefRequestContext interface which is exposed to clients.
//  References the isolated BCI or creates a new BCP.
//
// BCI = CefBrowserContextImpl
//   Entry point from WC when using an isolated RCI. Owns the RC and creates the
//   SPI indirectly. Life span controlled by RCI and (for the global context)
//   CefBrowserMainParts.
//
// BCP = CefBrowserContextProxy
//   Entry point from WC when using a custom RCI. Owns the RC and creates the
//   URCGP and SPP. Life span controlled by RCI.
//
// SPI = content::StoragePartitionImpl
//   Owns storage-related objects like Quota, IndexedDB, Cache, etc. Created by
//   StoragePartitionImplMap::Get(). Provides access to the URCGI. Life span is
//   controlled indirectly by BCI.
//
// SPP = CefStoragePartitionProxy
//   Forwards requests for storage-related objects to SPI. Created by
//   GetStoragePartitionFromConfig() calling BCI::GetStoragePartitionProxy().
//   Provides access to the URCGP. Life span is controlled by BCP.
//
// RC = CefResourceContext
//   Acts as a bridge for resource loading. URLRequest life span is tied to this
//   object. Must be destroyed before the associated URCGI/URCGP. Life span is
//   controlled by BCI/BCP.
//
// URCGI = CefURLRequestContextGetterImpl
//   Creates and owns the URCI. Created by StoragePartitionImplMap::Get()
//   calling BCI::CreateRequestContext(). Life span is controlled by RC and (for
//   the global context) CefBrowserMainParts, and SPI.
//
// URCGP = CefURLRequestContextGetterProxy
//   Creates and owns the URCP. Created by GetStoragePartitionFromConfig()
//   calling BCI::GetStoragePartitionProxy(). Life span is controlled by RC and
//   SPP.
//
// URCI = CefURLRequestContextImpl
//   Owns various network-related objects including the isolated cookie manager.
//   Owns URLRequest objects which must be destroyed first. Life span is
//   controlled by URCGI.
//
// URCP = CefURLRequestContextProxy
//   Creates the CSP and forwards requests to the objects owned by URCI. Owns
//   URLRequest objects which must be destroyed first. Life span is controlled
//   by URCGP.
//
// CSP = CefCookieStoreProxy
//   Gives the CefCookieManager instance retrieved via CefRequestContextHandler
//   an opportunity to handle cookie requests. Otherwise forwards requests via
//   URCI to the isolated cookie manager. Life span is controlled by URCP.
//
//
// Relationship diagram:
//   ref = reference (CefRefPtr/scoped_refptr)
//   own = ownership (scoped_ptr)
//   ptr = raw pointer
//
//                     CefBrowserMainParts----\   isolated cookie manager, etc.
//                       |                     \             ^
//                      ref                    ref        ref/own
//                       v                      v            |
//                /---> BCI -own-> SPI -ref-> URCGI --own-> URCI <-ptr-- CSP
//               /       ^          ^           ^                        ^
//             ptr      ref        ptr         ref                      /
//             /         |          |           |                      /
// BHI -own-> WC -ptr-> BCP -own-> SPP -ref-> URCGP -own-> URCP --ref-/
//
// BHI -ref-> RCI -ref-> BCI/BCP -own-> RC -ref-> URCGI/URCGP
//
//
// How shutdown works:
// 1. CefBrowserHostImpl is destroyed on any thread due to browser close,
//    ref release, etc.
// 2. CefRequestContextImpl is destroyed on any thread due to
//    CefBrowserHostImpl destruction, ref release, etc.
// 3. CefBrowserContext* is destroyed on the UI thread due to
//    CefRequestContextImpl destruction (*Impl, *Proxy) or ref release in
//    CefBrowserMainParts::PostMainMessageLoopRun() (*Impl).
// 4. CefResourceContext is destroyed asynchronously on the IO thread due to
//    CefBrowserContext* destruction. This cancels/destroys any pending
//    URLRequests.
// 5. CefURLRequestContextGetter* is destroyed asynchronously on the IO thread
//    due to CefResourceContext destruction (*Impl, *Proxy) or ref release in
//    CefBrowserMainParts::PostMainMessageLoopRun() (*Impl). This may be delayed
//    if other network-related objects still have a reference to it.
// 6. CefURLRequestContext* is destroyed on the IO thread due to
//    CefURLRequestContextGetter* destruction. 
*/

class HostContentSettingsMap;
class PrefService;

namespace extensions {
class CefExtensionSystem;
}

// Main entry point for configuring behavior on a per-browser basis. An instance
// of this class is passed to WebContents::Create in CefBrowserHostImpl::
// CreateInternal. Only accessed on the UI thread unless otherwise indicated.
class CefBrowserContext
    : public content::BrowserContext,
      public base::RefCountedThreadSafe<
          CefBrowserContext, content::BrowserThread::DeleteOnUIThread> {
 public:
  explicit CefBrowserContext(bool is_proxy);

  // Must be called immediately after this object is created.
  virtual void Initialize();

  // BrowserContext methods.
  virtual content::ResourceContext* GetResourceContext();
  virtual net::URLRequestContextGetter* GetRequestContext();
  virtual net::URLRequestContextGetter* CreateMediaRequestContext();
  virtual net::URLRequestContextGetter* CreateMediaRequestContextForStoragePartition(
      const base::FilePath& partition_path,
      bool in_memory);

  // Returns the settings associated with this object. Safe to call from any
  // thread.
  virtual const CefRequestContextSettings& GetSettings() const = 0;

  // Returns the handler associated with this object. Safe to call from any
  // thread.
  virtual CefRefPtr<CefRequestContextHandler> GetHandler() const = 0;

  // Settings for plugins and extensions.
  virtual HostContentSettingsMap* GetHostContentSettingsMap() = 0;

  // Called from CefBrowserHostImpl::DidNavigateAnyFrame to update the table of
  // visited links.
  virtual void AddVisitedURLs(const std::vector<GURL>& urls) = 0;

  // Prefs
  virtual PrefService* GetPrefs() = 0;
  virtual const PrefService* GetPrefs() const = 0;

  // Called from CefBrowserHostImpl::RenderFrameDeleted or
  // CefMimeHandlerViewGuestDelegate::OnGuestDetached when a render frame is
  // deleted.
  void OnRenderFrameDeleted(int render_process_id,
                            int render_frame_id,
                            bool is_main_frame,
                            bool is_guest_view);

  // Called from CefRequestContextImpl::PurgePluginListCacheInternal when the
  // plugin list cache should be purged.
  void OnPurgePluginListCache();

  CefResourceContext* resource_context() const {
    return resource_context_.get();
  }

#if DCHECK_IS_ON()
  // Simple tracking of allocated objects.
  static base::AtomicRefCount DebugObjCt;  // NOLINT(runtime/int)
#endif

 protected:
  ~CefBrowserContext() override;

  // Must be called before the child object destructor has completed.
  void Shutdown();

 private:
  // Only allow deletion via scoped_refptr().
  friend struct content::BrowserThread::DeleteOnThread<
      content::BrowserThread::UI>;
  friend class base::DeleteHelper<CefBrowserContext>;

  void RenderFrameDeletedOnIOThread(int render_process_id,
                                    int render_frame_id,
                                    bool is_main_frame,
                                    bool is_guest_view);
  void PurgePluginListCacheOnIOThread();

  // True if this CefBrowserContext is a CefBrowserContextProxy.
  const bool is_proxy_;

  std::unique_ptr<CefResourceContext> resource_context_;

  DISALLOW_COPY_AND_ASSIGN(CefBrowserContext);
};

#endif  // CEF_LIBCEF_BROWSER_BROWSER_CONTEXT_H_
