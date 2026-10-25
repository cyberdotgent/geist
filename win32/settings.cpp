// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "settings.hpp"

#include "win32_util.hpp"

#include <shlobj.h>
#include <urlmon.h>

#include <algorithm>
#include <vector>

namespace geist_reader {
namespace {

const wchar_t kSettingsKey[] = L"Software\\Geist\\Geist BOO Reader";
const wchar_t kProgId[] = L"GeistBOOReader.Book";
const wchar_t kClassesKey[] = L"Software\\Classes\\";

DWORD read_dword(HKEY key, const wchar_t* name, DWORD fallback) {
  DWORD value = 0, size = sizeof value, type = 0;
  if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size) == ERROR_SUCCESS &&
      type == REG_DWORD)
    return value;
  return fallback;
}

std::wstring read_string(HKEY key, const wchar_t* name) {
  DWORD size = 0, type = 0;
  if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ || !size)
    return {};
  std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
  if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(buffer.data()), &size) !=
      ERROR_SUCCESS)
    return {};
  return std::wstring(buffer.data());
}

bool write_string(HKEY root, const std::wstring& path, const wchar_t* name, const std::wstring& value) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(root, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
    return false;
  const LONG result = RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                     static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
  RegCloseKey(key);
  return result == ERROR_SUCCESS;
}

int clamp_size(DWORD value, int fallback) {
  if (value < kMinimumTextSize || value > kMaximumTextSize) return fallback;
  return static_cast<int>(value);
}

} // namespace

std::wstring executable_path() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length < buffer.size()) return std::wstring(buffer.data(), length);
    buffer.resize(buffer.size() * 2);
  }
}

Settings load_settings() {
  Settings settings;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return settings;
  const std::wstring family = read_string(key, L"FontFamily");
  if (!family.empty()) settings.font_family = family;
  settings.contents_size = clamp_size(read_dword(key, L"ContentsSize", 14), 14);
  settings.topic_size = clamp_size(read_dword(key, L"TopicSize", 14), 14);
  RegCloseKey(key);
  return settings;
}

void save_settings(const Settings& settings) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
      ERROR_SUCCESS)
    return;
  RegSetValueExW(key, L"FontFamily", 0, REG_SZ, reinterpret_cast<const BYTE*>(settings.font_family.c_str()),
                 static_cast<DWORD>((settings.font_family.size() + 1) * sizeof(wchar_t)));
  const DWORD contents = static_cast<DWORD>(settings.contents_size);
  const DWORD topic = static_cast<DWORD>(settings.topic_size);
  RegSetValueExW(key, L"ContentsSize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&contents), sizeof contents);
  RegSetValueExW(key, L"TopicSize", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&topic), sizeof topic);
  RegCloseKey(key);
}

bool register_file_type(std::wstring& error) {
  const std::wstring exe = executable_path();
  const std::wstring classes = kClassesKey;
  const std::wstring prog = classes + kProgId;
  bool ok = write_string(HKEY_CURRENT_USER, prog, nullptr, L"IBM BookManager book") &&
            write_string(HKEY_CURRENT_USER, prog + L"\\DefaultIcon", nullptr, L"\"" + exe + L"\",0") &&
            write_string(HKEY_CURRENT_USER, prog + L"\\shell\\open\\command", nullptr,
                         L"\"" + exe + L"\" \"%1\"") &&
            write_string(HKEY_CURRENT_USER, classes + L".boo", nullptr, kProgId);
  HKEY key = nullptr;
  if (ok && RegCreateKeyExW(HKEY_CURRENT_USER, (classes + L".boo\\OpenWithProgids").c_str(), 0, nullptr, 0,
                            KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    RegSetValueExW(key, kProgId, 0, REG_NONE, nullptr, 0);
    RegCloseKey(key);
  }
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  if (!ok) error = L"The registry could not be updated.";
  return ok;
}

bool unregister_file_type(std::wstring& error) {
  const std::wstring classes = kClassesKey;
  HKEY key = nullptr;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, (classes + L".boo").c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) ==
      ERROR_SUCCESS) {
    if (read_string(key, nullptr) == kProgId) RegDeleteValueW(key, nullptr);
    RegCloseKey(key);
  }
  if (RegOpenKeyExW(HKEY_CURRENT_USER, (classes + L".boo\\OpenWithProgids").c_str(), 0, KEY_SET_VALUE, &key) ==
      ERROR_SUCCESS) {
    RegDeleteValueW(key, kProgId);
    RegCloseKey(key);
  }
  const LONG result = RegDeleteTreeW(HKEY_CURRENT_USER, (classes + kProgId).c_str());
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
    error = L"The registry could not be updated.";
    return false;
  }
  return true;
}

bool file_type_registered() {
  HKEY key = nullptr;
  const std::wstring path = std::wstring(kClassesKey) + kProgId + L"\\shell\\open\\command";
  if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
  const std::wstring command = read_string(key, nullptr);
  RegCloseKey(key);
  return command.find(executable_path()) != std::wstring::npos;
}

void configure_browser_features() {
  std::wstring exe = executable_path();
  const auto slash = exe.find_last_of(L"\\/");
  if (slash != std::wstring::npos) exe.erase(0, slash + 1);
  HKEY key = nullptr;
  if (RegCreateKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Internet Explorer\\Main\\FeatureControl\\FEATURE_BROWSER_EMULATION",
                      0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    const DWORD wanted = 11001; // IE11 edge mode, whatever the page declares
    if (read_dword(key, exe.c_str(), 0) != wanted)
      RegSetValueExW(key, exe.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&wanted), sizeof wanted);
    RegCloseKey(key);
  }
  CoInternetSetFeatureEnabled(FEATURE_DISABLE_NAVIGATION_SOUNDS, SET_FEATURE_ON_PROCESS, TRUE);
}

} // namespace geist_reader
