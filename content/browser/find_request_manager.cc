// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/find_request_manager.h"

#include "content/browser/frame_host/render_frame_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/common/frame_messages.h"
#include "content/common/input_messages.h"

namespace content {

namespace {

// Returns the deepest last child frame under |node|/|rfh| in the frame tree.
FrameTreeNode* GetDeepestLastChild(FrameTreeNode* node) {
  while (node->child_count())
    node = node->child_at(node->child_count() - 1);
  return node;
}
RenderFrameHost* GetDeepestLastChild(RenderFrameHost* rfh) {
  FrameTreeNode* node =
      static_cast<RenderFrameHostImpl*>(rfh)->frame_tree_node();
  return GetDeepestLastChild(node)->current_frame_host();
}

// Returns the FrameTreeNode directly after |node| in the frame tree in search
// order, or nullptr if one does not exist. If |wrap| is set, then wrapping
// between the first and last frames is permitted. Note that this traversal
// follows the same ordering as in blink::FrameTree::traverseNextWithWrap().
FrameTreeNode* TraverseNext(FrameTreeNode* node, bool wrap) {
  if (node->child_count())
    return node->child_at(0);

  FrameTreeNode* sibling = node->NextSibling();
  while (!sibling) {
    if (!node->parent())
      return wrap ? node : nullptr;
    node = node->parent();
    sibling = node->NextSibling();
  }
  return sibling;
}

// Returns the FrameTreeNode directly before |node| in the frame tree in search
// order, or nullptr if one does not exist. If |wrap| is set, then wrapping
// between the first and last frames is permitted. Note that this traversal
// follows the same ordering as in blink::FrameTree::traversePreviousWithWrap().
FrameTreeNode* TraversePrevious(FrameTreeNode* node, bool wrap) {
  if (FrameTreeNode* previous_sibling = node->PreviousSibling())
    return GetDeepestLastChild(previous_sibling);
  if (node->parent())
    return node->parent();
  return wrap ? GetDeepestLastChild(node) : nullptr;
}

// The same as either TraverseNext() or TraversePrevious() depending on
// |forward|.
FrameTreeNode* TraverseNode(FrameTreeNode* node, bool forward, bool wrap) {
  return forward ? TraverseNext(node, wrap) : TraversePrevious(node, wrap);
}

}  // namespace

// static
const int FindRequestManager::kInvalidId = -1;

FindRequestManager::FindRequestManager(WebContentsImpl* web_contents)
    : WebContentsObserver(web_contents),
      contents_(web_contents),
      current_session_id_(kInvalidId),
      pending_find_next_reply_(nullptr),
      pending_active_match_ordinal_(false),
      number_of_matches_(0),
      active_frame_(nullptr),
      relative_active_match_ordinal_(0),
      active_match_ordinal_(0),
      last_reported_id_(kInvalidId) {}

FindRequestManager::~FindRequestManager() {}

void FindRequestManager::Find(int request_id,
                              const base::string16& search_text,
                              const blink::WebFindOptions& options) {
  // Every find request must have a unique ID, and these IDs must strictly
  // increase so that newer requests always have greater IDs than older
  // requests.
  DCHECK_GT(request_id, current_request_.id);
  DCHECK_GT(request_id, current_session_id_);

  // If this is a new find session, clear any queued requests from last session.
  if (!options.findNext)
    find_request_queue_ = std::queue<FindRequest>();

  find_request_queue_.emplace(request_id, search_text, options);
  if (find_request_queue_.size() == 1)
    FindInternal(find_request_queue_.front());
}

void FindRequestManager::StopFinding(StopFindAction action) {
  contents_->SendToAllFrames(
      new FrameMsg_StopFinding(MSG_ROUTING_NONE, action));

  current_session_id_ = kInvalidId;
}

void FindRequestManager::OnFindReply(RenderFrameHost* rfh,
                                     int request_id,
                                     int number_of_matches,
                                     const gfx::Rect& selection_rect,
                                     int active_match_ordinal,
                                     bool final_update) {
  // Ignore stale replies from abandoned find sessions.
  if (current_session_id_ == kInvalidId || request_id < current_session_id_)
    return;
  DCHECK(CheckFrame(rfh));

  // Update the stored find results.

  DCHECK_GE(number_of_matches, -1);
  DCHECK_GE(active_match_ordinal, -1);

  // Check for an update to the number of matches.
  if (number_of_matches != -1) {
    DCHECK_GE(number_of_matches, 0);
    auto matches_per_frame_it = matches_per_frame_.find(rfh);
    if (int matches_delta = number_of_matches - matches_per_frame_it->second) {
      // Increment the global number of matches by the number of additional
      // matches found for this frame.
      number_of_matches_ += matches_delta;
      matches_per_frame_it->second = number_of_matches;

      // The active match ordinal may need updating since the number of matches
      // before the active match may have changed.
      if (rfh != active_frame_)
        UpdateActiveMatchOrdinal();
    }
  }

  // Check for an update to the selection rect.
  if (!selection_rect.IsEmpty())
    selection_rect_ = selection_rect;

  // Check for an update to the active match ordinal.
  if (active_match_ordinal > 0) {
    if (rfh == active_frame_) {
      active_match_ordinal_ +=
          active_match_ordinal - relative_active_match_ordinal_;
      relative_active_match_ordinal_ = active_match_ordinal;
    } else {
      if (active_frame_) {
        // The new active match is in a different frame than the previous, so
        // the previous active frame needs to be informed (to clear its active
        // match highlighting).
        active_frame_->Send(new FrameMsg_ClearActiveFindMatch(
            active_frame_->GetRoutingID()));
      }
      active_frame_ = rfh;
      relative_active_match_ordinal_ = active_match_ordinal;
      UpdateActiveMatchOrdinal();
    }
    if (pending_active_match_ordinal_ && request_id == current_request_.id)
      pending_active_match_ordinal_ = false;
    AdvanceQueue(request_id);
  }

  if (!final_update) {
    NotifyFindReply(request_id, false /* final_update */);
    return;
  }

  // This is the final update for this frame for the current find operation.

  pending_initial_replies_.erase(rfh);
  if (request_id == current_session_id_ && !pending_initial_replies_.empty()) {
    NotifyFindReply(request_id, false /* final_update */);
    return;
  }

  // This is the final update for the current find operation.

  if (request_id == current_request_.id && request_id != current_session_id_) {
    DCHECK(current_request_.options.findNext);
    DCHECK_EQ(pending_find_next_reply_, rfh);
    pending_find_next_reply_ = nullptr;
  }

  FinalUpdateReceived(request_id, rfh);
}

void FindRequestManager::RemoveFrame(RenderFrameHost* rfh) {
  if (current_session_id_ == kInvalidId || !CheckFrame(rfh))
    return;

  // If matches are counted for the frame that is being removed, decrement the
  // match total before erasing that entry.
  auto it = matches_per_frame_.find(rfh);
  if (it != matches_per_frame_.end()) {
    number_of_matches_ -= it->second;
    matches_per_frame_.erase(it);
  }

  // Update the active match ordinal, since it may have changed.
  if (active_frame_ == rfh) {
    active_frame_ = nullptr;
    relative_active_match_ordinal_ = 0;
    selection_rect_ = gfx::Rect();
  }
  UpdateActiveMatchOrdinal();

  // If no pending find replies are expected for the removed frame, then just
  // report the updated results.
  if (!pending_initial_replies_.count(rfh) && pending_find_next_reply_ != rfh) {
    bool final_update =
        pending_initial_replies_.empty() && !pending_find_next_reply_;
    NotifyFindReply(current_session_id_, final_update);
    return;
  }

  if (pending_initial_replies_.count(rfh)) {
    // A reply should not be expected from the removed frame.
    pending_initial_replies_.erase(rfh);
    if (pending_initial_replies_.empty()) {
      FinalUpdateReceived(current_session_id_, rfh);
    }
  }

  if (pending_find_next_reply_ == rfh) {
    // A reply should not be expected from the removed frame.
    pending_find_next_reply_ = nullptr;
    FinalUpdateReceived(current_request_.id, rfh);
  }
}

void FindRequestManager::DidFinishLoad(RenderFrameHost* rfh,
                                       const GURL& validated_url) {
  if (current_session_id_ == kInvalidId)
    return;

  RemoveFrame(rfh);
  AddFrame(rfh, true /* force */);
}

void FindRequestManager::RenderFrameDeleted(RenderFrameHost* rfh) {
  RemoveFrame(rfh);
}

void FindRequestManager::RenderFrameHostChanged(RenderFrameHost* old_host,
                                                RenderFrameHost* new_host) {
  RemoveFrame(old_host);
}

void FindRequestManager::FrameDeleted(RenderFrameHost* rfh) {
  RemoveFrame(rfh);
}

void FindRequestManager::Reset(const FindRequest& initial_request) {
  current_session_id_ = initial_request.id;
  current_request_ = initial_request;
  pending_initial_replies_.clear();
  pending_find_next_reply_ = nullptr;
  pending_active_match_ordinal_ = true;
  matches_per_frame_.clear();
  number_of_matches_ = 0;
  active_frame_ = nullptr;
  relative_active_match_ordinal_ = 0;
  active_match_ordinal_ = 0;
  selection_rect_ = gfx::Rect();
  last_reported_id_ = kInvalidId;
}

void FindRequestManager::FindInternal(const FindRequest& request) {
  DCHECK_GT(request.id, current_request_.id);
  DCHECK_GT(request.id, current_session_id_);

  if (request.options.findNext) {
    // This is a find next operation.

    // This implies that there is an ongoing find session with the same search
    // text.
    DCHECK_GE(current_session_id_, 0);
    DCHECK_EQ(request.search_text, current_request_.search_text);

    // The find next request will be directed at the focused frame if there is
    // one, or the first frame with matches otherwise.
    RenderFrameHost* target_rfh = contents_->GetFocusedFrame();
    if (!target_rfh || !CheckFrame(target_rfh))
      target_rfh = GetInitialFrame(request.options.forward);

    SendFindIPC(request, target_rfh);
    current_request_ = request;
    pending_active_match_ordinal_ = true;
    return;
  }

  // This is an initial find operation.
  Reset(request);
  for (FrameTreeNode* node : contents_->GetFrameTree()->Nodes())
    AddFrame(node->current_frame_host(), false /* force */);
}

void FindRequestManager::AdvanceQueue(int request_id) {
  if (find_request_queue_.empty() ||
      request_id != find_request_queue_.front().id) {
    return;
  }

  find_request_queue_.pop();
  if (!find_request_queue_.empty())
    FindInternal(find_request_queue_.front());
}

void FindRequestManager::SendFindIPC(const FindRequest& request,
                                     RenderFrameHost* rfh) {
  DCHECK(CheckFrame(rfh));
  DCHECK(rfh->IsRenderFrameLive());

  if (request.options.findNext)
    pending_find_next_reply_ = rfh;
  else
    pending_initial_replies_.insert(rfh);

  rfh->Send(new FrameMsg_Find(rfh->GetRoutingID(), request.id,
                              request.search_text, request.options));
}

void FindRequestManager::NotifyFindReply(int request_id, bool final_update) {
  if (request_id == kInvalidId) {
    NOTREACHED();
    return;
  }

  // Ensure that replies are not reported with IDs lower than the ID of the
  // latest request we have results from.
  if (request_id < last_reported_id_)
    request_id = last_reported_id_;
  else
    last_reported_id_ = request_id;

  contents_->NotifyFindReply(request_id, number_of_matches_, selection_rect_,
                             active_match_ordinal_, final_update);
}

RenderFrameHost* FindRequestManager::GetInitialFrame(bool forward) const {
  RenderFrameHost* rfh = contents_->GetMainFrame();

  if (!forward)
    rfh = GetDeepestLastChild(rfh);

  return rfh;
}

RenderFrameHost* FindRequestManager::Traverse(RenderFrameHost* from_rfh,
                                              bool forward,
                                              bool matches_only,
                                              bool wrap) const {
  FrameTreeNode* node =
      static_cast<RenderFrameHostImpl*>(from_rfh)->frame_tree_node();

  while ((node = TraverseNode(node, forward, wrap)) != nullptr) {
    if (!CheckFrame(node->current_frame_host()))
      continue;
    RenderFrameHost* current_rfh = node->current_frame_host();
    if (!matches_only || matches_per_frame_.find(current_rfh)->second ||
        pending_initial_replies_.count(current_rfh)) {
      // Note that if there is still a pending reply expected for this frame,
      // then it may have unaccounted matches and will not be skipped via
      // |matches_only|.
      return node->current_frame_host();
    }
    if (wrap && node->current_frame_host() == from_rfh)
      return nullptr;
  }

  return nullptr;
}

void FindRequestManager::AddFrame(RenderFrameHost* rfh, bool force) {
  if (!rfh || !rfh->IsRenderFrameLive())
    return;

  // A frame that is already being searched should not normally be added again.
  DCHECK(force || !CheckFrame(rfh));

  matches_per_frame_[rfh] = 0;

  FindRequest request = current_request_;
  request.id = current_session_id_;
  request.options.findNext = false;
  request.options.force = force;
  SendFindIPC(request, rfh);
}

bool FindRequestManager::CheckFrame(RenderFrameHost* rfh) const {
  return rfh && matches_per_frame_.count(rfh);
}

void FindRequestManager::UpdateActiveMatchOrdinal() {
  active_match_ordinal_ = 0;

  if (!active_frame_ || !relative_active_match_ordinal_) {
    DCHECK(!active_frame_ && !relative_active_match_ordinal_);
    return;
  }

  // Traverse the frame tree backwards (in search order) and count all of the
  // matches in frames before the frame with the active match, in order to
  // determine the overall active match ordinal.
  RenderFrameHost* frame = active_frame_;
  while ((frame = Traverse(frame,
                           false /* forward */,
                           true /* matches_only */,
                           false /* wrap */)) != nullptr) {
    active_match_ordinal_ += matches_per_frame_[frame];
  }
  active_match_ordinal_ += relative_active_match_ordinal_;
}

void FindRequestManager::FinalUpdateReceived(int request_id,
                                             RenderFrameHost* rfh) {
  if (!number_of_matches_ ||
      (active_match_ordinal_ && !pending_active_match_ordinal_) ||
      pending_find_next_reply_) {
    // All the find results for |request_id| are in and ready to report. Note
    // that |final_update| will be set to false if there are still pending
    // replies expected from the initial find request.
    NotifyFindReply(request_id,
                    pending_initial_replies_.empty() /* final_update */);
    AdvanceQueue(request_id);
    return;
  }

  // There are matches, but no active match was returned, so another find next
  // request must be sent.

  RenderFrameHost* target_rfh;
  if (request_id == current_request_.id && current_request_.options.findNext) {
    // If this was a find next operation, then the active match will be in the
    // next frame with matches after this one.
    target_rfh = Traverse(rfh,
                          current_request_.options.forward,
                          true /* matches_only */,
                          true /* wrap */);
  } else if ((target_rfh = contents_->GetFocusedFrame()) != nullptr) {
    // Otherwise, if there is a focused frame, then the active match will be in
    // the next frame with matches after that one.
    target_rfh = Traverse(target_rfh,
                          current_request_.options.forward,
                          true /* matches_only */,
                          true /* wrap */);
  } else {
    // Otherwise, the first frame with matches will have the active match.
    target_rfh = GetInitialFrame(current_request_.options.forward);
    if (!CheckFrame(target_rfh) || !matches_per_frame_[target_rfh]) {
      target_rfh = Traverse(target_rfh,
                            current_request_.options.forward,
                            true /* matches_only */,
                            false /* wrap */);
    }
  }
  DCHECK(target_rfh);

  // Forward the find reply without |final_update| set because the active match
  // has not yet been found.
  NotifyFindReply(request_id, false /* final_update */);

  current_request_.options.findNext = true;
  SendFindIPC(current_request_, target_rfh);
}

}  // namespace content
