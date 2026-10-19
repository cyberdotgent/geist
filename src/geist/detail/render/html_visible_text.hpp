// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace geist::detail {

// Text projection of HTML emitted by Geist, not a general-purpose browser HTML
// parser. Inline elements preserve adjacency; block/cell boundaries separate
// words. Image alternatives and browser-generated decimal list markers are
// included; other attributes (including link destinations) supply no text.
std::string html_visible_text(const std::string &html);

} // namespace geist::detail
