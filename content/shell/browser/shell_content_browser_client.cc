// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/shell/browser/shell_content_browser_client.h"

#include <stddef.h>
#include <utility>

#include "base/base_switches.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/macros.h"
#include "base/path_service.h"
#include "base/strings/pattern.h"
#include "base/strings/utf_string_conversions.h"
#include "build/build_config.h"
#include "content/public/browser/client_certificate_delegate.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_dispatcher_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/service_names.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/web_preferences.h"
#include "content/shell/browser/shell.h"
#include "content/shell/browser/shell_browser_context.h"
#include "content/shell/browser/shell_browser_main_parts.h"
#include "content/shell/browser/shell_devtools_manager_delegate.h"
#include "content/shell/browser/shell_net_log.h"
#include "content/shell/browser/shell_quota_permission_context.h"
#include "content/shell/browser/shell_resource_dispatcher_host_delegate.h"
#include "content/shell/browser/shell_web_contents_view_delegate_creator.h"
#include "content/shell/common/shell_messages.h"
#include "content/shell/common/shell_switches.h"
#include "grit/shell_resources.h"
#include "net/url_request/url_request_context_getter.h"
#include "ui/base/resource/resource_bundle.h"
#include "url/gurl.h"
#include "url/origin.h"

#if defined(OS_POSIX) && !defined(OS_MACOSX)
#include "base/debug/leak_annotations.h"
#include "content/public/common/content_descriptors.h"
#endif

#if defined(OS_WIN)
#include "content/common/sandbox_win.h"
#include "sandbox/win/src/sandbox.h"
#endif

#if defined(ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
#include "media/mojo/services/mojo_media_application_factory.h"  // nogncheck
#endif

namespace content {

namespace {

ShellContentBrowserClient* g_browser_client;
bool g_swap_processes_for_redirect = false;

}  // namespace

ShellContentBrowserClient* ShellContentBrowserClient::Get() {
  return g_browser_client;
}

void ShellContentBrowserClient::SetSwapProcessesForRedirect(bool swap) {
  g_swap_processes_for_redirect = swap;
}

ShellContentBrowserClient::ShellContentBrowserClient()
    : shell_browser_main_parts_(NULL) {
  DCHECK(!g_browser_client);
  g_browser_client = this;
}

ShellContentBrowserClient::~ShellContentBrowserClient() {
  g_browser_client = NULL;
}

BrowserMainParts* ShellContentBrowserClient::CreateBrowserMainParts(
    const MainFunctionParams& parameters) {
  shell_browser_main_parts_ = new ShellBrowserMainParts(parameters);
  return shell_browser_main_parts_;
}

bool ShellContentBrowserClient::DoesSiteRequireDedicatedProcess(
    BrowserContext* browser_context,
    const GURL& effective_site_url) {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  DCHECK(command_line->HasSwitch(switches::kIsolateSitesForTesting));
  std::string pattern =
      command_line->GetSwitchValueASCII(switches::kIsolateSitesForTesting);
  url::Origin origin(effective_site_url);

  // Schemes like blob or filesystem, which have an embedded origin, should
  // already have been canonicalized to the origin site.
  CHECK_EQ(origin.scheme(), effective_site_url.scheme())
      << "a site url should have the same scheme as its origin.";

  // Practically |origin.Serialize()| is the same as
  // |effective_site_url.spec()|, except Origin serialization strips the
  // trailing "/", which makes for cleaner wildcard patterns.
  return base::MatchPattern(origin.Serialize(), pattern);
}

bool ShellContentBrowserClient::IsHandledURL(const GURL& url) {
  if (!url.is_valid())
    return false;
  // Keep in sync with ProtocolHandlers added by
  // ShellURLRequestContextGetter::GetURLRequestContext().
  static const char* const kProtocolList[] = {
      url::kBlobScheme,
      url::kFileSystemScheme,
      kChromeUIScheme,
      kChromeDevToolsScheme,
      url::kDataScheme,
      url::kFileScheme,
  };
  for (size_t i = 0; i < arraysize(kProtocolList); ++i) {
    if (url.scheme() == kProtocolList[i])
      return true;
  }
  return false;
}

void ShellContentBrowserClient::RegisterInProcessServices(
    StaticServiceMap* services) {
#if (ENABLE_MOJO_MEDIA_IN_BROWSER_PROCESS)
  content::ServiceInfo info;
  info.factory = base::Bind(&media::CreateMojoMediaApplication);
  services->insert(std::make_pair("service:media", info));
#endif
}

void ShellContentBrowserClient::RegisterOutOfProcessServices(
      OutOfProcessServiceMap* services) {
  // services->insert(std::make_pair(kTestServiceUrl,
  //                                 base::UTF8ToUTF16("Test Service")));
}

std::unique_ptr<base::Value>
ShellContentBrowserClient::GetServiceManifestOverlay(
    const std::string& name) {
  int id = -1;
  if (name == content::kBrowserServiceName)
    id = IDR_CONTENT_SHELL_BROWSER_MANIFEST_OVERLAY;
  else if (name == content::kRendererServiceName)
    id = IDR_CONTENT_SHELL_RENDERER_MANIFEST_OVERLAY;
  else if (name == content::kUtilityServiceName)
    id = IDR_CONTENT_SHELL_UTILITY_MANIFEST_OVERLAY;
  if (id == -1)
    return nullptr;

  base::StringPiece manifest_contents =
      ui::ResourceBundle::GetSharedInstance().GetRawDataResourceForScale(
          id, ui::ScaleFactor::SCALE_FACTOR_NONE);
  return base::JSONReader::Read(manifest_contents);
}

void ShellContentBrowserClient::AppendExtraCommandLineSwitches(
    base::CommandLine* command_line,
    int child_process_id) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kExposeInternalsForTesting)) {
    command_line->AppendSwitch(switches::kExposeInternalsForTesting);
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableCrashReporter)) {
    command_line->AppendSwitch(switches::kEnableCrashReporter);
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kCrashDumpsDir)) {
    command_line->AppendSwitchPath(
        switches::kCrashDumpsDir,
        base::CommandLine::ForCurrentProcess()->GetSwitchValuePath(
            switches::kCrashDumpsDir));
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kIsolateSitesForTesting)) {
    command_line->AppendSwitchASCII(
        switches::kIsolateSitesForTesting,
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kIsolateSitesForTesting));
  }
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kRegisterFontFiles)) {
    command_line->AppendSwitchASCII(
        switches::kRegisterFontFiles,
        base::CommandLine::ForCurrentProcess()->GetSwitchValueASCII(
            switches::kRegisterFontFiles));
  }
}

void ShellContentBrowserClient::ResourceDispatcherHostCreated() {
  resource_dispatcher_host_delegate_.reset(
      new ShellResourceDispatcherHostDelegate);
  ResourceDispatcherHost::Get()->SetDelegate(
      resource_dispatcher_host_delegate_.get());
}

std::string ShellContentBrowserClient::GetDefaultDownloadName() {
  return "download";
}

WebContentsViewDelegate* ShellContentBrowserClient::GetWebContentsViewDelegate(
    WebContents* web_contents) {
  return CreateShellWebContentsViewDelegate(web_contents);
}

QuotaPermissionContext*
ShellContentBrowserClient::CreateQuotaPermissionContext() {
  return new ShellQuotaPermissionContext();
}

void ShellContentBrowserClient::SelectClientCertificate(
    WebContents* web_contents,
    net::SSLCertRequestInfo* cert_request_info,
    std::unique_ptr<ClientCertificateDelegate> delegate) {
  if (!select_client_certificate_callback_.is_null())
    select_client_certificate_callback_.Run();
}

SpeechRecognitionManagerDelegate*
    ShellContentBrowserClient::CreateSpeechRecognitionManagerDelegate() {
  return new ShellSpeechRecognitionManagerDelegate();
}

net::NetLog* ShellContentBrowserClient::GetNetLog() {
  return shell_browser_main_parts_->net_log();
}

bool ShellContentBrowserClient::ShouldSwapProcessesForRedirect(
    ResourceContext* resource_context,
    const GURL& current_url,
    const GURL& new_url) {
  return g_swap_processes_for_redirect;
}

DevToolsManagerDelegate*
ShellContentBrowserClient::GetDevToolsManagerDelegate() {
  return new ShellDevToolsManagerDelegate(browser_context());
}

void ShellContentBrowserClient::OpenURL(
    BrowserContext* browser_context,
    const OpenURLParams& params,
    const base::Callback<void(WebContents*)>& callback) {
  callback.Run(Shell::CreateNewWindow(browser_context,
                                      params.url,
                                      nullptr,
                                      gfx::Size())->web_contents());
}

#if defined(OS_ANDROID)
void ShellContentBrowserClient::GetAdditionalMappedFilesForChildProcess(
    const base::CommandLine& command_line,
    int child_process_id,
    content::FileDescriptorInfo* mappings,
    std::map<int, base::MemoryMappedFile::Region>* regions) {
  // mappings->Share(
  //     kShellPakDescriptor,
  //     base::GlobalDescriptors::GetInstance()->Get(kShellPakDescriptor));
  // regions->insert(std::make_pair(
  //     kShellPakDescriptor,
  //     base::GlobalDescriptors::GetInstance()->GetRegion(kShellPakDescriptor)));
}
#elif defined(OS_POSIX) && !defined(OS_MACOSX)
void ShellContentBrowserClient::GetAdditionalMappedFilesForChildProcess(
    const base::CommandLine& command_line,
    int child_process_id,
    content::FileDescriptorInfo* mappings) {
}
#endif  // defined(OS_ANDROID)

#if defined(OS_WIN)
bool ShellContentBrowserClient::PreSpawnRenderer(
    sandbox::TargetPolicy* policy) {
  // Add sideloaded font files for testing. See also DIR_WINDOWS_FONTS
  // addition in |StartSandboxedProcess|.
  std::vector<std::string> font_files = switches::GetSideloadFontFiles();
  for (std::vector<std::string>::const_iterator i(font_files.begin());
      i != font_files.end();
      ++i) {
    policy->AddRule(sandbox::TargetPolicy::SUBSYS_FILES,
        sandbox::TargetPolicy::FILES_ALLOW_READONLY,
        base::UTF8ToWide(*i).c_str());
  }
  return true;
}
#endif  // OS_WIN

ShellBrowserContext* ShellContentBrowserClient::browser_context() {
  return shell_browser_main_parts_->browser_context();
}

ShellBrowserContext*
    ShellContentBrowserClient::off_the_record_browser_context() {
  return shell_browser_main_parts_->off_the_record_browser_context();
}

}  // namespace content
