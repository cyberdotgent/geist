// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "protocol.hpp"

#include "book_source.hpp"
#include "win32_util.hpp"

#include <urlmon.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace geist_reader {
namespace {

// {5D0A3F8E-6C1B-4E57-9B0B-2F0E7C4A9D11}: identifies the handler to urlmon
// only; it is never registered with COM.
const CLSID kProtocolClass = {0x5d0a3f8e, 0x6c1b, 0x4e57, {0x9b, 0x0b, 0x2f, 0x0e, 0x7c, 0x4a, 0x9d, 0x11}};

std::mutex registry_mutex;
std::map<std::string, std::weak_ptr<BookSource>>& registry() {
  static std::map<std::string, std::weak_ptr<BookSource>> sources;
  return sources;
}

std::shared_ptr<BookSource> source_for(const std::string& host) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  const auto found = registry().find(host);
  return found == registry().end() ? nullptr : found->second.lock();
}

class Protocol final : public IInternetProtocol {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IInternetProtocolRoot) ||
        IsEqualIID(iid, IID_IInternetProtocol)) {
      *out = static_cast<IInternetProtocol*>(this);
      AddRef();
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
  ULONG STDMETHODCALLTYPE Release() override {
    const LONG count = InterlockedDecrement(&refs_);
    if (!count) delete this;
    return static_cast<ULONG>(count);
  }

  HRESULT STDMETHODCALLTYPE Start(LPCWSTR url, IInternetProtocolSink* sink, IInternetBindInfo*,
                                  DWORD, HANDLE_PTR) override {
    if (!url || !sink) return E_INVALIDARG;
    const std::string address = narrow(url);
    std::optional<Response> response;
    if (const auto parsed = parse_url(address)) {
      if (auto source = source_for(parsed->host)) {
        try {
          response = source->serve(address);
        } catch (...) {
          response.reset();
        }
      }
    }
    if (!response) {
      sink->ReportResult(INET_E_RESOURCE_NOT_FOUND, 0, nullptr);
      return INET_E_RESOURCE_NOT_FOUND;
    }
    data_ = std::move(response->bytes);
    position_ = 0;
    const std::wstring mime = widen(response->mime);
    sink->ReportProgress(BINDSTATUS_VERIFIEDMIMETYPEAVAILABLE, mime.c_str());
    const ULONG size = static_cast<ULONG>(data_.size());
    sink->ReportData(BSCF_FIRSTDATANOTIFICATION | BSCF_LASTDATANOTIFICATION | BSCF_DATAFULLYAVAILABLE,
                     size, size);
    sink->ReportResult(S_OK, 0, nullptr);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE Continue(PROTOCOLDATA*) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Abort(HRESULT, DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Terminate(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Suspend() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Resume() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE Read(void* buffer, ULONG size, ULONG* read) override {
    if (!buffer) return E_POINTER;
    const std::size_t left = data_.size() - position_;
    const ULONG count = static_cast<ULONG>(std::min<std::size_t>(left, size));
    std::memcpy(buffer, data_.data() + position_, count);
    position_ += count;
    if (read) *read = count;
    return position_ >= data_.size() ? S_FALSE : S_OK;
  }
  HRESULT STDMETHODCALLTYPE Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return E_FAIL; }
  HRESULT STDMETHODCALLTYPE LockRequest(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE UnlockRequest() override { return S_OK; }

private:
  ~Protocol() = default;
  LONG refs_ = 1;
  std::string data_;
  std::size_t position_ = 0;
};

class Factory final : public IClassFactory {
public:
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
    if (!out) return E_POINTER;
    if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, IID_IClassFactory)) {
      *out = static_cast<IClassFactory*>(this);
      return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
  }
  // A static object: it lives as long as the process.
  ULONG STDMETHODCALLTYPE AddRef() override { return 2; }
  ULONG STDMETHODCALLTYPE Release() override { return 1; }
  HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
    if (outer) return CLASS_E_NOAGGREGATION;
    auto* protocol = new Protocol();
    const HRESULT result = protocol->QueryInterface(iid, out);
    protocol->Release();
    return result;
  }
  HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

Factory factory;
bool registered = false;

} // namespace

bool register_protocol() {
  if (registered) return true;
  ComPtr<IInternetSession> session;
  if (FAILED(CoInternetGetSession(0, session.put(), 0)) || !session) return false;
  registered = SUCCEEDED(session->RegisterNameSpace(&factory, kProtocolClass, L"geist", 0, nullptr, 0));
  return registered;
}

void unregister_protocol() {
  if (!registered) return;
  ComPtr<IInternetSession> session;
  if (SUCCEEDED(CoInternetGetSession(0, session.put(), 0)) && session)
    session->UnregisterNameSpace(&factory, L"geist");
  registered = false;
}

void attach_source(const std::shared_ptr<BookSource>& source) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  registry()[source->host()] = source;
}

void detach_source(const BookSource* source) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  const auto found = registry().find(source->host());
  if (found != registry().end()) registry().erase(found);
}

} // namespace geist_reader
