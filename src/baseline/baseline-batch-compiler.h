// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_BASELINE_BATCH_COMPILER_H_
#define V8_BASELINE_BASELINE_BATCH_COMPILER_H_

#include "src/handles/global-handles.h"
#include "src/handles/handles.h"

namespace v8 {
namespace internal {
namespace baseline {

class BaselineBatchCompiler {
 public:
  static const int kInitialQueueSize = 4;

  explicit BaselineBatchCompiler(Isolate* isolate);
  ~BaselineBatchCompiler();
  // Enqueues SharedFunctionInfo of |function| for compilation.
  // Returns true if the function is compiled (either it was compiled already,
  // or the current batch including the function was just compiled).
  bool EnqueueFunction(Handle<JSFunction> function);

  void set_enabled(bool enabled) { enabled_ = enabled; }
  bool is_enabled() { return enabled_; }

 private:
  // Ensure there is enough space in the compilation queue to enqueue another
  // function, growing the queue if necessary.
  void EnsureQueueCapacity();

  // Returns true if the current batch exceeds the threshold and should be
  // compiled.
  bool ShouldCompileBatch() const;

  // Compiles the current batch and returns the number of functions compiled.
  void CompileBatch(Handle<JSFunction> function);

  // Resets the current batch.
  void ClearBatch();

  // Tries to compile |maybe_sfi|. Returns false if compilation was not possible
  // (e.g. bytecode was fushed, weak handle no longer valid, ...).
  bool MaybeCompileFunction(MaybeObject maybe_sfi);

  Isolate* isolate_;

  // Global handle to shared function infos enqueued for compilation in the
  // current batch.
  Handle<WeakFixedArray> compilation_queue_;

  // Last index set in compilation_queue_;
  int last_index_;

  // Estimated insturction size of current batch.
  int estimated_instruction_size_;

  // Flag indicating whether batch compilation is enabled.
  // Batch compilation can be dynamically disabled e.g. when creating snapshots.
  bool enabled_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_BASELINE_BATCH_COMPILER_H_
