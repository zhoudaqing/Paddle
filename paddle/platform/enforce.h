/* Copyright (c) 2016 PaddlePaddle Authors. All Rights Reserve.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License. */

#pragma once

#include <execinfo.h>
#include <paddle/string/printf.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef PADDLE_ONLY_CPU

#include "paddle/platform/dynload/cublas.h"
#include "paddle/platform/dynload/cudnn.h"
#include "paddle/platform/dynload/curand.h"

#include <cublas_v2.h>
#include <cudnn.h>
#include <curand.h>
#include <thrust/system/cuda/error.h>
#include <thrust/system_error.h>

#endif  // PADDLE_ONLY_CPU

namespace paddle {
namespace platform {

struct EnforceNotMet : public std::exception {
  std::exception_ptr exp_;
  std::string err_str_;
  EnforceNotMet(std::exception_ptr e, const char* f, int l) : exp_(e) {
    static constexpr int TRACE_STACK_LIMIT = 100;
    try {
      std::rethrow_exception(exp_);
    } catch (const std::exception& exp) {
      std::ostringstream sout;
      sout << string::Sprintf("%s at [%s:%d]", exp.what(), f, l) << std::endl;
      sout << "Call Stacks: " << std::endl;
      void* call_stack[TRACE_STACK_LIMIT];
      int sz = backtrace(call_stack, TRACE_STACK_LIMIT);
      auto line = backtrace_symbols(call_stack, sz);
      for (int i = 0; i < sz; ++i) {
        sout << line[i] << std::endl;
      }
      free(line);
      err_str_ = sout.str();
    }
  }

  const char* what() const noexcept { return err_str_.c_str(); }
};

// Because most enforce conditions would evaluate to true, we can use
// __builtin_expect to instruct the C++ compiler to generate code that
// always forces branch prediction of true.
// This generates faster binary code. __builtin_expect is since C++11.
// For more details, please check https://stackoverflow.com/a/43870188/724872.
#define UNLIKELY(condition) __builtin_expect(static_cast<bool>(condition), 0)

template <typename... Args>
inline typename std::enable_if<sizeof...(Args) != 0, void>::type throw_on_error(
    int stat, const Args&... args) {
  if (UNLIKELY(!(stat))) {
    throw std::runtime_error(string::Sprintf(args...));
  }
}

#ifndef PADDLE_ONLY_CPU

template <typename... Args>
inline typename std::enable_if<sizeof...(Args) != 0, void>::type throw_on_error(
    cudaError_t e, const Args&... args) {
  if (UNLIKELY(e)) {
    throw thrust::system_error(e, thrust::cuda_category(),
                               string::Sprintf(args...));
  }
}

template <typename... Args>
inline typename std::enable_if<sizeof...(Args) != 0, void>::type throw_on_error(
    curandStatus_t stat, const Args&... args) {
  if (stat != CURAND_STATUS_SUCCESS) {
    throw thrust::system_error(cudaErrorLaunchFailure, thrust::cuda_category(),
                               string::Sprintf(args...));
  }
}

template <typename... Args>
inline typename std::enable_if<sizeof...(Args) != 0, void>::type throw_on_error(
    cudnnStatus_t stat, const Args&... args) {
  if (stat == CUDNN_STATUS_SUCCESS) {
    return;
  } else {
    throw std::runtime_error(platform::dynload::cudnnGetErrorString(stat) +
                             string::Sprintf(args...));
  }
}

template <typename... Args>
inline typename std::enable_if<sizeof...(Args) != 0, void>::type throw_on_error(
    cublasStatus_t stat, const Args&... args) {
  std::string err;
  if (stat == CUBLAS_STATUS_SUCCESS) {
    return;
  } else if (stat == CUBLAS_STATUS_NOT_INITIALIZED) {
    err = "CUBLAS: not initialized, ";
  } else if (stat == CUBLAS_STATUS_ALLOC_FAILED) {
    err = "CUBLAS: alloc failed, ";
  } else if (stat == CUBLAS_STATUS_INVALID_VALUE) {
    err = "CUBLAS: invalid value, ";
  } else if (stat == CUBLAS_STATUS_ARCH_MISMATCH) {
    err = "CUBLAS: arch mismatch, ";
  } else if (stat == CUBLAS_STATUS_MAPPING_ERROR) {
    err = "CUBLAS: mapping error, ";
  } else if (stat == CUBLAS_STATUS_EXECUTION_FAILED) {
    err = "CUBLAS: execution failed, ";
  } else if (stat == CUBLAS_STATUS_INTERNAL_ERROR) {
    err = "CUBLAS: internal error, ";
  } else if (stat == CUBLAS_STATUS_NOT_SUPPORTED) {
    err = "CUBLAS: not supported, ";
  } else if (stat == CUBLAS_STATUS_LICENSE_ERROR) {
    err = "CUBLAS: license error, ";
  }
  throw std::runtime_error(err + string::Sprintf(args...));
}

#endif  // PADDLE_ONLY_CPU

template <typename T>
inline void throw_on_error(T e) {
  throw_on_error(e, "");
}

#define PADDLE_THROW(...)                                              \
  do {                                                                 \
    throw ::paddle::platform::EnforceNotMet(                           \
        std::make_exception_ptr(                                       \
            std::runtime_error(paddle::string::Sprintf(__VA_ARGS__))), \
        __FILE__, __LINE__);                                           \
  } while (0)

#define PADDLE_ENFORCE(...)                                             \
  do {                                                                  \
    try {                                                               \
      ::paddle::platform::throw_on_error(__VA_ARGS__);                  \
    } catch (...) {                                                     \
      throw ::paddle::platform::EnforceNotMet(std::current_exception(), \
                                              __FILE__, __LINE__);      \
    }                                                                   \
  } while (0)

/*
 * Some enforce helpers here, usage:
 *    int a = 1;
 *    int b = 2;
 *    PADDLE_ENFORCE_EQ(a, b);
 *
 *    will raise an expression described as follows:
 *    "enforce a == b failed, 1 != 2" with detailed stack infomation.
 *
 *    extra messages is also supported, for example:
 *    PADDLE_ENFORCE(a, b, "some simple enforce failed between %d numbers", 2)
 */

#define PADDLE_ENFORCE_EQ(__VAL0, __VAL1, ...) \
  __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, ==, !=, __VA_ARGS__)
#define PADDLE_ENFORCE_NE(__VAL0, __VAL1, ...) \
  __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, !=, ==, __VA_ARGS__)
#define PADDLE_ENFORCE_GT(__VAL0, __VAL1, ...) \
  __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, >, <=, __VA_ARGS__)
#define PADDLE_ENFORCE_GE(__VAL0, __VAL1, ...) \
  __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, >=, <, __VA_ARGS__)
#define PADDLE_ENFORCE_LT(__VAL0, __VAL1, ...) \
  __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, <, >=, __VA_ARGS__)
#define PADDLE_ENFORCE_LE(__VAL0, __VAL1, ...) \
  __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, <=, >, __VA_ARGS__)

// if two values have different data types, choose a compatible type for them.
template <typename T1, typename T2>
struct CompatibleType {
  static const bool t1_to_t2 = std::is_convertible<T1, T2>::value;
  typedef typename std::conditional<t1_to_t2, T2, T1>::type type;
};

#define __PADDLE_BINARY_COMPARE(__VAL0, __VAL1, __CMP, __INV_CMP, ...)        \
  PADDLE_ENFORCE(__COMPATIBLE_TYPE(__VAL0, __VAL1, __VAL0)                    \
                     __CMP __COMPATIBLE_TYPE(__VAL0, __VAL1, __VAL1),         \
                 "enforce %s " #__CMP " %s failed, %s " #__INV_CMP " %s\n%s", \
                 #__VAL0, #__VAL1, std::to_string(__VAL0),                    \
                 std::to_string(__VAL1),                                      \
                 paddle::string::Sprintf("" __VA_ARGS__));

#define __COMPATIBLE_TYPE(__VAL0, __VAL1, __VAL)              \
  typename paddle::platform::CompatibleType<decltype(__VAL0), \
                                            decltype(__VAL1)>::type(__VAL)

}  // namespace platform
}  // namespace paddle
