/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

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

#include "tensorflow/lite/delegates/gpu/metal/kernels/test_util.h"

#import <Metal/Metal.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/substitute.h"
#include "tensorflow/lite/delegates/gpu/common/convert.h"
#include "tensorflow/lite/delegates/gpu/common/gpu_info.h"
#include "tensorflow/lite/delegates/gpu/common/precision.h"
#include "tensorflow/lite/delegates/gpu/common/shape.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"
#include "tensorflow/lite/delegates/gpu/common/tensor.h"
#include "tensorflow/lite/delegates/gpu/common/types.h"
#include "tensorflow/lite/delegates/gpu/common/util.h"
#include "tensorflow/lite/delegates/gpu/metal/compute_task.h"
#include "tensorflow/lite/delegates/gpu/metal/metal_spatial_tensor.h"

namespace tflite {
namespace gpu {
namespace metal {

std::vector<CalculationsPrecision>
MetalExecutionEnvironment::GetSupportedPrecisions() const {
  return {CalculationsPrecision::F32, CalculationsPrecision::F32_F16,
          CalculationsPrecision::F16};
}

std::vector<TensorStorageType> MetalExecutionEnvironment::GetSupportedStorages(
    DataType data_type) const {
  return {TensorStorageType::BUFFER, TensorStorageType::IMAGE_BUFFER,
          TensorStorageType::TEXTURE_2D, TensorStorageType::TEXTURE_3D,
          TensorStorageType::TEXTURE_ARRAY};
}

absl::Status MetalExecutionEnvironment::ExecuteGpuOperationInternal(
    const std::vector<TensorDescriptor*>& src_cpu,
    const std::vector<TensorDescriptor*>& dst_cpu,
    std::unique_ptr<GPUOperation>&& operation) {
  const OperationDef& op_def = operation->GetDefinition();
  std::vector<MetalSpatialTensor> src(src_cpu.size());
  for (int i = 0; i < src_cpu.size(); ++i) {
    RETURN_IF_ERROR(src[i].CreateFromDescriptor(*src_cpu[i], device_.device()));
    operation->SetSrc(&src[i], i);
  }

  std::vector<MetalSpatialTensor> dst(dst_cpu.size());
  for (int i = 0; i < dst_cpu.size(); ++i) {
    TensorDescriptor descriptor_with_shape = op_def.dst_tensors[i];
    descriptor_with_shape.SetBHWDCShape(dst_cpu[i]->GetBHWDCShape());
    RETURN_IF_ERROR(
        CreateTensor(device_.device(), descriptor_with_shape, &dst[i]));
    operation->SetDst(&dst[i], i);
  }

  ComputeTask gpu_task;
  gpu_task.Init(std::move(operation));
  RETURN_IF_ERROR(gpu_task.Compile(&device_));
  for (int i = 0; i < src_cpu.size(); ++i) {
    gpu_task.SetSrcTensor(&src[i], i);
  }
  for (int i = 0; i < dst_cpu.size(); ++i) {
    gpu_task.SetDstTensor(&dst[i], i);
  }
  RETURN_IF_ERROR(gpu_task.UpdateParams());

  bool use_icb = false;
  if (use_icb) {
    if (@available(macOS 11.00, iOS 13.0, tvOS 13.0, *)) {
      MTLIndirectCommandBufferDescriptor* icb_desc =
          [[MTLIndirectCommandBufferDescriptor alloc] init];
      icb_desc.commandTypes = MTLIndirectCommandTypeConcurrentDispatch;
      icb_desc.inheritBuffers = NO;
      icb_desc.inheritPipelineState = NO;
      icb_desc.maxKernelBufferBindCount = 1;

      id<MTLIndirectCommandBuffer> icb =
          [device_.device() newIndirectCommandBufferWithDescriptor:icb_desc
                                                   maxCommandCount:1
                                                           options:0];

      id<MTLIndirectComputeCommand> icb_command =
          [icb indirectComputeCommandAtIndex:0];
      gpu_task.EncodeToICB(icb_command);
      [icb_command setBarrier];

      id<MTLCommandQueue> command_queue = [device_.device() newCommandQueue];
      id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
      id<MTLComputeCommandEncoder> encoder =
          [command_buffer computeCommandEncoder];
      gpu_task.AddResourcesToEncoder(encoder);
      [encoder executeCommandsInBuffer:icb withRange:NSMakeRange(0, 1)];
      [encoder endEncoding];
      [command_buffer commit];
      [command_buffer waitUntilCompleted];
    } else {
      return absl::InternalError(
          "Indirect compute command buffer available since ios 13");
    }
  } else {
    id<MTLCommandQueue> command_queue = [device_.device() newCommandQueue];
    id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder =
        [command_buffer computeCommandEncoder];
    gpu_task.Encode(encoder);
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
  }

  for (int i = 0; i < dst_cpu.size(); ++i) {
    RETURN_IF_ERROR(dst[i].ToDescriptor(dst_cpu[i], device_.device()));
  }
  return absl::OkStatus();
}

}  // namespace metal
}  // namespace gpu
}  // namespace tflite
