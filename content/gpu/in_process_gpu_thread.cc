// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/gpu/in_process_gpu_thread.h"

#include "base/time/time.h"
#include "build/build_config.h"
#include "content/gpu/gpu_child_thread.h"
#include "content/gpu/gpu_process.h"
#include "gpu/config/gpu_info_collector.h"
#include "gpu/ipc/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "ui/gl/init/gl_factory.h"

namespace content {

InProcessGpuThread::InProcessGpuThread(
    const InProcessChildThreadParams& params,
    const gpu::GpuPreferences& gpu_preferences)
    : base::Thread("Chrome_InProcGpuThread"),
      params_(params),
      gpu_process_(NULL),
      gpu_preferences_(gpu_preferences),
      gpu_memory_buffer_factory_(
          gpu::GetNativeGpuMemoryBufferType() != gfx::EMPTY_BUFFER
          ? gpu::GpuMemoryBufferFactory::CreateNativeType()
          : nullptr) {}

InProcessGpuThread::~InProcessGpuThread() {
  Stop();
}

void InProcessGpuThread::Init() {
  base::ThreadPriority io_thread_priority = base::ThreadPriority::NORMAL;

  gpu_process_ = new GpuProcess(io_thread_priority);

  gpu::GPUInfo gpu_info;
  if (!gl::init::InitializeGLOneOff())
    VLOG(1) << "gl::init::InitializeGLOneOff failed";
  else
    gpu::CollectContextGraphicsInfo(&gpu_info);

  // The process object takes ownership of the thread object, so do not
  // save and delete the pointer.
  GpuChildThread* child_thread =
      new GpuChildThread(params_, gpu_info, gpu_memory_buffer_factory_.get());

  // Since we are in the browser process, use the thread start time as the
  // process start time.
  child_thread->Init(base::Time::Now());

  gpu_process_->set_main_thread(child_thread);
}

void InProcessGpuThread::CleanUp() {
  SetThreadWasQuitProperly(true);
  delete gpu_process_;
}

base::Thread* CreateInProcessGpuThread(
    const InProcessChildThreadParams& params,
    const gpu::GpuPreferences& gpu_preferences) {
  return new InProcessGpuThread(params, gpu_preferences);
}

}  // namespace content
