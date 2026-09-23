// ZTools Provider 桥接层
//
// 让原生层（任意 native 线程上的 C++/Swift 代码）能够调用 JS 侧注册的方法，
// 典型用途是调用宿主应用（如 ZTools 主进程）提供的 provider 能力（翻译 / OCR 等）。
//
// 工作方式：
// 1. JS 侧调用 startProviderBridge(dispatcher) 注册分发函数，签名为
//    (type: string, inputJson: string, seq: number) => void，
//    JS 负责执行实际逻辑，并按 seq 通过 resolveProviderBridge / rejectProviderBridge 回传结果；
// 2. 原生线程调用 InvokeProvider(type, inputJson, timeoutMs) 发起调用：
//    请求经线程安全函数调度到 JS 线程执行，原生线程阻塞等待结果（带超时）；
// 3. JS 执行完成后回传结果 JSON 字符串，原生侧拿到 InvokeResult。
//
// 线程约束：
// - InvokeProvider 严禁在 JS 主线程调用（阻塞等待会与调度回调互相等待造成死锁），
//   内部检测到 JS 线程调用时立即返回错误；
// - 其余导出函数（start / stop / resolve / reject 等）按常规 N-API 导出在 JS 线程上调用。

#ifndef ZTOOLS_PROVIDER_BRIDGE_H_
#define ZTOOLS_PROVIDER_BRIDGE_H_

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <napi.h>

#include "logger.h"

namespace ztools_provider_bridge {

/** 原生侧一次 provider 调用的结果。 */
struct InvokeResult {
  bool ok = false;    // 是否成功拿到 JS 侧结果
  std::string value;  // 成功时的结果 JSON 字符串
  std::string error;  // 失败原因（未启动 / 正在关闭 / 队列不可用 / 超时 / JS 侧报错等）
};

namespace internal {

/** 投递给 JS 线程的一次调用请求（BridgeCallJs 处理完后释放）。 */
struct InvokeJob {
  uint64_t seq;
  std::string type;
  std::string inputJson;
};

/** invokeProviderFromNative 的回调载荷（NativeInvokeCallJs 处理完后释放）。 */
struct NativeInvokePayload {
  napi_threadsafe_function tsfn;
  bool ok;
  std::string value;
  std::string error;
};

// 桥接全局状态：本头文件在各平台只被 binding 主编译单元包含一次，
// 使用函数内静态保证唯一切片，避免 ODR 问题
inline std::mutex& Mutex() {
  static std::mutex m;
  return m;
}
inline napi_threadsafe_function& Tsfn() {
  static napi_threadsafe_function tsfn = nullptr;
  return tsfn;
}
inline std::thread::id& JsThreadId() {
  static std::thread::id tid;
  return tid;
}
inline std::atomic<uint64_t>& NextSeq() {
  static std::atomic<uint64_t> seq(0);
  return seq;
}
inline std::map<uint64_t, std::shared_ptr<std::promise<InvokeResult>>>& Pending() {
  static std::map<uint64_t, std::shared_ptr<std::promise<InvokeResult>>> pending;
  return pending;
}

// 在 JS 线程上执行：取出请求，调用 JS 分发函数 dispatcher(type, inputJson, seq)
inline void BridgeCallJs(napi_env env, napi_value js_callback, void* /*context*/, void* data) {
  auto* job = static_cast<InvokeJob*>(data);
  if (job == nullptr) {
    return;
  }
  if (env != nullptr && js_callback != nullptr) {
    napi_value global = nullptr;
    napi_get_global(env, &global);
    napi_value argv[3];
    napi_create_string_utf8(env, job->type.c_str(), NAPI_AUTO_LENGTH, &argv[0]);
    napi_create_string_utf8(env, job->inputJson.c_str(), NAPI_AUTO_LENGTH, &argv[1]);
    napi_create_double(env, static_cast<double>(job->seq), &argv[2]);
    napi_call_function(env, global, js_callback, 3, argv, nullptr);
  }
  delete job;
}

// 取出指定 seq 的 pending promise；不存在（已超时 / 已停止）时返回空
inline std::shared_ptr<std::promise<InvokeResult>> TakePending(uint64_t seq) {
  std::lock_guard<std::mutex> lock(Mutex());
  auto it = Pending().find(seq);
  if (it == Pending().end()) {
    return nullptr;
  }
  auto promise = it->second;
  Pending().erase(it);
  return promise;
}

}  // namespace internal

// ==================== 原生侧调用入口（供本插件内的原生模块使用） ====================

/** 桥接是否已启动（JS 侧已注册分发函数）。 */
inline bool IsReady() {
  std::lock_guard<std::mutex> lock(internal::Mutex());
  return internal::Tsfn() != nullptr;
}

/**
 * 原生线程调用 JS 侧 provider 方法并阻塞等待结果。
 * @param type 能力类型（如 "translation" / "ocr"，由 JS 侧自行约定）
 * @param inputJson 入参 JSON 字符串
 * @param timeoutMs 等待 JS 侧结果的超时时间（毫秒）
 * @returns 调用结果；失败时 ok=false 且 error 说明原因
 */
inline InvokeResult Invoke(const std::string& type, const std::string& inputJson,
                           uint32_t timeoutMs) {
  napi_threadsafe_function tsfn = nullptr;
  {
    std::lock_guard<std::mutex> lock(internal::Mutex());
    tsfn = internal::Tsfn();
    if (tsfn == nullptr) {
      ZLOG_WARN("provider", "invoke \"%s\" rejected: bridge not started", type.c_str());
      return InvokeResult{false, "", "provider bridge not started"};
    }
    // JS 线程上阻塞等待会与 BridgeCallJs 互相等待，直接拒绝
    if (std::this_thread::get_id() == internal::JsThreadId()) {
      ZLOG_WARN("provider", "invoke \"%s\" rejected: called on JS main thread", type.c_str());
      return InvokeResult{false, "", "InvokeProvider must not be called on the JS main thread"};
    }
    // 增加引用计数，防止等待期间被 stopProviderBridge 释放底层函数
    if (napi_acquire_threadsafe_function(tsfn) != napi_ok) {
      ZLOG_WARN("provider", "invoke \"%s\" rejected: bridge is closing", type.c_str());
      return InvokeResult{false, "", "provider bridge is closing"};
    }
  }

  ZLOG_DEBUG("provider", "invoke \"%s\" (timeout %ums, input %zu bytes)", type.c_str(),
             timeoutMs, inputJson.size());
  const uint64_t seq = internal::NextSeq().fetch_add(1) + 1;
  auto promise = std::make_shared<std::promise<InvokeResult>>();
  auto future = promise->get_future();
  {
    std::lock_guard<std::mutex> lock(internal::Mutex());
    internal::Pending()[seq] = promise;
  }

  auto* job = new internal::InvokeJob{seq, type, inputJson};
  if (napi_call_threadsafe_function(tsfn, job, napi_tsfn_nonblocking) != napi_ok) {
    // 桥接正在关闭，请求未入队（CallJs 不会处理该 job），此处代为清理
    ZLOG_WARN("provider", "invoke \"%s\" failed: queue unavailable", type.c_str());
    internal::TakePending(seq);
    delete job;
    napi_release_threadsafe_function(tsfn, napi_tsfn_release);
    return InvokeResult{false, "", "provider bridge queue unavailable"};
  }

  InvokeResult result;
  if (future.wait_for(std::chrono::milliseconds(timeoutMs)) == std::future_status::timeout) {
    // 超时后移除登记；JS 迟到回传的结果会被 TakePending 丢弃
    ZLOG_ERROR("provider", "invoke \"%s\" timed out after %ums", type.c_str(), timeoutMs);
    internal::TakePending(seq);
    result = InvokeResult{false, "", "provider invocation timed out"};
  } else {
    result = future.get();
  }
  napi_release_threadsafe_function(tsfn, napi_tsfn_release);
  if (result.ok) {
    ZLOG_DEBUG("provider", "invoke \"%s\" ok (result %zu bytes)", type.c_str(),
               result.value.size());
  } else {
    ZLOG_WARN("provider", "invoke \"%s\" failed: %s", type.c_str(), result.error.c_str());
  }
  return result;
}

// ==================== N-API 导出（在 JS 线程上被调用） ====================

/**
 * 启动桥接：注册 JS 分发函数。
 * @param info[0] dispatcher: (type: string, inputJson: string, seq: number) => void，
 *                JS 侧负责执行逻辑并按 seq 调 resolveProviderBridge / rejectProviderBridge
 * @returns 无返回值；参数非法或重复启动时抛出 JS 异常
 */
inline Napi::Value StartProviderBridge(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsFunction()) {
    Napi::TypeError::New(env, "Expected a dispatcher function").ThrowAsJavaScriptException();
    return env.Undefined();
  }
  {
    std::lock_guard<std::mutex> lock(internal::Mutex());
    if (internal::Tsfn() != nullptr) {
      Napi::Error::New(env, "Provider bridge already started").ThrowAsJavaScriptException();
      return env.Undefined();
    }
  }

  napi_value callback = info[0];
  napi_value resource_name;
  napi_create_string_utf8(env, "ProviderBridgeCallback", NAPI_AUTO_LENGTH, &resource_name);

  napi_threadsafe_function tsfn = nullptr;
  if (napi_create_threadsafe_function(env, callback, nullptr, resource_name, 0, 1, nullptr,
                                      nullptr, nullptr, internal::BridgeCallJs,
                                      &tsfn) != napi_ok) {
    Napi::Error::New(env, "Failed to create provider bridge").ThrowAsJavaScriptException();
    return env.Undefined();
  }

  {
    std::lock_guard<std::mutex> lock(internal::Mutex());
    internal::Tsfn() = tsfn;
    // 记录 JS 主线程 id，供 Invoke 做死锁防护
    internal::JsThreadId() = std::this_thread::get_id();
  }
  ZLOG_INFO("provider", "bridge started");
  return env.Undefined();
}

/**
 * 停止桥接：所有还在等待的原生调用立即以错误结束，已入队的请求仍会被处理完。
 * @returns 无返回值
 */
inline Napi::Value StopProviderBridge(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  napi_threadsafe_function tsfn = nullptr;
  {
    std::lock_guard<std::mutex> lock(internal::Mutex());
    tsfn = internal::Tsfn();
    internal::Tsfn() = nullptr;
  }
  // 让所有还在等待的原生线程立即失败，而不是挂到超时
  std::map<uint64_t, std::shared_ptr<std::promise<InvokeResult>>> pending;
  {
    std::lock_guard<std::mutex> lock(internal::Mutex());
    pending.swap(internal::Pending());
  }
  for (auto& entry : pending) {
    entry.second->set_value(InvokeResult{false, "", "provider bridge stopped"});
  }
  ZLOG_INFO("provider", "bridge stopped (%zu pending call(s) failed)", pending.size());
  if (tsfn != nullptr) {
    napi_release_threadsafe_function(tsfn, napi_tsfn_release);
  }
  return env.Undefined();
}

/**
 * JS 侧回传成功结果。
 * @param info[0] seq 请求序号（与分发出的 seq 一致）
 * @param info[1] resultJson 结果 JSON 字符串
 * @returns 无返回值；seq 已超时或不存在时静默丢弃
 */
inline Napi::Value ResolveProviderBridge(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (seq: number, resultJson: string)")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  const uint64_t seq = static_cast<uint64_t>(info[0].As<Napi::Number>().DoubleValue());
  const std::string value = info[1].As<Napi::String>().Utf8Value();
  if (auto promise = internal::TakePending(seq)) {
    promise->set_value(InvokeResult{true, value, ""});
  }
  return env.Undefined();
}

/**
 * JS 侧回传失败。
 * @param info[0] seq 请求序号（与分发出的 seq 一致）
 * @param info[1] error 错误说明
 * @returns 无返回值；seq 已超时或不存在时静默丢弃
 */
inline Napi::Value RejectProviderBridge(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[0].IsNumber() || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (seq: number, error: string)")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  const uint64_t seq = static_cast<uint64_t>(info[0].As<Napi::Number>().DoubleValue());
  const std::string error = info[1].As<Napi::String>().Utf8Value();
  if (auto promise = internal::TakePending(seq)) {
    promise->set_value(InvokeResult{false, "", error});
  }
  return env.Undefined();
}

/**
 * 查询桥接是否就绪。
 * @returns 就绪返回 true，未启动（或已停止）返回 false
 */
inline Napi::Value IsProviderBridgeReady(const Napi::CallbackInfo& info) {
  return Napi::Boolean::New(info.Env(), IsReady());
}

// ==================== 供 JS 侧验证桥接通路的回调式入口 ====================

// 把原生线程上的调用结果带回 JS 线程并调用回调
inline void NativeInvokeCallJs(napi_env env, napi_value js_callback, void* /*context*/,
                               void* data) {
  auto* payload = static_cast<internal::NativeInvokePayload*>(data);
  if (payload == nullptr) {
    return;
  }
  if (env != nullptr && js_callback != nullptr) {
    napi_value global = nullptr;
    napi_get_global(env, &global);
    napi_value argv[2] = {nullptr, nullptr};
    if (payload->ok) {
      napi_get_null(env, &argv[0]);
      napi_create_string_utf8(env, payload->value.c_str(), NAPI_AUTO_LENGTH, &argv[1]);
    } else {
      napi_create_string_utf8(env, payload->error.c_str(), NAPI_AUTO_LENGTH, &argv[0]);
      napi_get_null(env, &argv[1]);
    }
    napi_call_function(env, global, js_callback, 2, argv, nullptr);
  }
  // 与创建时的 initial_thread_count=1 配对，由消费方释放；随后释放载荷
  napi_release_threadsafe_function(payload->tsfn, napi_tsfn_release);
  delete payload;
}

/**
 * 在独立原生线程上发起一次 provider 调用，完成后在 JS 线程回调结果。
 * 走的是与 Invoke 完全相同的真实通路，主要用于从 JS 验证桥接；
 * 真正的原生业务代码应直接调用 Invoke。
 * @param info[0] type 能力类型
 * @param info[1] inputJson 入参 JSON 字符串
 * @param info[2] timeoutMs 超时毫秒数（可选，默认 15000）
 * @param info[3] callback: (error: string | null, resultJson: string | null) => void
 * @returns 无返回值
 */
inline Napi::Value InvokeProviderFromNative(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 4 || !info[0].IsString() || !info[1].IsString() || !info[3].IsFunction()) {
    Napi::TypeError::New(env,
                         "Expected (type: string, inputJson: string, timeoutMs?: number, "
                         "callback: function)")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }
  const std::string type = info[0].As<Napi::String>().Utf8Value();
  const std::string inputJson = info[1].As<Napi::String>().Utf8Value();
  const uint32_t timeoutMs =
      info.Length() >= 3 && info[2].IsNumber() ? info[2].As<Napi::Number>().Uint32Value() : 15000u;

  napi_value callback = info[3];
  napi_value resource_name;
  napi_create_string_utf8(env, "ProviderBridgeNativeInvoke", NAPI_AUTO_LENGTH, &resource_name);

  napi_threadsafe_function tsfn = nullptr;
  if (napi_create_threadsafe_function(env, callback, nullptr, resource_name, 0, 1, nullptr,
                                      nullptr, nullptr, NativeInvokeCallJs,
                                      &tsfn) != napi_ok) {
    Napi::Error::New(env, "Failed to create native invoke callback")
        .ThrowAsJavaScriptException();
    return env.Undefined();
  }

  // 生产者线程只负责投递；TSFN 由 NativeInvokeCallJs（消费方）释放
  std::thread([type, inputJson, timeoutMs, tsfn]() {
    InvokeResult result = Invoke(type, inputJson, timeoutMs);
    auto* payload = new internal::NativeInvokePayload{tsfn, result.ok, result.value, result.error};
    if (napi_call_threadsafe_function(tsfn, payload, napi_tsfn_nonblocking) != napi_ok) {
      // 投递失败时 CallJs 不会执行，此处代为释放，避免泄漏
      napi_release_threadsafe_function(tsfn, napi_tsfn_release);
      delete payload;
    }
  }).detach();

  return env.Undefined();
}

}  // namespace ztools_provider_bridge

#endif  // ZTOOLS_PROVIDER_BRIDGE_H_
