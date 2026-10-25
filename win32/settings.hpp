// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Per-user state kept in the registry under HKCU: the reader's settings,
// the optional .boo file association and the MSHTML document mode.

#pragma once

#include <string>

namespace geist_reader {

struct Settings {
  std::wstring font_family = L"Segoe UI";
  int contents_size = 14; // pixels at 96 dpi, as the macOS reader's points
  int topic_size = 14;    // CSS pixels
};

constexpr int kMinimumTextSize = 8;
constexpr int kMaximumTextSize = 32;

Settings load_settings();
void save_settings(const Settings& settings);

// HKCU\Software\Classes registration of .boo for this executable.
bool register_file_type(std::wstring& error);
bool unregister_file_type(std::wstring& error);
bool file_type_registered();

// Asks MSHTML for the IE11 standards document mode in this executable
// (FEATURE_BROWSER_EMULATION), and quiets navigation sounds.
void configure_browser_features();

std::wstring executable_path();

} // namespace geist_reader
