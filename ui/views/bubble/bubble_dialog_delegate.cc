// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/views/bubble/bubble_dialog_delegate.h"

#include "build/build_config.h"
#include "ui/accessibility/ax_view_state.h"
#include "ui/base/material_design/material_design_controller.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/bubble/bubble_frame_view.h"
#include "ui/views/focus/view_storage.h"
#include "ui/views/layout/layout_constants.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"
#include "ui/views/window/dialog_client_view.h"

#if defined(OS_WIN)
#include "ui/base/win/shell.h"
#endif

namespace views {

namespace {

// Create a widget to host the bubble.
Widget* CreateBubbleWidget(BubbleDialogDelegateView* bubble) {
  Widget* bubble_widget = new Widget();
  Widget::InitParams bubble_params(Widget::InitParams::TYPE_BUBBLE);
  bubble_params.delegate = bubble;
  bubble_params.opacity = Widget::InitParams::TRANSLUCENT_WINDOW;
  bubble_params.accept_events = bubble->accept_events();
  if (bubble->parent_window())
    bubble_params.parent = bubble->parent_window();
  else if (bubble->anchor_widget())
    bubble_params.parent = bubble->anchor_widget()->GetNativeView();
  bubble_params.activatable = bubble->CanActivate()
                                  ? Widget::InitParams::ACTIVATABLE_YES
                                  : Widget::InitParams::ACTIVATABLE_NO;
  bubble->OnBeforeBubbleWidgetInit(&bubble_params, bubble_widget);
  bubble_widget->Init(bubble_params);
  if (bubble_params.parent)
    bubble_widget->StackAbove(bubble_params.parent);
  return bubble_widget;
}

}  // namespace

// static
const char BubbleDialogDelegateView::kViewClassName[] =
    "BubbleDialogDelegateView";

BubbleDialogDelegateView::~BubbleDialogDelegateView() {
  if (GetWidget())
    GetWidget()->RemoveObserver(this);
  SetLayoutManager(NULL);
  SetAnchorView(NULL);
}

// static
Widget* BubbleDialogDelegateView::CreateBubble(
    BubbleDialogDelegateView* bubble_delegate) {
  bubble_delegate->Init();
  // Get the latest anchor widget from the anchor view at bubble creation time.
  bubble_delegate->SetAnchorView(bubble_delegate->GetAnchorView());
  Widget* bubble_widget = CreateBubbleWidget(bubble_delegate);

#if defined(OS_WIN)
  // If glass is enabled, the bubble is allowed to extend outside the bounds of
  // the parent frame and let DWM handle compositing.  If not, then we don't
  // want to allow the bubble to extend the frame because it will be clipped.
  bubble_delegate->set_adjust_if_offscreen(ui::win::IsAeroGlassEnabled());
#elif (defined(OS_LINUX) && !defined(OS_CHROMEOS)) || defined(OS_MACOSX)
  // Linux clips bubble windows that extend outside their parent window bounds.
  // Mac never adjusts.
  bubble_delegate->set_adjust_if_offscreen(false);
#endif

  bubble_delegate->SizeToContents();
  bubble_widget->AddObserver(bubble_delegate);
  return bubble_widget;
}

BubbleDialogDelegateView* BubbleDialogDelegateView::AsBubbleDialogDelegate() {
  return this;
}

bool BubbleDialogDelegateView::ShouldShowCloseButton() const {
  return false;
}

ClientView* BubbleDialogDelegateView::CreateClientView(Widget* widget) {
  DialogClientView* client = new DialogClientView(widget, GetContentsView());
  client->set_button_row_insets(gfx::Insets());
  widget->non_client_view()->set_mirror_client_in_rtl(mirror_arrow_in_rtl_);
  return client;
}

NonClientFrameView* BubbleDialogDelegateView::CreateNonClientFrameView(
    Widget* widget) {
  BubbleFrameView* frame = new BubbleFrameView(
      gfx::Insets(kPanelVertMargin, kPanelHorizMargin, 0, kPanelHorizMargin),
      margins());
  // Note: In CreateBubble, the call to SizeToContents() will cause
  // the relayout that this call requires.
  frame->SetTitleFontList(GetTitleFontList());
  frame->SetFootnoteView(CreateFootnoteView());

  BubbleBorder::Arrow adjusted_arrow = arrow();
  if (base::i18n::IsRTL() && mirror_arrow_in_rtl_)
    adjusted_arrow = BubbleBorder::horizontal_mirror(adjusted_arrow);
  frame->SetBubbleBorder(std::unique_ptr<BubbleBorder>(
      new BubbleBorder(adjusted_arrow, shadow(), color())));
  return frame;
}

const char* BubbleDialogDelegateView::GetClassName() const {
  return kViewClassName;
}

void BubbleDialogDelegateView::OnWidgetDestroying(Widget* widget) {
  if (anchor_widget() == widget)
    SetAnchorView(NULL);
}

void BubbleDialogDelegateView::OnWidgetVisibilityChanging(Widget* widget,
                                                          bool visible) {
#if defined(OS_WIN)
  // On Windows we need to handle this before the bubble is visible or hidden.
  // Please see the comment on the OnWidgetVisibilityChanging function. On
  // other platforms it is fine to handle it after the bubble is shown/hidden.
  HandleVisibilityChanged(widget, visible);
#endif
}

void BubbleDialogDelegateView::OnWidgetVisibilityChanged(Widget* widget,
                                                         bool visible) {
#if !defined(OS_WIN)
  HandleVisibilityChanged(widget, visible);
#endif
}

void BubbleDialogDelegateView::OnWidgetActivationChanged(Widget* widget,
                                                         bool active) {
  if (close_on_deactivate() && widget == GetWidget() && !active)
    GetWidget()->Close();
}

void BubbleDialogDelegateView::OnWidgetBoundsChanged(
    Widget* widget,
    const gfx::Rect& new_bounds) {
  if (GetBubbleFrameView() && anchor_widget() == widget)
    SizeToContents();
}

View* BubbleDialogDelegateView::GetAnchorView() const {
  return ViewStorage::GetInstance()->RetrieveView(anchor_view_storage_id_);
}

gfx::Rect BubbleDialogDelegateView::GetAnchorRect() const {
  if (!GetAnchorView())
    return anchor_rect_;

  anchor_rect_ = GetAnchorView()->GetBoundsInScreen();
  anchor_rect_.Inset(anchor_view_insets_);
  return anchor_rect_;
}

void BubbleDialogDelegateView::OnBeforeBubbleWidgetInit(
    Widget::InitParams* params,
    Widget* widget) const {}

void BubbleDialogDelegateView::UseCompactMargins() {
  const int kCompactMargin = 6;
  margins_.Set(kCompactMargin, kCompactMargin, kCompactMargin, kCompactMargin);
}

void BubbleDialogDelegateView::SetAlignment(
    BubbleBorder::BubbleAlignment alignment) {
  GetBubbleFrameView()->bubble_border()->set_alignment(alignment);
  SizeToContents();
}

void BubbleDialogDelegateView::SetArrowPaintType(
    BubbleBorder::ArrowPaintType paint_type) {
  GetBubbleFrameView()->bubble_border()->set_paint_arrow(paint_type);
  SizeToContents();
}

void BubbleDialogDelegateView::SetBorderInteriorThickness(int thickness) {
  GetBubbleFrameView()->bubble_border()->SetBorderInteriorThickness(thickness);
  SizeToContents();
}

void BubbleDialogDelegateView::OnAnchorBoundsChanged() {
  SizeToContents();
}

BubbleDialogDelegateView::BubbleDialogDelegateView()
    : BubbleDialogDelegateView(nullptr, BubbleBorder::TOP_LEFT) {}

BubbleDialogDelegateView::BubbleDialogDelegateView(View* anchor_view,
                                                   BubbleBorder::Arrow arrow)
    : close_on_deactivate_(true),
      anchor_view_storage_id_(ViewStorage::GetInstance()->CreateStorageID()),
      anchor_widget_(NULL),
      arrow_(arrow),
      mirror_arrow_in_rtl_(true),
      shadow_(BubbleBorder::SMALL_SHADOW),
      color_explicitly_set_(false),
      margins_(kPanelVertMargin,
               kPanelHorizMargin,
               kPanelVertMargin,
               kPanelHorizMargin),
      accept_events_(true),
      border_accepts_events_(true),
      adjust_if_offscreen_(true),
      parent_window_(NULL) {
  if (anchor_view)
    SetAnchorView(anchor_view);
  UpdateColorsFromTheme(GetNativeTheme());
}

gfx::Rect BubbleDialogDelegateView::GetBubbleBounds() {
  // The argument rect has its origin at the bubble's arrow anchor point;
  // its size is the preferred size of the bubble's client view (this view).
  bool anchor_minimized = anchor_widget() && anchor_widget()->IsMinimized();
  return GetBubbleFrameView()->GetUpdatedWindowBounds(
      GetAnchorRect(), GetWidget()->client_view()->GetPreferredSize(),
      adjust_if_offscreen_ && !anchor_minimized);
}

const gfx::FontList& BubbleDialogDelegateView::GetTitleFontList() const {
  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  return rb.GetFontList(ui::ResourceBundle::MediumFont);
}

void BubbleDialogDelegateView::OnNativeThemeChanged(
    const ui::NativeTheme* theme) {
  UpdateColorsFromTheme(theme);
}

void BubbleDialogDelegateView::Init() {}

void BubbleDialogDelegateView::SetAnchorView(View* anchor_view) {
  // When the anchor view gets set the associated anchor widget might
  // change as well.
  if (!anchor_view || anchor_widget() != anchor_view->GetWidget()) {
    if (anchor_widget()) {
      anchor_widget_->RemoveObserver(this);
      anchor_widget_ = NULL;
    }
    if (anchor_view) {
      anchor_widget_ = anchor_view->GetWidget();
      if (anchor_widget_)
        anchor_widget_->AddObserver(this);
    }
  }

  // Remove the old storage item and set the new (if there is one).
  ViewStorage* view_storage = ViewStorage::GetInstance();
  if (view_storage->RetrieveView(anchor_view_storage_id_))
    view_storage->RemoveView(anchor_view_storage_id_);
  if (anchor_view)
    view_storage->StoreView(anchor_view_storage_id_, anchor_view);

  // Do not update anchoring for NULL views; this could indicate that our
  // NativeWindow is being destroyed, so it would be dangerous for us to update
  // our anchor bounds at that point. (It's safe to skip this, since if we were
  // to update the bounds when |anchor_view| is NULL, the bubble won't move.)
  if (anchor_view && GetWidget())
    OnAnchorBoundsChanged();
}

void BubbleDialogDelegateView::SetAnchorRect(const gfx::Rect& rect) {
  anchor_rect_ = rect;
  if (GetWidget())
    OnAnchorBoundsChanged();
}

void BubbleDialogDelegateView::SizeToContents() {
  GetWidget()->SetBounds(GetBubbleBounds());
}

BubbleFrameView* BubbleDialogDelegateView::GetBubbleFrameView() const {
  const NonClientView* view =
      GetWidget() ? GetWidget()->non_client_view() : NULL;
  return view ? static_cast<BubbleFrameView*>(view->frame_view()) : NULL;
}

void BubbleDialogDelegateView::UpdateColorsFromTheme(
    const ui::NativeTheme* theme) {
  if (!color_explicitly_set_)
    color_ = theme->GetSystemColor(ui::NativeTheme::kColorId_BubbleBackground);
  BubbleFrameView* frame_view = GetBubbleFrameView();
  if (frame_view)
    frame_view->bubble_border()->set_background_color(color());

  // When there's an opaque layer, the bubble border background won't show
  // through, so explicitly paint a background color.
  set_background(layer() && layer()->fills_bounds_opaquely()
                     ? Background::CreateSolidBackground(color())
                     : nullptr);
}

void BubbleDialogDelegateView::HandleVisibilityChanged(Widget* widget,
                                                       bool visible) {
  if (widget == GetWidget() && anchor_widget() &&
      anchor_widget()->GetTopLevelWidget()) {
    anchor_widget()->GetTopLevelWidget()->SetAlwaysRenderAsActive(visible);
  }

  // Fire AX_EVENT_ALERT for bubbles marked as AX_ROLE_ALERT_DIALOG; this
  // instructs accessibility tools to read the bubble in its entirety rather
  // than just its title and initially focused view.  See
  // http://crbug.com/474622 for details.
  if (widget == GetWidget() && visible) {
    ui::AXViewState state;
    GetAccessibleState(&state);
    if (state.role == ui::AX_ROLE_ALERT_DIALOG)
      NotifyAccessibilityEvent(ui::AX_EVENT_ALERT, true);
  }
}

}  // namespace views
