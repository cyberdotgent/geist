// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// File > Print Book. The pages are composed by the reader itself, as the
// macOS reader composes a PDF: every topic is laid out by MSHTML in a hidden
// browser at the printable width, measured, split at block or line starts,
// and drawn onto its own pages through GDI; the cover and title topics come
// first and unnumbered, a generated contents lists the printed page numbers,
// and wide fixed-layout topics are printed on landscape pages.
//
// The page plan is print_plan.hpp; this file drives MSHTML and the printer.
// With GEIST_PRINT_TO_DIRECTORY set in the environment the pages are written
// as bitmaps to that directory instead of a printer, for testing.

#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace geist_reader {

class BookPrintJob {
public:
  virtual ~BookPrintJob() = default;
  // Asks for a printer and starts preparing the book. Returns null when the
  // user cancels the print dialog or the book cannot be opened; otherwise
  // `finished` is called (once) when the job ends for any reason.
  static std::unique_ptr<BookPrintJob> start(HWND owner, const std::filesystem::path& book,
                                             const std::string& stylesheet, const std::string& host,
                                             std::function<void()> finished);
  virtual void cancel() = 0;
  static bool register_class(HINSTANCE instance);
};

} // namespace geist_reader
