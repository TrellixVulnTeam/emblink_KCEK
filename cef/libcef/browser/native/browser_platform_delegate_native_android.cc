// Copyright 2015 The Chromium Embedded Framework Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libcef/browser/native/browser_platform_delegate_native_android.h"

// Include this first to avoid type conflict errors.
#include "base/tracked_objects.h"
#undef Status

#include <sys/sysinfo.h>

#include "libcef/browser/browser_host_impl.h"
#include "libcef/browser/context.h"
#include "libcef/browser/native/menu_runner_linux.h"
#include "libcef/browser/native/window_delegate_view.h"
#include "libcef/browser/thread_util.h"

#include "content/browser/renderer_host/render_widget_host_impl.h"
#include "content/public/browser/native_web_keyboard_event.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/common/renderer_preferences.h"
#include "ui/aura/window.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "ui/events/keycodes/keyboard_code_conversion_android.h"
#include "ui/events/keycodes/keysym_to_unicode.h"
#include "ui/gfx/font_render_params.h"
#include "ui/views/widget/widget.h"

namespace {

// Returns the number of seconds since system boot.
long GetSystemUptime() {
  struct sysinfo info;
  if (sysinfo(&info) == 0)
    return info.uptime;
  return 0;
}

}  // namespace

CefBrowserPlatformDelegateNativeAndroid::CefBrowserPlatformDelegateNativeAndroid(
    const CefWindowInfo& window_info)
    : CefBrowserPlatformDelegateNative(window_info),
      host_window_created_(false),
      window_widget_(nullptr) {
}

void CefBrowserPlatformDelegateNativeAndroid::BrowserDestroyed(
    CefBrowserHostImpl* browser) {
  CefBrowserPlatformDelegate::BrowserDestroyed(browser);

  if (host_window_created_) {
    // Release the reference added in CreateHostWindow().
    browser->Release();
  }
}

bool CefBrowserPlatformDelegateNativeAndroid::CreateHostWindow() {
  DCHECK(!window_widget_);

  if (window_info_.width == 0)
    window_info_.width = 800;
  if (window_info_.height == 0)
    window_info_.height = 600;

  gfx::Rect rect(window_info_.x, window_info_.y,
                 window_info_.width, window_info_.height);

  // Create a new window object. It will delete itself when the associated X11
  // window is destroyed.
  // window_x11_ = new CefWindowX11(browser_, window_info_.parent_window, rect);
  window_info_.window = NULL; // window_x11_->xwindow();

  host_window_created_ = true;

  // Add a reference that will be released in BrowserDestroyed().
  browser_->AddRef();

  SkColor background_color = SK_ColorWHITE;
  const CefSettings& settings = CefContext::Get()->settings();
  if (CefColorGetA(settings.background_color) > 0) {
    background_color = SkColorSetRGB(
        CefColorGetR(settings.background_color),
        CefColorGetG(settings.background_color),
        CefColorGetB(settings.background_color));
  }

  CefWindowDelegateView* delegate_view =
      new CefWindowDelegateView(background_color);
  delegate_view->Init(window_info_.window,
                      browser_->web_contents(),
                      gfx::Rect(gfx::Point(), rect.size()));

  window_widget_ = delegate_view->GetWidget();
  window_widget_->Show();

  // window_x11_->Show();

  // As an additional requirement on Linux, we must set the colors for the
  // render widgets in webkit.
  content::RendererPreferences* prefs =
      browser_->web_contents()->GetMutableRendererPrefs();
  prefs->focus_ring_color = SkColorSetARGB(255, 229, 151, 0);
  prefs->thumb_active_color = SkColorSetRGB(244, 244, 244);
  prefs->thumb_inactive_color = SkColorSetRGB(234, 234, 234);
  prefs->track_color = SkColorSetRGB(211, 211, 211);

  prefs->active_selection_bg_color = SkColorSetRGB(30, 144, 255);
  prefs->active_selection_fg_color = SK_ColorWHITE;
  prefs->inactive_selection_bg_color = SkColorSetRGB(200, 200, 200);
  prefs->inactive_selection_fg_color = SkColorSetRGB(50, 50, 50);

  // Set font-related attributes.
  CR_DEFINE_STATIC_LOCAL(const gfx::FontRenderParams, params,
      (gfx::GetFontRenderParams(gfx::FontRenderParamsQuery(), NULL)));
  prefs->should_antialias_text = params.antialiasing;
  prefs->use_subpixel_positioning = params.subpixel_positioning;
  prefs->hinting = params.hinting;
  prefs->use_autohinter = params.autohinter;
  prefs->use_bitmaps = params.use_bitmaps;
  prefs->subpixel_rendering = params.subpixel_rendering;

  browser_->web_contents()->GetRenderViewHost()->SyncRendererPrefs();

  return true;
}

void CefBrowserPlatformDelegateNativeAndroid::CloseHostWindow() {
  // if (window_x11_)
  //   window_x11_->Close();
}

CefWindowHandle
    CefBrowserPlatformDelegateNativeAndroid::GetHostWindowHandle() const {
  if (windowless_handler_)
    return windowless_handler_->GetParentWindowHandle();
  return window_info_.window;
}

views::Widget* CefBrowserPlatformDelegateNativeAndroid::GetWindowWidget() const {
  return window_widget_;
}

void CefBrowserPlatformDelegateNativeAndroid::SendFocusEvent(bool setFocus) {
  if (!setFocus)
    return;

  if (browser_->web_contents()) {
    // Give logical focus to the RenderWidgetHostViewAura in the views
    // hierarchy. This does not change the native keyboard focus.
    browser_->web_contents()->Focus();
  }

  // if (window_x11_) {
  //   // Give native focus to the DesktopNativeWidgetAura for the root window.
  //   // Needs to be done via the ::Window so that keyboard focus is assigned
  //   // correctly.
  //   window_x11_->Focus();
  // }
}

void CefBrowserPlatformDelegateNativeAndroid::NotifyMoveOrResizeStarted() {
  // Call the parent method to dismiss any existing popups.
  CefBrowserPlatformDelegate::NotifyMoveOrResizeStarted();

  // if (!window_x11_)
  //   return;

  // views::DesktopWindowTreeHostX11* tree_host = window_x11_->GetHost();
  // if (!tree_host)
  //   return;

  // // Explicitly set the screen bounds so that WindowTreeHost::*Screen()
  // // methods return the correct results.
  // const gfx::Rect& bounds = window_x11_->GetBoundsInScreen();
  // tree_host->set_screen_bounds(bounds);

  // Send updated screen rectangle information to the renderer process so that
  // popups are displayed in the correct location.
  content::RenderWidgetHostImpl::From(
      browser_->web_contents()->GetRenderViewHost()->GetWidget())->
          SendScreenRects();
}

void CefBrowserPlatformDelegateNativeAndroid::SizeTo(int width, int height) {
  // if (window_x11_) {
  //   window_x11_->SetBounds(
  //       gfx::Rect(window_x11_->bounds().origin(), gfx::Size(width, height)));
  // }
}

gfx::Point CefBrowserPlatformDelegateNativeAndroid::GetScreenPoint(
    const gfx::Point& view) const {
  if (windowless_handler_)
    return windowless_handler_->GetParentScreenPoint(view);

  if (!window_widget_)
    return view;

  aura::Window* window = window_widget_->GetNativeView();
  const gfx::Rect& bounds_in_screen = window->GetBoundsInScreen();
  const gfx::Point& screen_point = gfx::Point(bounds_in_screen.x() + view.x(),
                                              bounds_in_screen.y() + view.y());

  // Adjust for potential display scaling.
  float scale = display::Screen::GetScreen()->
      GetDisplayNearestPoint(screen_point).device_scale_factor();
  return gfx::ToFlooredPoint(
      gfx::ScalePoint(gfx::PointF(screen_point), scale));
}

void CefBrowserPlatformDelegateNativeAndroid::ViewText(const std::string& text) {
}

void CefBrowserPlatformDelegateNativeAndroid::HandleKeyboardEvent(
    const content::NativeWebKeyboardEvent& event) {
  // TODO(cef): Is something required here to handle shortcut keys?
}

void CefBrowserPlatformDelegateNativeAndroid::HandleExternalProtocol(
    const GURL& url) {
}

void CefBrowserPlatformDelegateNativeAndroid::TranslateKeyEvent(
    content::NativeWebKeyboardEvent& result,
    const CefKeyEvent& key_event) const {
  result.timeStampSeconds = GetSystemUptime();

  result.windowsKeyCode = key_event.windows_key_code;
  result.nativeKeyCode = key_event.native_key_code;
  result.isSystemKey = key_event.is_system_key ? 1 : 0;
  switch (key_event.type) {
  case KEYEVENT_RAWKEYDOWN:
  case KEYEVENT_KEYDOWN:
    result.type = blink::WebInputEvent::RawKeyDown;
    break;
  case KEYEVENT_KEYUP:
    result.type = blink::WebInputEvent::KeyUp;
    break;
  case KEYEVENT_CHAR:
    result.type = blink::WebInputEvent::Char;
    break;
  default:
    NOTREACHED();
  }

  // Populate DOM values that will be passed to JavaScript handlers via
  // KeyboardEvent.
  result.domCode =
      static_cast<int>(ui::KeycodeConverter::NativeKeycodeToDomCode(
            key_event.native_key_code));
  int keysym = ui::KeyboardCodeFromAndroidKeyCode(
      static_cast<int>(key_event.windows_key_code));
  base::char16 ch = 0; //ui::GetUnicodeCharacterFromXKeySym(keysym);
  result.domKey = static_cast<int>(ui::GetDomKeyFromAndroidEvent(keysym, ch));

  result.text[0] = key_event.character;
  result.unmodifiedText[0] = key_event.unmodified_character;

  result.modifiers |= TranslateModifiers(key_event.modifiers);
}

void CefBrowserPlatformDelegateNativeAndroid::TranslateClickEvent(
    blink::WebMouseEvent& result,
    const CefMouseEvent& mouse_event,
    CefBrowserHost::MouseButtonType type,
    bool mouseUp, int clickCount) const {
  TranslateMouseEvent(result, mouse_event);

  switch (type) {
  case MBT_LEFT:
    result.type = mouseUp ? blink::WebInputEvent::MouseUp :
                            blink::WebInputEvent::MouseDown;
    result.button = blink::WebMouseEvent::Button::Left;
    break;
  case MBT_MIDDLE:
    result.type = mouseUp ? blink::WebInputEvent::MouseUp :
                            blink::WebInputEvent::MouseDown;
    result.button = blink::WebMouseEvent::Button::Middle;
    break;
  case MBT_RIGHT:
    result.type = mouseUp ? blink::WebInputEvent::MouseUp :
                            blink::WebInputEvent::MouseDown;
    result.button = blink::WebMouseEvent::Button::Right;
    break;
  default:
    NOTREACHED();
  }

  result.clickCount = clickCount;
}

void CefBrowserPlatformDelegateNativeAndroid::TranslateMoveEvent(
    blink::WebMouseEvent& result,
    const CefMouseEvent& mouse_event,
    bool mouseLeave) const {
  TranslateMouseEvent(result, mouse_event);

  if (!mouseLeave) {
    result.type = blink::WebInputEvent::MouseMove;
    if (mouse_event.modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON)
      result.button = blink::WebMouseEvent::Button::Left;
    else if (mouse_event.modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON)
      result.button = blink::WebMouseEvent::Button::Middle;
    else if (mouse_event.modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON)
      result.button = blink::WebMouseEvent::Button::Right;
    else
      result.button = blink::WebMouseEvent::Button::NoButton;
  } else {
    result.type = blink::WebInputEvent::MouseLeave;
    result.button = blink::WebMouseEvent::Button::NoButton;
  }

  result.clickCount = 0;
}

void CefBrowserPlatformDelegateNativeAndroid::TranslateWheelEvent(
    blink::WebMouseWheelEvent& result,
    const CefMouseEvent& mouse_event,
    int deltaX, int deltaY) const {
  result = blink::WebMouseWheelEvent();
  TranslateMouseEvent(result, mouse_event);

  result.type = blink::WebInputEvent::MouseWheel;

  static const double scrollbarPixelsPerGtkTick = 40.0;
  result.deltaX = deltaX;
  result.deltaY = deltaY;
  result.wheelTicksX = result.deltaX / scrollbarPixelsPerGtkTick;
  result.wheelTicksY = result.deltaY / scrollbarPixelsPerGtkTick;
  result.hasPreciseScrollingDeltas = true;

  // Unless the phase and momentumPhase are passed in as parameters to this
  // function, there is no way to know them
  result.phase = blink::WebMouseWheelEvent::PhaseNone;
  result.momentumPhase = blink::WebMouseWheelEvent::PhaseNone;

  if (mouse_event.modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON)
    result.button = blink::WebMouseEvent::Button::Left;
  else if (mouse_event.modifiers & EVENTFLAG_MIDDLE_MOUSE_BUTTON)
    result.button = blink::WebMouseEvent::Button::Middle;
  else if (mouse_event.modifiers & EVENTFLAG_RIGHT_MOUSE_BUTTON)
    result.button = blink::WebMouseEvent::Button::Right;
  else
    result.button = blink::WebMouseEvent::Button::NoButton;
}

CefEventHandle CefBrowserPlatformDelegateNativeAndroid::GetEventHandle(
    const content::NativeWebKeyboardEvent& event) const {
  if (!event.os_event)
    return NULL;
  return const_cast<CefEventHandle>(event.os_event->native_event());
}

std::unique_ptr<CefMenuRunner>
    CefBrowserPlatformDelegateNativeAndroid::CreateMenuRunner() {
  // return base::WrapUnique(new CefMenuRunnerLinux);
  return std::unique_ptr<CefMenuRunner>();
}

void CefBrowserPlatformDelegateNativeAndroid::TranslateMouseEvent(
    blink::WebMouseEvent& result,
    const CefMouseEvent& mouse_event) const {
  // position
  result.x = mouse_event.x;
  result.y = mouse_event.y;
  result.windowX = result.x;
  result.windowY = result.y;

  const gfx::Point& screen_pt = GetScreenPoint(gfx::Point(result.x, result.y));
  result.globalX = screen_pt.x();
  result.globalY = screen_pt.y();

  // modifiers
  result.modifiers |= TranslateModifiers(mouse_event.modifiers);

  // timestamp
  result.timeStampSeconds = GetSystemUptime();
}

