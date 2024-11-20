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

#ifndef TENSORFLOW_LITE_DELEGATES_GPU_COMMON_TASKS_DEPTHWISE_CONV_H_
#define TENSORFLOW_LITE_DELEGATES_GPU_COMMON_TASKS_DEPTHWISE_CONV_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "tensorflow/lite/delegates/gpu/common/data_type.h"
#include "tensorflow/lite/delegates/gpu/common/operations.h"
#include "tensorflow/lite/delegates/gpu/common/shape.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"
#include "tensorflow/lite/delegates/gpu/common/task/buffer_desc.h"
#include "tensorflow/lite/delegates/gpu/common/task/gpu_operation.h"
#include "tensorflow/lite/delegates/gpu/common/tensor.h"
#include "tensorflow/lite/delegates/gpu/common/types.h"

namespace tflite {
namespace gpu {

class DepthwiseConv : public GPUOperation {
 public:
  int3 GetGridSize() const override;
  void GetPossibleKernelWorkGroups(
      TuningType tuning_type, const GpuInfo& gpu_info,
      const KernelInfo& kernel_info,
      std::vector<int3>* work_groups) const override;

  // Move only
  DepthwiseConv(DepthwiseConv&& operation) = default;
  DepthwiseConv& operator=(DepthwiseConv&& operation) = default;
  DepthwiseConv(const DepthwiseConv&) = delete;
  DepthwiseConv& operator=(const DepthwiseConv&) = delete;

  friend DepthwiseConv CreateDepthwiseConvolution2D(
      const GpuInfo& gpu_info, const OperationDef& definition,
      const DepthwiseConvolution2DAttributes& attr);

  friend DepthwiseConv CreateDepthwiseConvolution2DDynamicWeights(
      const GpuInfo& gpu_info, const OperationDef& definition,
      const DepthwiseConvolution2DAttributes& attr);

  friend DepthwiseConv CreateDepthwiseConvolution3D(
      const GpuInfo& gpu_info, const OperationDef& definition,
      const DepthwiseConvolution3DAttributes& attr);

 private:
  struct DepthwiseConvParams {
    bool UseLocalMem() const {
      return use_weights_caching || use_spatial_caching;
    }
    int GetKernelsTotalSize() const {
      return x_kernel_size * y_kernel_size * z_kernel_size;
    }
    int GetWorkGroupTotalSize() const {
      return work_group_size.x * work_group_size.y * work_group_size.z;
    }
    int channel_multiplier;
    // Supportd only tensors with Width & Height spatial dimensions
    // optional, if true, spatial dims will be uploaded to local mem
    bool use_spatial_caching = false;
    // optional, if true, weights will be uploaded to local memory
    bool use_weights_caching = false;
    // optional, if UsesLocalMem() return true this field must be initialized
    int3 work_group_size = int3(1, 1, 1);

    // optional, if UsesLocalMem() return true this field must be initialized
    int x_kernel_size = 1;
    // optional, if UsesLocalMem() return true this field must be initialized
    int y_kernel_size = 1;
    // optional, if UsesLocalMem() return true this field must be initialized
    int z_kernel_size = 1;

    // optional, if use_spatial_caching true this field must be initialized
    int x_dilation_size = 1;
    // optional, if use_spatial_caching true this field must be initialized
    int y_dilation_size = 1;
    // optional, if use_spatial_caching true this field must be initialized
    int z_dilation_size = 1;
  };

  explicit DepthwiseConv(const OperationDef& definition,
                         const DepthwiseConvParams& params);

  std::string GenerateSrcUpload(const GpuInfo& gpu_info);
  std::string GenerateWeightsUpload(const GpuInfo& gpu_info);
  std::string GenerateCode(const GpuInfo& gpu_info);

  template <DataType T>
  void UploadWeightsForDWConv2D(const tflite::gpu::Tensor<OHWI, T>& weights,
                                bool weights_are_buffer);

  template <DataType T>
  void UploadWeightsForDWConv3D(const tflite::gpu::Tensor<OHWDI, T>& weights,
                                bool weights_are_buffer);

  DepthwiseConvParams params_;
};

template <DataType S, typename T>
void RearrangeWeightsForDWConv2D(const tflite::gpu::Tensor<OHWI, S>& weights,
                                 absl::Span<T> dst) {
  const int dst_channels = weights.shape.i * weights.shape.o;
  const int dst_depth = DivideRoundUp(dst_channels, 4);
  const int kernel_x = weights.shape.w;
  const int kernel_y = weights.shape.h;

  int counter = 0;
  for (int d = 0; d < dst_depth; ++d) {
    for (int y = 0; y < kernel_y; ++y) {
      for (int x = 0; x < kernel_x; ++x) {
        T filter_val;
        for (int i = 0; i < 4; ++i) {
          const int d_ch = d * 4 + i;
          if (d_ch < dst_channels) {
            const int f_index = weights.shape.LinearIndex(
                {d_ch % weights.shape.o, y, x, d_ch / weights.shape.o});
            filter_val[i] = weights.data[f_index];
          } else {
            filter_val[i] = 0.0f;
          }
        }
        dst[counter++] = filter_val;
      }
    }
  }
}

template <DataType T>
void DepthwiseConv::UploadWeightsForDWConv2D(
    const tflite::gpu::Tensor<OHWI, T>& weights, bool weights_are_buffer) {
  const int dst_channels = weights.shape.i * weights.shape.o;
  const int dst_slices = DivideRoundUp(dst_channels, 4);
  const int kernel_x = weights.shape.w;
  const int kernel_y = weights.shape.h;

  const int elements_count = kernel_x * kernel_y * dst_slices;

  const bool fp32_weights = definition_.precision == CalculationsPrecision::F32;
  const int float4_size = fp32_weights ? 16 : 8;

  std::vector<uint8_t> data(float4_size * elements_count);

  if (fp32_weights) {
    float4* ptr = reinterpret_cast<float4*>(data.data());
    RearrangeWeightsForDWConv2D(weights, absl::MakeSpan(ptr, elements_count));
  } else {
    half4* ptr = reinterpret_cast<half4*>(data.data());
    RearrangeWeightsForDWConv2D(weights, absl::MakeSpan(ptr, elements_count));
  }

  if (weights_are_buffer) {
    BufferDescriptor desc;
    desc.element_type = fp32_weights ? DataType::FLOAT32 : DataType::FLOAT16;
    desc.element_size = 4;
    desc.size = float4_size * elements_count;
    desc.data = std::move(data);
    args_.AddObject("weights", std::make_unique<BufferDescriptor>(desc));
  } else {
    TensorDescriptor desc = CreateConstantHWVec4TensorDescriptor(
        fp32_weights ? DataType::FLOAT32 : DataType::FLOAT16,
        TensorStorageType::TEXTURE_2D, kernel_x * kernel_y, dst_slices,
        data.data());
    args_.AddObject("weights", std::make_unique<TensorDescriptor>(desc));
  }
}

template <DataType S, typename T>
void RearrangeWeightsForDWConv3D(const tflite::gpu::Tensor<OHWDI, S>& weights,
                                 absl::Span<T> dst) {
  const int dst_channels = weights.shape.i * weights.shape.o;
  const int dst_slices = DivideRoundUp(dst_channels, 4);
  const int kernel_x = weights.shape.w;
  const int kernel_y = weights.shape.h;
  const int kernel_z = weights.shape.d;

  int counter = 0;
  for (int d = 0; d < dst_slices; ++d) {
    for (int z = 0; z < kernel_z; ++z) {
      for (int y = 0; y < kernel_y; ++y) {
        for (int x = 0; x < kernel_x; ++x) {
          T filter_val;
          for (int i = 0; i < 4; ++i) {
            const int d_ch = d * 4 + i;
            if (d_ch < dst_channels) {
              const int f_index = weights.shape.LinearIndex(
                  {d_ch % weights.shape.o, y, x, z, d_ch / weights.shape.o});
              filter_val[i] = weights.data[f_index];
            } else {
              filter_val[i] = 0.0f;
            }
          }
          dst[counter++] = filter_val;
        }
      }
    }
  }
}

template <DataType T>
void DepthwiseConv::UploadWeightsForDWConv3D(
    const tflite::gpu::Tensor<OHWDI, T>& weights, bool weights_are_buffer) {
  const int dst_channels = weights.shape.i * weights.shape.o;
  const int dst_slices = DivideRoundUp(dst_channels, 4);
  const int kernel_x = weights.shape.w;
  const int kernel_y = weights.shape.h;
  const int kernel_z = weights.shape.d;

  const int elements_count = kernel_x * kernel_y * kernel_z * dst_slices;

  const bool fp32_weights = definition_.precision == CalculationsPrecision::F32;
  const int float4_size = fp32_weights ? 16 : 8;

  std::vector<uint8_t> data(float4_size * elements_count);

  if (fp32_weights) {
    float4* ptr = reinterpret_cast<float4*>(data.data());
    RearrangeWeightsForDWConv3D(weights, absl::MakeSpan(ptr, elements_count));
  } else {
    half4* ptr = reinterpret_cast<half4*>(data.data());
    RearrangeWeightsForDWConv3D(weights, absl::MakeSpan(ptr, elements_count));
  }

  if (weights_are_buffer) {
    BufferDescriptor desc;
    desc.element_type = fp32_weights ? DataType::FLOAT32 : DataType::FLOAT16;
    desc.element_size = 4;
    desc.size = float4_size * elements_count;
    desc.data = std::move(data);
    args_.AddObject("weights",
                    std::make_unique<BufferDescriptor>(std::move(desc)));
  } else {
    TensorDescriptor desc = CreateConstantHWVec4TensorDescriptor(
        fp32_weights ? DataType::FLOAT32 : DataType::FLOAT16,
        TensorStorageType::TEXTURE_2D, kernel_x * kernel_y * kernel_z,
        dst_slices, data.data());
    args_.AddObject("weights", std::make_unique<TensorDescriptor>(desc));
  }
}

DepthwiseConv CreateDepthwiseConvolution2D(
    const GpuInfo& gpu_info, const OperationDef& definition,
    const DepthwiseConvolution2DAttributes& attr);

DepthwiseConv CreateDepthwiseConvolution2DDynamicWeights(
    const GpuInfo& gpu_info, const OperationDef& definition,
    const DepthwiseConvolution2DAttributes& attr);

DepthwiseConv CreateDepthwiseConvolution3D(
    const GpuInfo& gpu_info, const OperationDef& definition,
    const DepthwiseConvolution3DAttributes& attr);

}  // namespace gpu
}  // namespace tflite

#endif  // TENSORFLOW_LITE_DELEGATES_GPU_COMMON_TASKS_DEPTHWISE_CONV_H_
