// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/display/display_list.h"

#include "base/memory/ptr_util.h"
#include "ui/display/display_observer.h"

namespace display {

DisplayListObserverLock::~DisplayListObserverLock() {
  display_list_->DecrementObserverSuspendLockCount();
}

DisplayListObserverLock::DisplayListObserverLock(DisplayList* display_list)
    : display_list_(display_list) {
  display_list_->IncrementObserverSuspendLockCount();
}

DisplayList::DisplayList() {}

DisplayList::~DisplayList() {
  DCHECK_EQ(0, observer_suspend_lock_count_);
}

void DisplayList::AddObserver(display::DisplayObserver* observer) {
  observers_.AddObserver(observer);
}

void DisplayList::RemoveObserver(display::DisplayObserver* observer) {
  observers_.RemoveObserver(observer);
}

DisplayList::Displays::const_iterator DisplayList::FindDisplayById(
    int64_t id) const {
  for (auto iter = displays_.begin(); iter != displays_.end(); ++iter) {
    if (iter->id() == id)
      return iter;
  }
  return displays_.end();
}

DisplayList::Displays::iterator DisplayList::FindDisplayById(int64_t id) {
  for (auto iter = displays_.begin(); iter != displays_.end(); ++iter) {
    if (iter->id() == id)
      return iter;
  }
  return displays_.end();
}

DisplayList::Displays::const_iterator DisplayList::GetPrimaryDisplayIterator()
    const {
  return primary_display_index_ == -1
             ? displays_.end()
             : displays_.begin() + primary_display_index_;
}

std::unique_ptr<DisplayListObserverLock> DisplayList::SuspendObserverUpdates() {
  return base::WrapUnique(new DisplayListObserverLock(this));
}

void DisplayList::UpdateDisplay(const display::Display& display, Type type) {
  auto iter = FindDisplayById(display.id());
  DCHECK(iter != displays_.end());

  display::Display* local_display = &(*iter);
  uint32_t changed_values = 0;
  if (type == Type::PRIMARY &&
      static_cast<int>(iter - displays_.begin()) !=
          static_cast<int>(GetPrimaryDisplayIterator() - displays_.begin())) {
    primary_display_index_ = static_cast<int>(iter - displays_.begin());
    // ash::DisplayManager only notifies for the Display gaining primary, not
    // the one losing it.
    changed_values |= display::DisplayObserver::DISPLAY_METRIC_PRIMARY;
  }
  if (local_display->bounds() != display.bounds()) {
    local_display->set_bounds(display.bounds());
    changed_values |= display::DisplayObserver::DISPLAY_METRIC_BOUNDS;
  }
  if (local_display->work_area() != display.work_area()) {
    local_display->set_work_area(display.work_area());
    changed_values |= display::DisplayObserver::DISPLAY_METRIC_WORK_AREA;
  }
  if (local_display->rotation() != display.rotation()) {
    local_display->set_rotation(display.rotation());
    changed_values |= display::DisplayObserver::DISPLAY_METRIC_ROTATION;
  }
  if (local_display->device_scale_factor() != display.device_scale_factor()) {
    local_display->set_device_scale_factor(display.device_scale_factor());
    changed_values |=
        display::DisplayObserver::DISPLAY_METRIC_DEVICE_SCALE_FACTOR;
  }
  if (should_notify_observers()) {
    FOR_EACH_OBSERVER(display::DisplayObserver, observers_,
                      OnDisplayMetricsChanged(*local_display, changed_values));
  }
}

void DisplayList::AddDisplay(const display::Display& display, Type type) {
  DCHECK(displays_.end() == FindDisplayById(display.id()));
  displays_.push_back(display);
  if (type == Type::PRIMARY)
    primary_display_index_ = static_cast<int>(displays_.size()) - 1;
  if (should_notify_observers()) {
    FOR_EACH_OBSERVER(display::DisplayObserver, observers_,
                      OnDisplayAdded(display));
  }
}

void DisplayList::RemoveDisplay(int64_t id) {
  auto iter = FindDisplayById(id);
  DCHECK(displays_.end() != iter);
  if (primary_display_index_ == static_cast<int>(iter - displays_.begin())) {
    // The primary display can only be removed if it is the last display.
    // Users must choose a new primary before removing an old primary display.
    DCHECK_EQ(1u, displays_.size());
    primary_display_index_ = -1;
  } else if (primary_display_index_ >
             static_cast<int>(iter - displays_.begin())) {
    primary_display_index_--;
  }
  const display::Display display = *iter;
  displays_.erase(iter);
  if (should_notify_observers()) {
    FOR_EACH_OBSERVER(display::DisplayObserver, observers_,
                      OnDisplayRemoved(display));
  }
}

void DisplayList::IncrementObserverSuspendLockCount() {
  observer_suspend_lock_count_++;
}

void DisplayList::DecrementObserverSuspendLockCount() {
  DCHECK_GT(observer_suspend_lock_count_, 0);
  observer_suspend_lock_count_--;
}

}  // namespace display
