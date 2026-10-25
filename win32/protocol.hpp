// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// The geist: URL scheme, served to MSHTML by an asynchronous pluggable
// protocol registered as a temporary, process-local namespace handler
// (IInternetSession::RegisterNameSpace). Nothing is written to the registry
// for it, and MinGW-w64 and MSVC both ship the urlmon declarations it needs.
//
// The handler is shared by every window; each window's book registers under
// its own URL host, so a request finds its book by the host it names.

#pragma once

#include <memory>

namespace geist_reader {

class BookSource;

bool register_protocol();
void unregister_protocol();
void attach_source(const std::shared_ptr<BookSource>& source);
void detach_source(const BookSource* source);

} // namespace geist_reader
