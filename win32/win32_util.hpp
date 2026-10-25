// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Small shared helpers for the Win32 reader: UTF-8/UTF-16 conversion, an
// owning COM pointer, BSTR and VARIANT wrappers and DPI scaling. Plain Win32
// and the C++ standard library only, so both MinGW-w64 and MSVC build it.

#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <oleauto.h>

#include <string>
#include <utility>

namespace geist_reader {

std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& utf16);

template <class T> class ComPtr {
public:
  ComPtr() = default;
  ComPtr(const ComPtr& other) : ptr_(other.ptr_) { if (ptr_) ptr_->AddRef(); }
  ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
  ~ComPtr() { reset(); }
  ComPtr& operator=(ComPtr other) { std::swap(ptr_, other.ptr_); return *this; }
  T* get() const { return ptr_; }
  T* operator->() const { return ptr_; }
  explicit operator bool() const { return ptr_ != nullptr; }
  T** put() { reset(); return &ptr_; }
  void** put_void() { return reinterpret_cast<void**>(put()); }
  void reset() { if (ptr_) { ptr_->Release(); ptr_ = nullptr; } }
  void attach(T* value) { reset(); ptr_ = value; }
  template <class U> ComPtr<U> as(REFIID iid) const {
    ComPtr<U> out;
    if (ptr_) ptr_->QueryInterface(iid, out.put_void());
    return out;
  }

private:
  T* ptr_ = nullptr;
};

class Bstr {
public:
  Bstr() = default;
  explicit Bstr(const std::wstring& text) : value_(SysAllocStringLen(text.data(), static_cast<UINT>(text.size()))) {}
  Bstr(const Bstr&) = delete;
  Bstr& operator=(const Bstr&) = delete;
  ~Bstr() { SysFreeString(value_); }
  BSTR get() const { return value_; }
  BSTR* put() { SysFreeString(value_); value_ = nullptr; return &value_; }
  std::wstring str() const { return value_ ? std::wstring(value_, SysStringLen(value_)) : std::wstring(); }

private:
  BSTR value_ = nullptr;
};

class Variant {
public:
  Variant() { VariantInit(&value_); }
  explicit Variant(const std::wstring& text) {
    VariantInit(&value_);
    value_.vt = VT_BSTR;
    value_.bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
  }
  explicit Variant(long number) { VariantInit(&value_); value_.vt = VT_I4; value_.lVal = number; }
  Variant(const Variant&) = delete;
  Variant& operator=(const Variant&) = delete;
  ~Variant() { VariantClear(&value_); }
  VARIANT* get() { return &value_; }
  const VARIANT& operator*() const { return value_; }

private:
  VARIANT value_;
};

// The string a VARIANT (possibly by reference) holds, or empty.
std::wstring variant_string(const VARIANT& value);

// Scales a 96-dpi length to `dpi`.
inline int scale(int value, UINT dpi) { return MulDiv(value, static_cast<int>(dpi), 96); }

// The window's DPI, falling back to the system DPI before Windows 10 1607.
UINT window_dpi(HWND window);

// The message font for `dpi`, at `percent` of its size and the given weight.
HFONT create_ui_font(UINT dpi, int percent = 100, int weight = FW_NORMAL);

// Shows `message` in a task-modal error box.
void show_error(HWND owner, const std::wstring& title, const std::wstring& message);

} // namespace geist_reader
