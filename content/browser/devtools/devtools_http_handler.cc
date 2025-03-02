// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread.h"
#include "base/values.h"
#include "build/build_config.h"
#include "content/browser/devtools/devtools_http_handler.h"
#include "content/browser/devtools/devtools_manager.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/devtools_external_agent_proxy_delegate.h"
#include "content/public/browser/devtools_manager_delegate.h"
#include "content/public/browser/devtools_socket_factory.h"
#include "content/public/common/url_constants.h"
#include "content/public/common/user_agent.h"
#include "net/base/escape.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include "net/base/net_errors.h"
#include "net/server/http_server.h"
#include "net/server/http_server_request_info.h"
#include "net/server/http_server_response_info.h"
#include "net/socket/server_socket.h"

namespace content {

namespace {

const base::FilePath::CharType kDevToolsActivePortFileName[] =
    FILE_PATH_LITERAL("DevToolsActivePort");

const char kDevToolsHandlerThreadName[] = "Chrome_DevToolsHandlerThread";

const char kPageUrlPrefix[] = "/devtools/page/";

const char kTargetIdField[] = "id";
const char kTargetParentIdField[] = "parentId";
const char kTargetTypeField[] = "type";
const char kTargetTitleField[] = "title";
const char kTargetDescriptionField[] = "description";
const char kTargetUrlField[] = "url";
const char kTargetFaviconUrlField[] = "faviconUrl";
const char kTargetWebSocketDebuggerUrlField[] = "webSocketDebuggerUrl";
const char kTargetDevtoolsFrontendUrlField[] = "devtoolsFrontendUrl";

// Maximum write buffer size of devtools http/websocket connections.
// TODO(rmcilroy/pfieldman): Reduce this back to 100Mb when we have
// added back pressure on the TraceComplete message protocol - crbug.com/456845.
const int32_t kSendBufferSizeForDevTools = 256 * 1024 * 1024;  // 256Mb

}  // namespace

// ServerWrapper -------------------------------------------------------------
// All methods in this class are only called on handler thread.
class ServerWrapper : net::HttpServer::Delegate {
 public:
  ServerWrapper(base::WeakPtr<DevToolsHttpHandler> handler,
                std::unique_ptr<net::ServerSocket> socket,
                const base::FilePath& frontend_dir,
                bool bundles_resources);

  int GetLocalAddress(net::IPEndPoint* address);

  void AcceptWebSocket(int connection_id,
                       const net::HttpServerRequestInfo& request);
  void SendOverWebSocket(int connection_id, const std::string& message);
  void SendResponse(int connection_id,
                    const net::HttpServerResponseInfo& response);
  void Send200(int connection_id,
               const std::string& data,
               const std::string& mime_type);
  void Send404(int connection_id);
  void Send500(int connection_id, const std::string& message);
  void Close(int connection_id);

  void WriteActivePortToUserProfile(const base::FilePath& output_directory);

  ~ServerWrapper() override {}

 private:
  // net::HttpServer::Delegate implementation.
  void OnConnect(int connection_id) override {}
  void OnHttpRequest(int connection_id,
                     const net::HttpServerRequestInfo& info) override;
  void OnWebSocketRequest(int connection_id,
                          const net::HttpServerRequestInfo& info) override;
  void OnWebSocketMessage(int connection_id,
                          const std::string& data) override;
  void OnClose(int connection_id) override;

  base::WeakPtr<DevToolsHttpHandler> handler_;
  std::unique_ptr<net::HttpServer> server_;
  base::FilePath frontend_dir_;
  bool bundles_resources_;
};

ServerWrapper::ServerWrapper(base::WeakPtr<DevToolsHttpHandler> handler,
                             std::unique_ptr<net::ServerSocket> socket,
                             const base::FilePath& frontend_dir,
                             bool bundles_resources)
    : handler_(handler),
      server_(new net::HttpServer(std::move(socket), this)),
      frontend_dir_(frontend_dir),
      bundles_resources_(bundles_resources) {}

int ServerWrapper::GetLocalAddress(net::IPEndPoint* address) {
  return server_->GetLocalAddress(address);
}

void ServerWrapper::AcceptWebSocket(int connection_id,
                                    const net::HttpServerRequestInfo& request) {
  server_->SetSendBufferSize(connection_id, kSendBufferSizeForDevTools);
  server_->AcceptWebSocket(connection_id, request);
}

void ServerWrapper::SendOverWebSocket(int connection_id,
                                      const std::string& message) {
  server_->SendOverWebSocket(connection_id, message);
}

void ServerWrapper::SendResponse(int connection_id,
                                 const net::HttpServerResponseInfo& response) {
  server_->SendResponse(connection_id, response);
}

void ServerWrapper::Send200(int connection_id,
                            const std::string& data,
                            const std::string& mime_type) {
  server_->Send200(connection_id, data, mime_type);
}

void ServerWrapper::Send404(int connection_id) {
  server_->Send404(connection_id);
}

void ServerWrapper::Send500(int connection_id,
                            const std::string& message) {
  server_->Send500(connection_id, message);
}

void ServerWrapper::Close(int connection_id) {
  server_->Close(connection_id);
}

// Thread and ServerWrapper lifetime management ------------------------------

void TerminateOnUI(base::Thread* thread,
                   ServerWrapper* server_wrapper,
                   DevToolsSocketFactory* socket_factory) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (server_wrapper) {
    DCHECK(thread);
    thread->task_runner()->DeleteSoon(FROM_HERE, server_wrapper);
  }
  if (socket_factory) {
    DCHECK(thread);
    thread->task_runner()->DeleteSoon(FROM_HERE, socket_factory);
  }
  if (thread) {
    BrowserThread::DeleteSoon(BrowserThread::FILE, FROM_HERE, thread);
  }
}

void ServerStartedOnUI(base::WeakPtr<DevToolsHttpHandler> handler,
                       base::Thread* thread,
                       ServerWrapper* server_wrapper,
                       DevToolsSocketFactory* socket_factory,
                       std::unique_ptr<net::IPEndPoint> ip_address) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  if (handler && thread && server_wrapper) {
    handler->ServerStarted(thread, server_wrapper, socket_factory,
                           std::move(ip_address));
  } else {
    TerminateOnUI(thread, server_wrapper, socket_factory);
  }
}

void StartServerOnHandlerThread(
    base::WeakPtr<DevToolsHttpHandler> handler,
    base::Thread* thread,
    DevToolsSocketFactory* socket_factory,
    const base::FilePath& output_directory,
    const base::FilePath& frontend_dir,
    bool bundles_resources) {
  DCHECK(thread->task_runner()->BelongsToCurrentThread());
  ServerWrapper* server_wrapper = nullptr;
  std::unique_ptr<net::ServerSocket> server_socket =
      socket_factory->CreateForHttpServer();
  std::unique_ptr<net::IPEndPoint> ip_address(new net::IPEndPoint);
  if (server_socket) {
    server_wrapper = new ServerWrapper(handler, std::move(server_socket),
                                       frontend_dir, bundles_resources);
    if (!output_directory.empty())
      server_wrapper->WriteActivePortToUserProfile(output_directory);

    if (server_wrapper->GetLocalAddress(ip_address.get()) != net::OK)
      ip_address.reset();
  } else {
    ip_address.reset();
    LOG(ERROR) << "Cannot start http server for devtools. Stop devtools.";
  }
  BrowserThread::PostTask(BrowserThread::UI, FROM_HERE,
      base::Bind(&ServerStartedOnUI,
                 handler,
                 thread,
                 server_wrapper,
                 socket_factory,
                 base::Passed(&ip_address)));
}

void StartServerOnFile(
    base::WeakPtr<DevToolsHttpHandler> handler,
    DevToolsSocketFactory* socket_factory,
    const base::FilePath& output_directory,
    const base::FilePath& frontend_dir,
    bool bundles_resources) {
  DCHECK_CURRENTLY_ON(BrowserThread::FILE);
  std::unique_ptr<base::Thread> thread(
      new base::Thread(kDevToolsHandlerThreadName));
  base::Thread::Options options;
  options.message_loop_type = base::MessageLoop::TYPE_IO;
  if (thread->StartWithOptions(options)) {
    base::MessageLoop* message_loop = thread->message_loop();
    message_loop->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&StartServerOnHandlerThread, handler,
                   base::Unretained(thread.release()), socket_factory,
                   output_directory, frontend_dir, bundles_resources));
  }
}

// DevToolsAgentHostClientImpl -----------------------------------------------
// An internal implementation of DevToolsAgentHostClient that delegates
// messages sent to a DebuggerShell instance.
class DevToolsAgentHostClientImpl : public DevToolsAgentHostClient {
 public:
  DevToolsAgentHostClientImpl(base::MessageLoop* message_loop,
                              ServerWrapper* server_wrapper,
                              int connection_id,
                              scoped_refptr<DevToolsAgentHost> agent_host)
      : message_loop_(message_loop),
        server_wrapper_(server_wrapper),
        connection_id_(connection_id),
        agent_host_(agent_host) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    agent_host_->AttachClient(this);
  }

  ~DevToolsAgentHostClientImpl() override {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    if (agent_host_.get())
      agent_host_->DetachClient(this);
  }

  void AgentHostClosed(DevToolsAgentHost* agent_host,
                       bool replaced_with_another_client) override {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    DCHECK(agent_host == agent_host_.get());

    std::string message = base::StringPrintf(
        "{ \"method\": \"Inspector.detached\", "
        "\"params\": { \"reason\": \"%s\"} }",
        replaced_with_another_client ?
            "replaced_with_devtools" : "target_closed");
    DispatchProtocolMessage(agent_host, message);

    agent_host_ = nullptr;
    message_loop_->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&ServerWrapper::Close, base::Unretained(server_wrapper_),
                   connection_id_));
  }

  void DispatchProtocolMessage(DevToolsAgentHost* agent_host,
                               const std::string& message) override {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    DCHECK(agent_host == agent_host_.get());
    message_loop_->task_runner()->PostTask(
        FROM_HERE,
        base::Bind(&ServerWrapper::SendOverWebSocket,
                   base::Unretained(server_wrapper_), connection_id_, message));
  }

  void OnMessage(const std::string& message) {
    DCHECK_CURRENTLY_ON(BrowserThread::UI);
    if (agent_host_.get())
      agent_host_->DispatchProtocolMessage(this, message);
  }

 private:
  base::MessageLoop* const message_loop_;
  ServerWrapper* const server_wrapper_;
  const int connection_id_;
  scoped_refptr<DevToolsAgentHost> agent_host_;
};

static bool TimeComparator(scoped_refptr<DevToolsAgentHost> host1,
                           scoped_refptr<DevToolsAgentHost> host2) {
  return host1->GetLastActivityTime() > host2->GetLastActivityTime();
}

// DevToolsHttpHandler -------------------------------------------------------

DevToolsHttpHandler::~DevToolsHttpHandler() {
  TerminateOnUI(thread_, server_wrapper_, socket_factory_);
}

static std::string PathWithoutParams(const std::string& path) {
  size_t query_position = path.find("?");
  if (query_position != std::string::npos)
    return path.substr(0, query_position);
  return path;
}

static std::string GetMimeType(const std::string& filename) {
  if (base::EndsWith(filename, ".html", base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/html";
  } else if (base::EndsWith(filename, ".css",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "text/css";
  } else if (base::EndsWith(filename, ".js",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "application/javascript";
  } else if (base::EndsWith(filename, ".png",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/png";
  } else if (base::EndsWith(filename, ".gif",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/gif";
  } else if (base::EndsWith(filename, ".json",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "application/json";
  } else if (base::EndsWith(filename, ".svg",
                            base::CompareCase::INSENSITIVE_ASCII)) {
    return "image/svg+xml";
  }
  LOG(ERROR) << "GetMimeType doesn't know mime type for: "
             << filename
             << " text/plain will be returned";
  NOTREACHED();
  return "text/plain";
}

void ServerWrapper::OnHttpRequest(int connection_id,
                                  const net::HttpServerRequestInfo& info) {
  server_->SetSendBufferSize(connection_id, kSendBufferSizeForDevTools);

  if (base::StartsWith(info.path, "/json", base::CompareCase::SENSITIVE)) {
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&DevToolsHttpHandler::OnJsonRequest,
                   handler_,
                   connection_id,
                   info));
    return;
  }

  if (info.path.empty() || info.path == "/") {
    // Discovery page request.
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&DevToolsHttpHandler::OnDiscoveryPageRequest,
                   handler_,
                   connection_id));
    return;
  }

  if (!base::StartsWith(info.path, "/devtools/",
                        base::CompareCase::SENSITIVE)) {
    server_->Send404(connection_id);
    return;
  }

  std::string filename = PathWithoutParams(info.path.substr(10));
  std::string mime_type = GetMimeType(filename);

  if (!frontend_dir_.empty()) {
    base::FilePath path = frontend_dir_.AppendASCII(filename);
    std::string data;
    base::ReadFileToString(path, &data);
    server_->Send200(connection_id, data, mime_type);
    return;
  }

  if (bundles_resources_) {
    BrowserThread::PostTask(
        BrowserThread::UI,
        FROM_HERE,
        base::Bind(&DevToolsHttpHandler::OnFrontendResourceRequest,
                   handler_,
                   connection_id,
                   filename));
    return;
  }
  server_->Send404(connection_id);
}

void ServerWrapper::OnWebSocketRequest(
    int connection_id,
    const net::HttpServerRequestInfo& request) {
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &DevToolsHttpHandler::OnWebSocketRequest,
          handler_,
          connection_id,
          request));
}

void ServerWrapper::OnWebSocketMessage(int connection_id,
                                       const std::string& data) {
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &DevToolsHttpHandler::OnWebSocketMessage,
          handler_,
          connection_id,
          data));
}

void ServerWrapper::OnClose(int connection_id) {
  BrowserThread::PostTask(
      BrowserThread::UI,
      FROM_HERE,
      base::Bind(
          &DevToolsHttpHandler::OnClose,
          handler_,
          connection_id));
}

std::string DevToolsHttpHandler::GetFrontendURLInternal(
    const std::string& id,
    const std::string& host) {
  return base::StringPrintf(
      "%s%sws=%s%s%s",
      frontend_url_.c_str(),
      frontend_url_.find("?") == std::string::npos ? "?" : "&",
      host.c_str(),
      kPageUrlPrefix,
      id.c_str());
}

static bool ParseJsonPath(
    const std::string& path,
    std::string* command,
    std::string* target_id) {

  // Fall back to list in case of empty query.
  if (path.empty()) {
    *command = "list";
    return true;
  }

  if (!base::StartsWith(path, "/", base::CompareCase::SENSITIVE)) {
    // Malformed command.
    return false;
  }
  *command = path.substr(1);

  size_t separator_pos = command->find("/");
  if (separator_pos != std::string::npos) {
    *target_id = command->substr(separator_pos + 1);
    *command = command->substr(0, separator_pos);
  }
  return true;
}

void DevToolsHttpHandler::OnJsonRequest(
    int connection_id,
    const net::HttpServerRequestInfo& info) {
  // Trim /json
  std::string path = info.path.substr(5);

  // Trim fragment and query
  std::string query;
  size_t query_pos = path.find("?");
  if (query_pos != std::string::npos) {
    query = path.substr(query_pos + 1);
    path = path.substr(0, query_pos);
  }

  size_t fragment_pos = path.find("#");
  if (fragment_pos != std::string::npos)
    path = path.substr(0, fragment_pos);

  std::string command;
  std::string target_id;
  if (!ParseJsonPath(path, &command, &target_id)) {
    SendJson(connection_id,
             net::HTTP_NOT_FOUND,
             NULL,
             "Malformed query: " + info.path);
    return;
  }

  if (command == "version") {
    base::DictionaryValue version;
    version.SetString("Protocol-Version",
        DevToolsAgentHost::GetProtocolVersion().c_str());
    version.SetString("WebKit-Version", GetWebKitVersion());
    version.SetString("Browser", product_name_);
    version.SetString("User-Agent", user_agent_);
    SendJson(connection_id, net::HTTP_OK, &version, std::string());
    return;
  }

  if (command == "list") {
    DevToolsAgentHost::DiscoverAllHosts(
        base::Bind(&DevToolsHttpHandler::RespondToJsonList,
                   weak_factory_.GetWeakPtr(), connection_id,
                   info.headers["host"]));
    return;
  }

  if (command == "new") {
    GURL url(net::UnescapeURLComponent(
        query, net::UnescapeRule::URL_SPECIAL_CHARS_EXCEPT_PATH_SEPARATORS |
                   net::UnescapeRule::PATH_SEPARATORS));
    if (!url.is_valid())
      url = GURL(url::kAboutBlankURL);
    scoped_refptr<DevToolsAgentHost> agent_host = nullptr;
    agent_host = delegate_->CreateNewTarget(url);
    if (!agent_host) {
      SendJson(connection_id,
               net::HTTP_INTERNAL_SERVER_ERROR,
               NULL,
               "Could not create new page");
      return;
    }
    std::string host = info.headers["host"];
    std::unique_ptr<base::DictionaryValue> dictionary(
        SerializeDescriptor(agent_host, host));
    SendJson(connection_id, net::HTTP_OK, dictionary.get(), std::string());
    const std::string target_id = agent_host->GetId();
    agent_host_map_[target_id] = agent_host;
    return;
  }

  if (command == "activate" || command == "close") {
    scoped_refptr<DevToolsAgentHost> agent_host = GetAgentHost(target_id);
    if (!agent_host) {
      SendJson(connection_id,
               net::HTTP_NOT_FOUND,
               NULL,
               "No such target id: " + target_id);
      return;
    }

    if (command == "activate") {
      if (agent_host->Activate()) {
        SendJson(connection_id, net::HTTP_OK, NULL, "Target activated");
      } else {
        SendJson(connection_id,
                 net::HTTP_INTERNAL_SERVER_ERROR,
                 NULL,
                 "Could not activate target id: " + target_id);
      }
      return;
    }

    if (command == "close") {
      if (agent_host->Close()) {
        SendJson(connection_id, net::HTTP_OK, NULL, "Target is closing");
      } else {
        SendJson(connection_id,
                 net::HTTP_INTERNAL_SERVER_ERROR,
                 NULL,
                 "Could not close target id: " + target_id);
      }
      return;
    }
  }
  SendJson(connection_id,
           net::HTTP_NOT_FOUND,
           NULL,
           "Unknown command: " + command);
  return;
}

void DevToolsHttpHandler::RespondToJsonList(
    int connection_id,
    const std::string& host,
    DevToolsAgentHost::List hosts) {
  DevToolsAgentHost::List agent_hosts = std::move(hosts);
  std::sort(agent_hosts.begin(), agent_hosts.end(), TimeComparator);
  agent_host_map_.clear();
  base::ListValue list_value;
  for (auto& agent_host : agent_hosts) {
    agent_host_map_[agent_host->GetId()] = agent_host;
    list_value.Append(SerializeDescriptor(agent_host, host));
  }
  SendJson(connection_id, net::HTTP_OK, &list_value, std::string());
}

scoped_refptr<DevToolsAgentHost> DevToolsHttpHandler::GetAgentHost(
    const std::string& target_id) {
  DescriptorMap::const_iterator it = agent_host_map_.find(target_id);
  return it != agent_host_map_.end() ? it->second : nullptr;
}

void DevToolsHttpHandler::OnDiscoveryPageRequest(int connection_id) {
  std::string response = delegate_->GetDiscoveryPageHTML();
  Send200(connection_id, response, "text/html; charset=UTF-8");
}

void DevToolsHttpHandler::OnFrontendResourceRequest(
    int connection_id, const std::string& path) {
  Send200(connection_id,
          delegate_->GetFrontendResource(path),
          GetMimeType(path));
}

void DevToolsHttpHandler::OnWebSocketRequest(
    int connection_id,
    const net::HttpServerRequestInfo& request) {
  if (!thread_)
    return;

  std::string browser_prefix = "/devtools/browser";
  if (base::StartsWith(request.path, browser_prefix,
                       base::CompareCase::SENSITIVE)) {
    scoped_refptr<DevToolsAgentHost> browser_agent =
        DevToolsAgentHost::CreateForBrowser(
            thread_->task_runner(),
            base::Bind(&DevToolsSocketFactory::CreateForTethering,
                       base::Unretained(socket_factory_)));
    connection_to_client_[connection_id].reset(new DevToolsAgentHostClientImpl(
        thread_->message_loop(), server_wrapper_, connection_id,
        browser_agent));
    AcceptWebSocket(connection_id, request);
    return;
  }

  if (!base::StartsWith(request.path, kPageUrlPrefix,
                        base::CompareCase::SENSITIVE)) {
    Send404(connection_id);
    return;
  }

  std::string target_id = request.path.substr(strlen(kPageUrlPrefix));
  scoped_refptr<DevToolsAgentHost> agent = GetAgentHost(target_id);
  if (!agent) {
    Send500(connection_id, "No such target id: " + target_id);
    return;
  }

  if (agent->IsAttached()) {
    Send500(connection_id,
            "Target with given id is being inspected: " + target_id);
    return;
  }

  connection_to_client_[connection_id].reset(new DevToolsAgentHostClientImpl(
      thread_->message_loop(), server_wrapper_, connection_id, agent));

  AcceptWebSocket(connection_id, request);
}

void DevToolsHttpHandler::OnWebSocketMessage(
    int connection_id,
    const std::string& data) {
  ConnectionToClientMap::iterator it =
      connection_to_client_.find(connection_id);
  if (it != connection_to_client_.end())
    it->second->OnMessage(data);
}

void DevToolsHttpHandler::OnClose(int connection_id) {
  connection_to_client_.erase(connection_id);
}

DevToolsHttpHandler::DevToolsHttpHandler(
    DevToolsManagerDelegate* delegate,
    std::unique_ptr<DevToolsSocketFactory> socket_factory,
    const std::string& frontend_url,
    const base::FilePath& output_directory,
    const base::FilePath& debug_frontend_dir,
    const std::string& product_name,
    const std::string& user_agent)
    : thread_(nullptr),
      frontend_url_(frontend_url),
      product_name_(product_name),
      user_agent_(user_agent),
      server_wrapper_(nullptr),
      delegate_(delegate),
      socket_factory_(nullptr),
      weak_factory_(this) {
  bool bundles_resources = frontend_url_.empty();
  if (frontend_url_.empty())
    frontend_url_ = "/devtools/inspector.html";

  BrowserThread::PostTask(
      BrowserThread::FILE, FROM_HERE,
      base::Bind(&StartServerOnFile,
                 weak_factory_.GetWeakPtr(),
                 socket_factory.release(),
                 output_directory,
                 debug_frontend_dir,
                 bundles_resources));
}

void DevToolsHttpHandler::ServerStarted(
    base::Thread* thread,
    ServerWrapper* server_wrapper,
    DevToolsSocketFactory* socket_factory,
    std::unique_ptr<net::IPEndPoint> ip_address) {
  thread_ = thread;
  server_wrapper_ = server_wrapper;
  socket_factory_ = socket_factory;
  server_ip_address_.swap(ip_address);
}

void ServerWrapper::WriteActivePortToUserProfile(
    const base::FilePath& output_directory) {
  DCHECK(!output_directory.empty());
  net::IPEndPoint endpoint;
  int err;
  if ((err = server_->GetLocalAddress(&endpoint)) != net::OK) {
    LOG(ERROR) << "Error " << err << " getting local address";
    return;
  }

  // Write this port to a well-known file in the profile directory
  // so Telemetry can pick it up.
  base::FilePath path = output_directory.Append(kDevToolsActivePortFileName);
  std::string port_string = base::UintToString(endpoint.port());
  if (base::WriteFile(path, port_string.c_str(),
                      static_cast<int>(port_string.length())) < 0) {
    LOG(ERROR) << "Error writing DevTools active port to file";
  }
}

void DevToolsHttpHandler::SendJson(int connection_id,
                                   net::HttpStatusCode status_code,
                                   base::Value* value,
                                   const std::string& message) {
  if (!thread_)
    return;

  // Serialize value and message.
  std::string json_value;
  if (value) {
    base::JSONWriter::WriteWithOptions(
        *value, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json_value);
  }
  std::string json_message;
  base::JSONWriter::Write(base::StringValue(message), &json_message);

  net::HttpServerResponseInfo response(status_code);
  response.SetBody(json_value + message, "application/json; charset=UTF-8");

  thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ServerWrapper::SendResponse,
                 base::Unretained(server_wrapper_), connection_id, response));
}

void DevToolsHttpHandler::Send200(int connection_id,
                                  const std::string& data,
                                  const std::string& mime_type) {
  if (!thread_)
    return;
  thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ServerWrapper::Send200, base::Unretained(server_wrapper_),
                 connection_id, data, mime_type));
}

void DevToolsHttpHandler::Send404(int connection_id) {
  if (!thread_)
    return;
  thread_->task_runner()->PostTask(
      FROM_HERE, base::Bind(&ServerWrapper::Send404,
                            base::Unretained(server_wrapper_), connection_id));
}

void DevToolsHttpHandler::Send500(int connection_id,
                                  const std::string& message) {
  if (!thread_)
    return;
  thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ServerWrapper::Send500, base::Unretained(server_wrapper_),
                 connection_id, message));
}

void DevToolsHttpHandler::AcceptWebSocket(
    int connection_id,
    const net::HttpServerRequestInfo& request) {
  if (!thread_)
    return;
  thread_->task_runner()->PostTask(
      FROM_HERE,
      base::Bind(&ServerWrapper::AcceptWebSocket,
                 base::Unretained(server_wrapper_), connection_id, request));
}

std::unique_ptr<base::DictionaryValue> DevToolsHttpHandler::SerializeDescriptor(
    scoped_refptr<DevToolsAgentHost> agent_host,
    const std::string& host) {
  std::unique_ptr<base::DictionaryValue> dictionary(new base::DictionaryValue);
  std::string id = agent_host->GetId();
  dictionary->SetString(kTargetIdField, id);
  std::string parent_id = agent_host->GetParentId();
  if (!parent_id.empty())
    dictionary->SetString(kTargetParentIdField, parent_id);
  dictionary->SetString(kTargetTypeField, agent_host->GetType());
  dictionary->SetString(kTargetTitleField,
                        net::EscapeForHTML(agent_host->GetTitle()));
  dictionary->SetString(kTargetDescriptionField, agent_host->GetDescription());

  GURL url = agent_host->GetURL();
  dictionary->SetString(kTargetUrlField, url.spec());

  GURL favicon_url = agent_host->GetFaviconURL();
  if (favicon_url.is_valid())
    dictionary->SetString(kTargetFaviconUrlField, favicon_url.spec());

  if (!agent_host->IsAttached()) {
    dictionary->SetString(kTargetWebSocketDebuggerUrlField,
                          base::StringPrintf("ws://%s%s%s",
                                             host.c_str(),
                                             kPageUrlPrefix,
                                             id.c_str()));
    std::string devtools_frontend_url = GetFrontendURLInternal(
        id.c_str(),
        host);
    dictionary->SetString(
        kTargetDevtoolsFrontendUrlField, devtools_frontend_url);
  }

  return dictionary;
}

}  // namespace content
