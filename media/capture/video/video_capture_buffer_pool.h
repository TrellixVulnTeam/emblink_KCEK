// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MEDIA_VIDEO_CAPTURE_VIDEO_CAPTURE_BUFFER_POOL_H_
#define MEDIA_VIDEO_CAPTURE_VIDEO_CAPTURE_BUFFER_POOL_H_

#include "base/memory/ref_counted.h"
#include "media/base/video_capture_types.h"
#include "media/capture/capture_export.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/gpu_memory_buffer.h"

namespace media {

class VideoCaptureBufferHandle;

// A thread-safe class that does the bookkeeping and lifetime management for a
// pool of pixel buffers cycled between an in-process producer (e.g. a
// VideoCaptureDevice) and a set of out-of-process consumers. The pool is
// intended to be orchestrated by a VideoCaptureDevice::Client, but is designed
// to outlive the controller if necessary. The pixel buffers may be backed by a
// SharedMemory, but this is not compulsory.
//
// Producers get a buffer by calling ReserveForProducer(), and may pass on their
// ownership to the consumer by calling HoldForConsumers(), or drop the buffer
// (without further processing) by calling RelinquishProducerReservation().
// Consumers signal that they are done with the buffer by calling
// RelinquishConsumerHold().
//
// Buffers are allocated on demand, but there will never be more than |count|
// buffers in existence at any time. Buffers are identified by an int value
// called |buffer_id|. -1 (kInvalidId) is never a valid ID, and is returned by
// some methods to indicate failure. The active set of buffer ids may change
// over the lifetime of the buffer pool, as existing buffers are freed and
// reallocated at larger size. When reallocation occurs, new buffer IDs will
// circulate.
class CAPTURE_EXPORT VideoCaptureBufferPool
    : public base::RefCountedThreadSafe<VideoCaptureBufferPool> {
 public:
  static constexpr int kInvalidId = -1;

  // One-time (per client/per-buffer) initialization to share a particular
  // buffer to a process. The shared handle is returned as |new_handle|.
  virtual bool ShareToProcess(int buffer_id,
                              base::ProcessHandle process_handle,
                              base::SharedMemoryHandle* new_handle) = 0;
  virtual bool ShareToProcess2(int buffer_id,
                               int plane,
                               base::ProcessHandle process_handle,
                               gfx::GpuMemoryBufferHandle* new_handle) = 0;

  // Try and obtain a BufferHandle for |buffer_id|.
  virtual std::unique_ptr<VideoCaptureBufferHandle> GetBufferHandle(
      int buffer_id) = 0;

  // Reserve or allocate a buffer to support a packed frame of |dimensions| of
  // pixel |format| and return its id. This will fail (returning kInvalidId) if
  // the pool already is at its |count| limit of the number of allocations, and
  // all allocated buffers are in use by the producer and/or consumers.
  //
  // If successful, the reserved buffer remains reserved (and writable by the
  // producer) until ownership is transferred either to the consumer via
  // HoldForConsumers(), or back to the pool with
  // RelinquishProducerReservation().
  //
  // On occasion, this call will decide to free an old buffer to make room for a
  // new allocation at a larger size. If so, the ID of the destroyed buffer is
  // returned via |buffer_id_to_drop|.
  virtual int ReserveForProducer(const gfx::Size& dimensions,
                                 media::VideoPixelFormat format,
                                 media::VideoPixelStorage storage,
                                 int* buffer_id_to_drop) = 0;

  // Indicate that a buffer held for the producer should be returned back to the
  // pool without passing on to the consumer. This effectively is the opposite
  // of ReserveForProducer().
  virtual void RelinquishProducerReservation(int buffer_id) = 0;

  // Attempt to reserve the same buffer that was relinquished in the last call
  // to RelinquishProducerReservation(). If the buffer is not still being
  // consumed, and has not yet been re-used since being consumed, and the
  // specified |dimensions|, |format|, and |storage| agree with its last
  // reservation, this will succeed. Otherwise, |kInvalidId| will be returned.
  //
  // A producer may assume the content of the buffer has been preserved and may
  // also make modifications.
  virtual int ResurrectLastForProducer(const gfx::Size& dimensions,
                                       media::VideoPixelFormat format,
                                       media::VideoPixelStorage storage) = 0;

  // Returns a snapshot of the current number of buffers in-use divided by the
  // maximum |count_|.
  virtual double GetBufferPoolUtilization() const = 0;

  // Transfer a buffer from producer to consumer ownership.
  // |buffer_id| must be a buffer index previously returned by
  // ReserveForProducer(), and not already passed to HoldForConsumers().
  virtual void HoldForConsumers(int buffer_id, int num_clients) = 0;

  // Indicate that one or more consumers are done with a particular buffer. This
  // effectively is the opposite of HoldForConsumers(). Once the consumers are
  // done, a buffer is returned to the pool for reuse.
  virtual void RelinquishConsumerHold(int buffer_id, int num_clients) = 0;

 protected:
  virtual ~VideoCaptureBufferPool() {}

 private:
  friend class base::RefCountedThreadSafe<VideoCaptureBufferPool>;
};

}  // namespace media

#endif  // MEDIA_VIDEO_CAPTURE_VIDEO_CAPTURE_BUFFER_POOL_H_
