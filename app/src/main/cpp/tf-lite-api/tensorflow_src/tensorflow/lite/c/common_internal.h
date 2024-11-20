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
#ifndef TENSORFLOW_LITE_C_COMMON_INTERNAL_H_
#define TENSORFLOW_LITE_C_COMMON_INTERNAL_H_

#include "tensorflow/lite/c/c_api_types.h"
#include "tensorflow/lite/c/common.h"

// Internal structures and subroutines used by the C API. These are likely to
// change and should not be depended on directly by any C API clients.
//
// NOTE: This header does not follow C conventions and does not define a C API.
// It is effectively an (internal) implementation detail of the C API.

// `TfLiteRegistrationExternal` is an external version of `TfLiteRegistration`
// for C API which doesn't use internal types (such as `TfLiteContext`) but only
// uses stable API types (such as `TfLiteOpaqueContext`). The purpose of each
// field is the exactly the same as with `TfLiteRegistration`.
typedef struct TfLiteRegistrationExternal {
  // Custom op name.  This should be non-null iff the op is a custom op,
  // i.e. iff builtin_code is kTfLiteBuiltinCustom.
  const char* custom_name;

  // The version of the op. The version should be higher than 0.
  int version;

  // Initializes the op from serialized data.
  void* (*init)(void* data, TfLiteOpaqueContext* context, const char* buffer,
                size_t length);
  // The pointer `buffer` is the data previously returned by an init invocation.
  void (*free)(void* data, TfLiteOpaqueContext* context, void* buffer);

  // Called when the inputs that this node depends on have been resized.
  TfLiteStatus (*prepare)(void* data, TfLiteOpaqueContext* context,
                          TfLiteOpaqueNode* node);

  // Called when the node is executed. (should read node->inputs and output to
  // node->outputs).
  TfLiteStatus (*invoke)(void* data, TfLiteOpaqueContext* context,
                         TfLiteOpaqueNode* node);

  // Builtin op code.
  // The values stored in this field should be enum constants from the
  // TfLiteBuiltinOperator enum.
  // For custom ops, this should be the value kTfLiteBuiltinCustom.
  int32_t builtin_code;
  // Opaque data, meant to be supplied as the 'data' argument when 'init' is
  // invoked.
  void* init_data;
  // Opaque data, meant to be supplied as the 'data' argument when 'free' is
  // invoked.
  void* free_data;
  // Opaque data, meant to be supplied as the 'data' argument when 'prepare' is
  // invoked.
  void* prepare_data;
  // Opaque data, meant to be supplied as the 'data' argument when 'invoke' is
  // invoked.
  void* invoke_data;
} TfLiteRegistrationExternal;

// Returns true iff it's safe to dereference
// 'delegate->opaque_delegate_builder'.
inline bool TfLiteDelegateHasValidOpaqueDelegateBuilder(
    const TfLiteDelegate* delegate) {
  // We want to give precedence to the delegate's `opaque_delegate_builder`
  // field when it is available.  In an ideal setting, where all client code
  // properly initializes the delegate, we could simply check if the
  // `opaque_delegate_builder` contains a non-zero address.  However, in
  // practice this breaks code that doesn't adhere to these best practices.
  //
  // We can avoid this problem by checking the `Prepare` field contained in the
  // `TfliteDelegate` (not to be confused with the `Prepare` field contained in
  // `TfLiteOpaqueDelegateBuilder` struct). In order to tell if we should use
  // the `opaque_delegate_builder` field we check that the `TfLiteDelegate`'s
  // `Prepare` member is null.  This should be true for every delegate that
  // adopts the `TfLiteOpaqueDelegateBuilder` interface and should not be true
  // for any delegate implementation that is using `TfLiteDelegate` directly.
  //
  // TODO(b/245730811): Consider signalling to clients if the delegate is not
  // initialized cleanly.
  return delegate->Prepare == nullptr &&
         delegate->opaque_delegate_builder != nullptr;
}

// Invokes 'Prepare' on the provided 'delegate', giving the 'delegate' a view
// of the current graph through the provided 'context'.  Returns the delegate's
// 'Prepare' return value.
TfLiteStatus TfLiteDelegatePrepareInternal(TfLiteContext* context,
                                           TfLiteDelegate* delegate);

// Invokes 'CopyFromBufferHandle' on the provided 'delegate', supplying the
// provided 'buffer_handle' and 'tensor' as arguments. The provided
// 'buffer_handle' must have a non-null buffer handle value (i.e., not
// 'kTfLiteNullBufferHandle').  Returns the delegate's 'CopyFromBufferHandle'
// return value.
TfLiteStatus TfLiteDelegateCopyFromBufferHandleInternal(
    TfLiteContext* context, TfLiteDelegate* delegate,
    TfLiteBufferHandle buffer_handle, TfLiteTensor* tensor);

// Invokes 'FreeBufferHandle' on the provided 'delegate', supplying the provided
// 'buffer_handle' as an argument.  The '*buffer_handle' must have a non-null
// buffer handle value (i.e., not 'kTfLiteNullBufferHandle').  Returns
// 'kTfLiteOk' if 'FreeBufferHandle' was called, or 'kTfLiteError' if the
// callback is not available.
TfLiteStatus TfLiteDelegateFreeBufferHandleInternal(
    TfLiteContext* context, TfLiteDelegate* delegate,
    TfLiteBufferHandle* buffer_handle);

// Returns the 'delegate' flags value.  Note, if the delegate contains a valid
// opaque_delegate_builder field, then the flags of the delegate external are
// returned.  Otherwise, the flags field inside `TfLiteDelegate` is returned.
int64_t TfLiteDelegateGetFlagsInternal(TfLiteDelegate* delegate);

#endif  // TENSORFLOW_LITE_C_COMMON_INTERNAL_H_
