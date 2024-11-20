/* Copyright 2022 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/lite/tensorflow_profiler_logger.h"

#include <stdlib.h>

#include <algorithm>
#include <memory>
#include <string>

#include "tensorflow/core/profiler/lib/scoped_memory_debug_annotation.h"
#include "tensorflow/core/profiler/lib/traceme.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/kernels/kernel_util.h"

namespace tflite {
namespace {

struct Statistics {
  uint64_t total_bytes_allocated = 0LL;
  uint64_t peak_bytes_in_use = 0LL;
};
static Statistics g_stat_dynamic;
static Statistics g_stat_arena;

static char g_current_op_name[256];

// Adds memory trace information for TensorFlow profiler.
// `stat`: Statistics object for the (de)allocation.
// `is_allocating`: Whether memory is being allocated or deallocated.
// `allocation_bytes`: The number of bytes being allocated or deallocated.
// `requested_bytes`: The number of bytes requested for allocation/deallocation.
// `tensor_id`: A unique ID for the tensor being allocated or deallocated.
//              Usually the memory address should be used.
// `name`: The name of the tensor being allocated or deallocated.
// `dims`: The dimension of the tensor in a string form.
std::string AddTraceMeInternal(Statistics* stat, bool is_allocating,
                               const std::string& allocator_name,
                               int64_t tensor_id, const std::string& name,
                               const std::string& dims,
                               int64_t allocation_bytes,
                               int64_t requested_bytes) {
  if (is_allocating) {
    stat->total_bytes_allocated += allocation_bytes;
  } else {
    stat->total_bytes_allocated -= allocation_bytes;
  }
  stat->peak_bytes_in_use =
      std::max(stat->peak_bytes_in_use, stat->total_bytes_allocated);
  int64_t total_bytes_allocated = stat->total_bytes_allocated;
  int64_t peak_bytes_in_use = stat->peak_bytes_in_use;

  std::string res = tensorflow::profiler::TraceMeEncode(
      is_allocating ? "MemoryAllocation" : "MemoryDeallocation",
      // Note that all of these fields are necessary for profiling UI.
      {{"allocator_name", allocator_name},  // name shown on 'Memory ID'
       {"bytes_allocated", total_bytes_allocated},
       {"peak_bytes_in_use", peak_bytes_in_use},
       {"requested_bytes", requested_bytes},
       {"allocation_bytes", allocation_bytes},
       // Note: addr is used as a key to match alloc and dealloc.
       {"addr", tensor_id},
       // Note that we're using tensor.name not op name here.
       {"tf_op", name},
       {"shape", dims}});
  // Note: bytes_reserved, fragmentation, data_type, region_type, id
  // can be potentially useful but not added.
  return res;
}

void AddTraceMe(bool is_allocating, TfLiteTensor* tensor,
                size_t allocation_bytes) {
  if (tensor == nullptr || allocation_bytes == 0) return;
  int64_t tensor_id = reinterpret_cast<int64_t>(tensor->data.raw);
  std::string name;
  if (g_current_op_name[0]) {
    name = g_current_op_name;
  }
  if (tensor->name) {
    name += ":";
    name += tensor->name;
  }
  std::string dims = tensor->dims ? GetShapeDebugString(tensor->dims) : "[]";
  int64_t requested_bytes = is_allocating ? allocation_bytes : 0;
  const std::string allocator_name = "_tflite_native_dynamic";

  tensorflow::profiler::TraceMe::InstantActivity(
      [is_allocating, allocator_name, tensor_id, name, dims, allocation_bytes,
       requested_bytes]() {
        return AddTraceMeInternal(&g_stat_dynamic, is_allocating,
                                  allocator_name, tensor_id, name, dims,
                                  allocation_bytes, requested_bytes);
      },
      /*level=*/tensorflow::profiler::TraceMeLevel::kInfo);
}

}  // namespace

void OnTfLiteOpPrepare(const char* op_name, int subgraph_index,
                       int node_index) {
  snprintf(g_current_op_name, sizeof(g_current_op_name), "%sPrepare_%d",
           op_name, node_index);
  // Updates TF's current annotation object by creating scoped annotation obj.
  tensorflow::profiler::ScopedMemoryDebugAnnotation annotation(
      g_current_op_name);
}

tensorflow::profiler::TraceMe* OnTfLiteSubgraphInvoke(const char* name,
                                                      int index) {
  tensorflow::profiler::TraceMe* trace_me =
      new tensorflow::profiler::TraceMe([name, index]() {
        char eventName[256];
        snprintf(eventName, sizeof(eventName), "Subgraph%d", index);
        return tensorflow::profiler::TraceMeEncode(
            eventName, {{"subgraph_name", name}, {"subgraph_index", index}});
      });
  return trace_me;
}

void OnTfLiteSubgraphInvokeEnd(tensorflow::profiler::TraceMe* trace_me) {
  delete trace_me;
}

tensorflow::profiler::TraceMe* OnTfLiteOpInvoke(const char* op_name,
                                                int subgraph_index,
                                                int node_index) {
  snprintf(g_current_op_name, sizeof(g_current_op_name), "%s_%d", op_name,
           node_index);
  // Updates TF's current annotation object by creating scoped annotation obj.
  tensorflow::profiler::ScopedMemoryDebugAnnotation annotation(
      g_current_op_name);

  tensorflow::profiler::TraceMe* trace_me = new tensorflow::profiler::TraceMe(
      [op_name, subgraph_index, node_index]() {
        char eventName[256];
        // TF ops should have "<detail>:<op_name>" format.
        snprintf(eventName, sizeof(eventName), "%s:%s", op_name, op_name);
        return tensorflow::profiler::TraceMeEncode(
            eventName, {{"is_eager", 0},
                        {"subgraph_index", subgraph_index},
                        {"node_index", node_index}});
      });
  return trace_me;
}

void OnTfLiteOpInvokeEnd(tensorflow::profiler::TraceMe* trace_me) {
  delete trace_me;
}

void OnTfLiteTensorAlloc(TfLiteTensor* tensor, size_t num_bytes) {
  AddTraceMe(/*is_allocating=*/true, tensor, num_bytes);
}

void OnTfLiteTensorDealloc(TfLiteTensor* tensor) {
  if (tensor != nullptr) {
    size_t num_bytes = tensor->bytes;
    AddTraceMe(/*is_allocating=*/false, tensor, num_bytes);
  }
}

void AddArenaTrace(bool is_allocating, int subgraph_index, int arena_id,
                   size_t allocation_bytes) {
  std::string name = "Subgraph" + std::to_string(subgraph_index);
  int64_t tensor_id = arena_id;
  std::string dims = "";
  int64_t requested_bytes = is_allocating ? allocation_bytes : 0;
  const std::string allocator_name = "_tflite_arena";

  tensorflow::profiler::TraceMe::InstantActivity(
      [is_allocating, allocator_name, tensor_id, name, dims, allocation_bytes,
       requested_bytes]() {
        return AddTraceMeInternal(&g_stat_arena, is_allocating, allocator_name,
                                  tensor_id, name, dims, allocation_bytes,
                                  requested_bytes);
      },
      /*level=*/tensorflow::profiler::TraceMeLevel::kInfo);
}

void OnTfLiteArenaAlloc(int subgraph_index, int arena_id, size_t num_bytes) {
  if (num_bytes == 0) return;
  AddArenaTrace(/*is_allocating=*/true, subgraph_index, arena_id, num_bytes);
}

void OnTfLiteArenaDealloc(int subgraph_index, int arena_id, size_t num_bytes) {
  if (num_bytes == 0) return;
  AddArenaTrace(/*is_allocating=*/false, subgraph_index, arena_id, num_bytes);
}

}  // namespace tflite
