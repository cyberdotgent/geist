// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "geist/detail/lowering/figure_document_lowering.hpp"
namespace geist::detail {
std::optional<std::vector<BlockIR>>
lower_ordered_figure_document_blocks(const FigureSourceBlockIR &figure,
                                     std::string *error);
}
