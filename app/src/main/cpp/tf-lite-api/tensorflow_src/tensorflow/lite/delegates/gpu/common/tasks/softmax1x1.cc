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

#include "tensorflow/lite/delegates/gpu/common/tasks/softmax1x1.h"

#include <string>
#include <utility>
#include <vector>

#include "tensorflow/lite/delegates/gpu/common/operations.h"
#include "tensorflow/lite/delegates/gpu/common/status.h"
#include "tensorflow/lite/delegates/gpu/common/task/util.h"

namespace tflite {
namespace gpu {
namespace {
std::string MakeAccOp(OperationType op_type, const std::string& a,
                      const std::string& b) {
  if (op_type == OperationType::ADD) {
    return a + " = " + a + " + " + b;
  } else if (op_type == OperationType::MAXIMUM) {
    return a + " = max(" + a + ", " + b + ")";
  } else {
    return a;
  }
}

std::string GetReduceCode(const std::string& value, OperationType op_type,
                          int group_reduction_size) {
  std::vector<int> stages;
  if (group_reduction_size == 1024) {
    stages = {8, 8, 4, 4};
  } else if (group_reduction_size == 512) {
    stages = {8, 8, 8};
  } else if (group_reduction_size == 256) {
    stages = {8, 8, 4};
  } else if (group_reduction_size == 128) {
    stages = {8, 4, 4};
  } else if (group_reduction_size == 64) {
    stages = {8, 8};
  } else if (group_reduction_size == 32) {
    stages = {8, 4};
  } else if (group_reduction_size == 16) {
    stages = {4, 4};
  } else if (group_reduction_size <= 8) {
    stages = {group_reduction_size};
  }
  std::string c;
  c += "  LOCAL_MEM_BARRIER;\n";
  c += "  loc_mem[tid] = " + value + ";\n";
  int stride = 1;
  for (int i = 0; i < stages.size(); ++i) {
    const bool last_stage = i == stages.size() - 1;
    const std::string condition =
        last_stage ? "tid == 0"
                   : "tid % " + std::to_string(stride * stages[i]) + " == 0";
    const std::string location = last_stage ? "loc_mem[0]" : "loc_mem[tid]";
    c += "  LOCAL_MEM_BARRIER;\n";
    c += "  if (" + condition + ") {\n";
    for (int j = 1; j < stages[i]; ++j) {
      c += "    " +
           MakeAccOp(op_type, value,
                     "loc_mem[tid + " + std::to_string(stride * j) + "]") +
           ";\n";
    }
    c += "    " + location + " = " + value + ";\n";
    c += "  }\n";
    stride *= stages[i];
  }
  c += "  LOCAL_MEM_BARRIER;\n";
  c += "  " + value + " = loc_mem[0];\n";
  return c;
}
}  // namespace

Softmax1x1::Softmax1x1(const OperationDef& definition, const GpuInfo& gpu_info,
                       const BHWC& shape)
    : GPUOperation(definition) {
  // work_group_size_.x must be power of 2 up to 1024
  if (gpu_info.IsAdreno()) {
    work_group_size_ = int3(512, 1, 1);
  } else if (gpu_info.IsMali()) {
    work_group_size_ = int3(1024, 1, 1);
  } else {
    work_group_size_ = int3(256, 1, 1);
  }
  const int slices = DivideRoundUp(shape.c, 4);
  while (work_group_size_.x >= slices * 2) {
    work_group_size_.x /= 2;
  }
  if (gpu_info.IsAdreno()) {
    while (work_group_size_.x >= gpu_info.GetMaxWorkGroupSizeForX()) {
      work_group_size_.x /= 2;
    }
  } else {
    while (work_group_size_.x > gpu_info.GetMaxWorkGroupSizeForX()) {
      work_group_size_.x /= 2;
    }
  }
  code_ = GetSoftmaxKernelCode(definition_);
}

Softmax1x1::Softmax1x1(Softmax1x1&& kernel) : GPUOperation(std::move(kernel)) {}

Softmax1x1& Softmax1x1::operator=(Softmax1x1&& kernel) {
  if (this != &kernel) {
    GPUOperation::operator=(std::move(kernel));
  }
  return *this;
}

std::string Softmax1x1::GetSoftmaxKernelCode(const OperationDef& op_def) {
  AddSrcTensor("src_tensor", op_def.src_tensors[0]);
  AddDstTensor("dst_tensor", op_def.dst_tensors[0]);
  args_.AddFloat("mask_x");
  args_.AddFloat("mask_y");
  args_.AddFloat("mask_z");
  args_.AddFloat("mask_w");

  std::string c;
  c += "MAIN_FUNCTION($0) {\n";
  if (op_def.dst_tensors[0].HasAxis(Axis::BATCH)) {
    c += "  int linear_id = GROUP_ID_1;\n";
    c += "  int X = linear_id / args.dst_tensor.Batch();\n";
    c += "  int B = linear_id % args.dst_tensor.Batch();\n";
    c += "  if (B >= args.dst_tensor.Batch()) return;\n";
    c += "  args.src_tensor.SetBatchRef(B);\n";
    c += "  args.dst_tensor.SetBatchRef(B);\n";
  } else {
    c += "  int X = GROUP_ID_1;\n";
  }
  c += "  int Y = GROUP_ID_2;\n";
  c += "  if (X >= args.dst_tensor.Width()) return;\n";
  c += "  if (Y >= args.dst_tensor.Height()) return;\n";
  c += "  float4 mask = INIT_FLOAT4v4(args.mask_x, args.mask_y, args.mask_z, "
       "args.mask_w);\n";
  c +=
      "  float4 maxx4 = INIT_FLOAT4(args.src_tensor.Read<float>(X, Y, 0).x);\n";
  c += "  int tid = LOCAL_ID_0;\n";
  const int group_reduction_size = work_group_size_.x;
  c += "  for (int s = tid; s < args.src_tensor.Slices(); s += " +
       std::to_string(group_reduction_size) + ") {\n";
  c += "    float4 mask_a = s == args.src_tensor.Slices() - 1 ? mask : "
       "INIT_FLOAT4(1.0f);\n";
  c += "    float4 mask_b = INIT_FLOAT4(1.0f) - mask_a;\n";
  c += "    float4 src = args.src_tensor.Read<float>(X, Y, s);\n";
  c += "    src = src * mask_a + mask_b * src.x;\n";
  c += "    maxx4 = max(maxx4, src);\n";
  c += "  }\n";
  c += "  float maximum = max(maxx4.x, maxx4.y);\n";
  c += "  maximum = max(maximum, maxx4.z);\n";
  c += "  maximum = max(maximum, maxx4.w);\n";
  c += "  __local float loc_mem[" + std::to_string(group_reduction_size) +
       "];\n";
  c += GetReduceCode("maximum", OperationType::MAXIMUM, group_reduction_size);
  c += "  float sum = 0.0f;\n";
  c += "  for (int s = tid; s < args.src_tensor.Slices(); s += " +
       std::to_string(group_reduction_size) + ") {\n";
  c += "    float4 mask_temp = s == args.src_tensor.Slices() - 1 ? mask : "
       "INIT_FLOAT4(1.0f);\n";
  c += "    float4 src = args.src_tensor.Read<float>(X, Y, s) - "
       "INIT_FLOAT4(maximum);\n";
  c += "    sum += dot(mask_temp, exp(src));\n";
  c += "  }\n";
  c += GetReduceCode("sum", OperationType::ADD, group_reduction_size);
  c += "  sum = 1.0f / sum;\n";
  c += "  int dst_s = GLOBAL_ID_0;\n";
  c += "  if (dst_s < args.dst_tensor.Slices()) {\n";
  c += "    float4 src = args.src_tensor.Read<float>(X, Y, dst_s) - "
       "INIT_FLOAT4(maximum);\n";
  c += "    FLT4 res = TO_FLT4(exp(src) * sum);\n";
  c += "    args.dst_tensor.Write(res, X, Y, dst_s);\n";
  c += "  }\n";
  c += "}\n";
  return c;
}

absl::Status Softmax1x1::BindArguments(ArgumentsBinder* args) {
  float4 mask = GetMaskForLastPlane(src_[0]->Channels());
  RETURN_IF_ERROR(args->SetFloat("mask_x", mask.x));
  RETURN_IF_ERROR(args->SetFloat("mask_y", mask.y));
  RETURN_IF_ERROR(args->SetFloat("mask_z", mask.z));
  RETURN_IF_ERROR(args->SetFloat("mask_w", mask.w));
  return absl::OkStatus();
}

int3 Softmax1x1::GetGridSize() const {
  return int3(dst_[0]->Slices(), dst_[0]->Width() * dst_[0]->Batch(),
              dst_[0]->Height());
}

Softmax1x1 CreateSoftmax1x1(const OperationDef& definition,
                            const GpuInfo& gpu_info, const BHWC& shape) {
  return Softmax1x1(definition, gpu_info, shape);
}

}  // namespace gpu
}  // namespace tflite
