// Copyright (c) 2012 The Chromium Embedded Framework Authors.
// Portions copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// MSVC++ requires this to be set before any other includes to get M_PI.
// Otherwise there will be compile errors in wtf/MathExtras.h.
#define _USE_MATH_DEFINES

// Defines required to access Blink internals (unwrap WebNode).
#undef BLINK_IMPLEMENTATION
#define BLINK_IMPLEMENTATION 1
#undef INSIDE_BLINK
#define INSIDE_BLINK 1

#include "libcef/renderer/webkit_glue.h"

#include "base/compiler_specific.h"

MSVC_PUSH_WARNING_LEVEL(0);
#include "third_party/WebKit/public/platform/WebString.h"
#include "third_party/WebKit/public/web/WebDocument.h"
#include "third_party/WebKit/public/web/WebElement.h"
#include "third_party/WebKit/public/web/WebNode.h"
#include "third_party/WebKit/public/web/WebViewClient.h"

#include "third_party/WebKit/Source/bindings/core/v8/ScriptController.h"
#include "third_party/WebKit/Source/bindings/core/v8/ScriptSourceCode.h"
#include "third_party/WebKit/Source/bindings/core/v8/V8Binding.h"
#include "third_party/WebKit/Source/core/dom/Document.h"
#include "third_party/WebKit/Source/core/dom/Element.h"
#include "third_party/WebKit/Source/core/dom/Node.h"
#include "third_party/WebKit/Source/core/editing/serializers/Serialization.h"
#include "third_party/WebKit/Source/core/frame/LocalFrame.h"
#include "third_party/WebKit/Source/core/frame/Settings.h"
#include "third_party/WebKit/Source/web/WebLocalFrameImpl.h"
#include "third_party/WebKit/Source/web/WebViewImpl.h"
MSVC_POP_WARNING();
#undef LOG

#include "base/logging.h"
#include "content/public/renderer/render_frame.h"

namespace webkit_glue {

const int64_t kInvalidFrameId = -1;

bool CanGoBack(blink::WebView* view) {
  if (!view)
    return false;
  blink::WebViewImpl* impl = reinterpret_cast<blink::WebViewImpl*>(view);
  return (impl->client()->historyBackListCount() > 0);
}

bool CanGoForward(blink::WebView* view) {
  if (!view)
    return false;
  blink::WebViewImpl* impl = reinterpret_cast<blink::WebViewImpl*>(view);
  return (impl->client()->historyForwardListCount() > 0);
}

void GoBack(blink::WebView* view) {
  if (!view)
    return;
  blink::WebViewImpl* impl = reinterpret_cast<blink::WebViewImpl*>(view);
  if (impl->client()->historyBackListCount() > 0)
    impl->client()->navigateBackForwardSoon(-1);
}

void GoForward(blink::WebView* view) {
  if (!view)
    return;
  blink::WebViewImpl* impl = reinterpret_cast<blink::WebViewImpl*>(view);
  if (impl->client()->historyForwardListCount() > 0)
    impl->client()->navigateBackForwardSoon(1);
}

std::string DumpDocumentText(blink::WebFrame* frame) {
  // We use the document element's text instead of the body text here because
  // not all documents have a body, such as XML documents.
  blink::WebElement document_element = frame->document().documentElement();
  if (document_element.isNull())
    return std::string();

  blink::Element* web_element = document_element.unwrap<blink::Element>();
  return blink::WebString(web_element->innerText()).utf8();
}

cef_dom_node_type_t GetNodeType(const blink::WebNode& node) {
  const blink::Node* web_node = node.constUnwrap<blink::Node>();
  switch (web_node->getNodeType()) {
    case blink::Node::kElementNode:
      return DOM_NODE_TYPE_ELEMENT;
    case blink::Node::kAttributeNode:
      return DOM_NODE_TYPE_ATTRIBUTE;
    case blink::Node::kTextNode:
      return DOM_NODE_TYPE_TEXT;
    case blink::Node::kCdataSectionNode:
      return DOM_NODE_TYPE_CDATA_SECTION;
    case blink::Node::kProcessingInstructionNode:
      return DOM_NODE_TYPE_PROCESSING_INSTRUCTIONS;
    case blink::Node::kCommentNode:
      return DOM_NODE_TYPE_COMMENT;
    case blink::Node::kDocumentNode:
      return DOM_NODE_TYPE_DOCUMENT;
    case blink::Node::kDocumentTypeNode:
      return DOM_NODE_TYPE_DOCUMENT_TYPE;
    case blink::Node::kDocumentFragmentNode:
      return DOM_NODE_TYPE_DOCUMENT_FRAGMENT;
  }
  return DOM_NODE_TYPE_UNSUPPORTED;
}

blink::WebString GetNodeName(const blink::WebNode& node) {
  const blink::Node* web_node = node.constUnwrap<blink::Node>();
  return web_node->nodeName();
}

blink::WebString CreateNodeMarkup(const blink::WebNode& node) {
  const blink::Node* web_node = node.constUnwrap<blink::Node>();
  return blink::createMarkup(web_node);
}

bool SetNodeValue(blink::WebNode& node, const blink::WebString& value) {
  blink::Node* web_node = node.unwrap<blink::Node>();
  web_node->setNodeValue(value);
  return true;
}

int64_t GetIdentifier(blink::WebFrame* frame) {
  // Each WebFrame will have an associated RenderFrame. The RenderFrame
  // routing IDs are unique within a given renderer process.
  content::RenderFrame* render_frame =
      content::RenderFrame::FromWebFrame(frame);
  DCHECK(render_frame);
  if (render_frame)
    return render_frame->GetRoutingID();
  return kInvalidFrameId;
}

// Based on WebViewImpl::findFrameByName and FrameTree::find.
blink::WebFrame* FindFrameByUniqueName(const blink::WebString& unique_name,
                                       blink::WebFrame* relative_to_frame) {
  blink::Frame* start_frame = toWebLocalFrameImpl(relative_to_frame)->frame();
  if (!start_frame)
    return NULL;

  const AtomicString& atomic_name = unique_name;
  blink::Frame* found_frame = NULL;

  // Search the subtree starting with |start_frame|.
  for (blink::Frame* frame = start_frame;
       frame;
       frame = frame->tree().traverseNext(start_frame)) {
    if (frame->tree().uniqueName() == atomic_name) {
      found_frame = frame;
      break;
    }
  }

  if (found_frame && found_frame->isLocalFrame())
    return blink::WebLocalFrameImpl::fromFrame(toLocalFrame(found_frame));

  return NULL;
}

v8::MaybeLocal<v8::Value> CallV8Function(v8::Local<v8::Context> context,
                                         v8::Local<v8::Function> function,
                                         v8::Local<v8::Object> receiver,
                                         int argc,
                                         v8::Local<v8::Value> args[],
                                         v8::Isolate* isolate) {
  v8::MaybeLocal<v8::Value> func_rv;

  // Execute the function call using the V8ScriptRunner so that inspector
  // instrumentation works.
  blink::LocalFrame* frame =
      toLocalFrame(blink::toFrameIfNotDetached(context));
  DCHECK(frame);
  if (frame &&
      frame->script().canExecuteScripts(blink::AboutToExecuteScript)) {
    func_rv = blink::V8ScriptRunner::callFunction(
        function, frame->document(), receiver, argc, args, isolate);
  }

  return func_rv;
}

bool IsTextControlElement(const blink::WebElement& element) {
  const blink::Element* web_element = element.constUnwrap<blink::Element>();
  return web_element->isTextFormControl();
}

v8::MaybeLocal<v8::Value> ExecuteV8ScriptAndReturnValue(
    const blink::WebString& source,
    const blink::WebString& source_url,
    int start_line,
    v8::Local<v8::Context> context,
    v8::Isolate* isolate,
    v8::TryCatch& tryCatch,
    blink::AccessControlStatus accessControlStatus) {
  // Based on ScriptController::executeScriptAndReturnValue
  DCHECK(isolate);

  if (start_line < 1)
    start_line = 1;

  const blink::KURL kurl = source_url.isEmpty() ?
      blink::KURL() : blink::KURL(blink::ParsedURLString, source_url);

  const blink::ScriptSourceCode ssc = blink::ScriptSourceCode(source, kurl,
      WTF::TextPosition(WTF::OrdinalNumber::fromOneBasedInt(start_line),
      WTF::OrdinalNumber::fromZeroBasedInt(0)));

  v8::MaybeLocal<v8::Value> result;

  blink::LocalFrame* frame =
      toLocalFrame(blink::toFrameIfNotDetached(context));
  DCHECK(frame);

  if (frame) {
    blink::V8CacheOptions v8CacheOptions(blink::V8CacheOptionsDefault);
    if (frame && frame->settings())
      v8CacheOptions = frame->settings()->v8CacheOptions();

    v8::Local<v8::Script> script;
    if (!blink::v8Call(blink::V8ScriptRunner::compileScript(ssc, isolate,
            accessControlStatus, v8CacheOptions), script, tryCatch)) {
      return result;
    }

    result = blink::V8ScriptRunner::runCompiledScript(isolate, script,
        blink::toExecutionContext(context));
  }

  return result;
}

bool IsScriptForbidden() {
  return blink::ScriptForbiddenScope::isScriptForbidden();
}

}  // webkit_glue
