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
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/c/c_api.h"

#include <stdint.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "flatbuffers/base.h"  // from @flatbuffers
#include "flatbuffers/buffer.h"  // from @flatbuffers
#include "flatbuffers/flatbuffer_builder.h"  // from @flatbuffers
#include "flatbuffers/flatbuffers.h"  // from @flatbuffers
#include "flatbuffers/vector.h"  // from @flatbuffers
#include "tensorflow/lite/core/api/error_reporter.h"
#include "tensorflow/lite/experimental/acceleration/configuration/configuration_generated.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/embedded_mobilenet_model.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/embedded_mobilenet_validation_model.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/mini_benchmark_test_helper.h"
#include "tensorflow/lite/experimental/acceleration/mini_benchmark/status_codes.h"

namespace {

using ::testing::_;

std::vector<const tflite::BenchmarkEvent*> ToBenchmarkEvents(uint8_t* data,
                                                             size_t size) {
  std::vector<const tflite::BenchmarkEvent*> results;
  uint8_t* current_root = data;
  while (current_root < data + size) {
    flatbuffers::uoffset_t current_size =
        flatbuffers::GetPrefixedSize(current_root);
    results.push_back(
        flatbuffers::GetSizePrefixedRoot<tflite::BenchmarkEvent>(current_root));
    current_root += current_size + sizeof(flatbuffers::uoffset_t);
  }
  return results;
}

class MockErrorReporter {
 public:
  static int InvokeErrorReporter(void* user_data, const char* format,
                                 va_list args) {
    MockErrorReporter* error_reporter =
        static_cast<MockErrorReporter*>(user_data);
    return error_reporter->Log(format, args);
  }
  MOCK_METHOD(int, Log, (const char* format, va_list args));
};

class CApiTest : public ::testing::Test {
 protected:
  void SetUp() override {
    tflite::acceleration::MiniBenchmarkTestHelper helper;
    should_perform_test_ = helper.should_perform_test();

    if (!should_perform_test_) {
      return;
    }
    embedded_model_path_ = helper.DumpToTempFile(
        "mobilenet_quant_with_validation.tflite",
        g_tflite_acceleration_embedded_mobilenet_validation_model,
        g_tflite_acceleration_embedded_mobilenet_validation_model_len);
    ASSERT_TRUE(!embedded_model_path_.empty());

    plain_model_path_ = helper.DumpToTempFile(
        "mobilenet_quant.tflite",
        g_tflite_acceleration_embedded_mobilenet_model,
        g_tflite_acceleration_embedded_mobilenet_model_len);
  }

  flatbuffers::Offset<tflite::BenchmarkStoragePaths> CreateStoragePaths() {
    return tflite::CreateBenchmarkStoragePaths(
        mini_benchmark_fbb_,
        mini_benchmark_fbb_.CreateString(::testing::TempDir() +
                                         "/storage_path.fb"),
        mini_benchmark_fbb_.CreateString(::testing::TempDir()));
  }

  flatbuffers::Offset<tflite::ValidationSettings> CreateValidationSettings() {
    return tflite::CreateValidationSettings(mini_benchmark_fbb_, 5000);
  }

  flatbuffers::Offset<tflite::ModelFile> CreateModelFile(
      const std::string& model_path) {
    return tflite::CreateModelFile(
        mini_benchmark_fbb_, mini_benchmark_fbb_.CreateString(model_path));
  }

  flatbuffers::Offset<
      flatbuffers::Vector<flatbuffers::Offset<tflite::TFLiteSettings>>>
  CreateTFLiteSettings() {
    return mini_benchmark_fbb_.CreateVector(
        {tflite::CreateTFLiteSettings(mini_benchmark_fbb_)});
  }

  flatbuffers::FlatBufferBuilder mini_benchmark_fbb_;
  std::string embedded_model_path_;
  std::string plain_model_path_;
  bool should_perform_test_ = true;
};

TEST_F(CApiTest, SucceedWithEmbeddedValidation) {
  if (!should_perform_test_) {
    std::cerr << "Skipping test";
    return;
  }

  mini_benchmark_fbb_.Finish(tflite::CreateMinibenchmarkSettings(
      mini_benchmark_fbb_, CreateTFLiteSettings(),
      CreateModelFile(embedded_model_path_), CreateStoragePaths(),
      CreateValidationSettings()));
  TfLiteMiniBenchmarkSettings settings{mini_benchmark_fbb_.GetBufferPointer(),
                                       mini_benchmark_fbb_.GetSize()};

  TfLiteMiniBenchmarkResult* result =
      TfLiteBlockingValidatorRunnerTriggerValidation(&settings);
  std::vector<const tflite::BenchmarkEvent*> events =
      ToBenchmarkEvents(result->flatbuffer_data, result->flatbuffer_data_size);

  EXPECT_THAT(result->init_status, tflite::acceleration::kMinibenchmarkSuccess);
  EXPECT_THAT(events, testing::Not(testing::IsEmpty()));
  for (auto& event : events) {
    EXPECT_EQ(event->event_type(), tflite::BenchmarkEventType_END);
    EXPECT_TRUE(event->result()->ok());
  }
  TfLiteMiniBenchmarkResultFree(result);
}

TEST_F(CApiTest, SucceedWithCustomValidation) {
  if (!should_perform_test_) {
    std::cerr << "Skipping test";
    return;
  }

  const int batch_size = 5;
  size_t input_size[] = {batch_size * 224 * 224 * 3};
  std::vector<uint8_t> custom_input_data(input_size[0], 1);
  mini_benchmark_fbb_.Finish(tflite::CreateMinibenchmarkSettings(
      mini_benchmark_fbb_, CreateTFLiteSettings(),
      CreateModelFile(plain_model_path_), CreateStoragePaths(),
      CreateValidationSettings()));
  TfLiteMiniBenchmarkSettings settings{
      mini_benchmark_fbb_.GetBufferPointer(), mini_benchmark_fbb_.GetSize(),
      TfLiteMiniBenchmarkCustomValidationInfo{/*batch_size=*/batch_size,
                                              /*num_input=*/1,
                                              /*buffer_dim=*/input_size,
                                              custom_input_data.data()}};

  TfLiteMiniBenchmarkResult* result =
      TfLiteBlockingValidatorRunnerTriggerValidation(&settings);
  std::vector<const tflite::BenchmarkEvent*> events =
      ToBenchmarkEvents(result->flatbuffer_data, result->flatbuffer_data_size);

  EXPECT_THAT(result->init_status, tflite::acceleration::kMinibenchmarkSuccess);

  EXPECT_THAT(events, testing::Not(testing::IsEmpty()));
  for (auto& event : events) {
    EXPECT_EQ(event->event_type(), tflite::BenchmarkEventType_END);
  }
  TfLiteMiniBenchmarkResultFree(result);
}

TEST_F(CApiTest, ReturnFailStatusWhenModelPathInvalid) {
  mini_benchmark_fbb_.Finish(tflite::CreateMinibenchmarkSettings(
      mini_benchmark_fbb_, CreateTFLiteSettings(),
      CreateModelFile("invalid/path"), CreateStoragePaths(),
      CreateValidationSettings()));
  TfLiteMiniBenchmarkSettings settings{
      mini_benchmark_fbb_.GetBufferPointer(),
      mini_benchmark_fbb_.GetSize(),
  };

  TfLiteMiniBenchmarkResult* result =
      TfLiteBlockingValidatorRunnerTriggerValidation(&settings);

  EXPECT_THAT(result->init_status,
              tflite::acceleration::kMinibenchmarkModelBuildFailed);
  EXPECT_EQ(result->flatbuffer_data, nullptr);
  EXPECT_EQ(result->flatbuffer_data_size, 0);
  TfLiteMiniBenchmarkResultFree(result);
}

TEST_F(CApiTest, ReturnEmptyWhenTestTimedOut) {
  if (!should_perform_test_) {
    std::cerr << "Skipping test";
    return;
  }

  mini_benchmark_fbb_.Finish(tflite::CreateMinibenchmarkSettings(
      mini_benchmark_fbb_, CreateTFLiteSettings(),
      CreateModelFile(embedded_model_path_), CreateStoragePaths(),
      tflite::CreateValidationSettings(mini_benchmark_fbb_,
                                       /*per_test_timeout_ms=*/2)));
  TfLiteMiniBenchmarkSettings settings{
      mini_benchmark_fbb_.GetBufferPointer(),
      mini_benchmark_fbb_.GetSize(),
  };

  TfLiteMiniBenchmarkResult* result =
      TfLiteBlockingValidatorRunnerTriggerValidation(&settings);

  EXPECT_THAT(result->init_status, tflite::acceleration::kMinibenchmarkSuccess);
  EXPECT_EQ(result->flatbuffer_data, nullptr);
  EXPECT_EQ(result->flatbuffer_data_size, 0);
  TfLiteMiniBenchmarkResultFree(result);
}

TEST_F(CApiTest, UseProvidedErrorReporterWhenFail) {
  mini_benchmark_fbb_.Finish(tflite::CreateMinibenchmarkSettings(
      mini_benchmark_fbb_, CreateTFLiteSettings(), 0, 0, 0));
  TfLiteMiniBenchmarkSettings settings{
      mini_benchmark_fbb_.GetBufferPointer(),
      mini_benchmark_fbb_.GetSize(),
  };
  MockErrorReporter reporter;
  EXPECT_CALL(reporter, Log(_, _)).Times(testing::AtLeast(1));
  settings.error_reporter_user_data = &reporter;
  settings.error_reporter_func = &MockErrorReporter::InvokeErrorReporter;

  TfLiteMiniBenchmarkResult* result =
      TfLiteBlockingValidatorRunnerTriggerValidation(&settings);
  EXPECT_THAT(result->init_status,
              tflite::acceleration::kMinibenchmarkPreconditionNotMet);
  TfLiteMiniBenchmarkResultFree(result);
}

TEST_F(CApiTest, ReturnFailStatusWhenSettingsCorrupted) {
  std::vector<uint8_t> settings_corrupted(10, 1);
  TfLiteMiniBenchmarkSettings settings{settings_corrupted.data(),
                                       settings_corrupted.size()};
  TfLiteMiniBenchmarkResult* result =
      TfLiteBlockingValidatorRunnerTriggerValidation(&settings);

  EXPECT_THAT(
      result->init_status,
      tflite::acceleration::kMinibenchmarkCorruptSizePrefixedFlatbufferFile);
  TfLiteMiniBenchmarkResultFree(result);
}
}  // namespace
