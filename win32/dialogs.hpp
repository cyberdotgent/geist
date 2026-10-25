// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The reader's dialogs: settings, book information and about.

#pragma once

#include "book_source.hpp"
#include "win32_util.hpp"

#include <string>

namespace geist_reader {

// Modeless; changes apply to every window as they are made.
HWND create_settings_dialog(HWND owner);
// Modal to `owner`.
void show_book_information(HWND owner, const BookInformation& information);
void show_about_dialog(HWND owner);
// The embedded licence notices (this program and its static libraries).
void show_licenses_dialog(HWND owner);

} // namespace geist_reader
