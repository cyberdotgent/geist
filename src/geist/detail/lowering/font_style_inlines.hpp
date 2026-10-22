// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/layout/font_span_ir.hpp"

namespace geist::detail {

// How a `CFONT` highlight style lowers inside running text and verbatim
// rows alike: the monospace example styles become a code span
// (`font_style_is_code`), every other style emphasis of the kind
// `font_style_emphasis_kind` names.
bool font_style_is_code(FontStyleIR style);
EmphasisKindIR font_style_emphasis_kind(FontStyleIR style);

} // namespace geist::detail
