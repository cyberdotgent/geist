#pragma once

// Typed model for the two reader-generated navigation topics: the book's
// table of contents (`CHDLEVEL :TOC`, id `CONTENTS`) and its index
// (`CHDLEVEL :INDEX`, id `INDEX`).
//
// These topics carry no display rows at all.  Their whole body is a sequence
// of BookManager *display lines* (doc/boo-spec/logical-controls.adoc, "Display Lines
// Inside A Record Payload"): a one-byte length prefix followed by exactly that
// many payload bytes of tokens.  In a generated TOC/INDEX topic every such
// line holds exactly one control, which makes the topic a pure control record
// and the display-line walk its complete, verifiable segmentation.  This was
// confirmed over the whole corpus: 34 CONTENTS and 29 INDEX topics, every
// record's lines parse, and each of the 36,972 body lines opens with one of
// the controls below.
//
// Byte-level evidence (SC31-711 DT 19941010174546, book record 6, payload at
// 0xa435):
//
//   token 30  value 5  width 1  -> length byte of a five-byte line
//   token 31  value 56495 width 2 -> `ctocdef=0`
//   tokens 32-34 values 169/168/179 width 1 -> operands `1 0 2`
//   token 60  value 9  width 1  -> length byte of a nine-byte line
//   token 61  value 60 width 1 -> `ctoce`
//   tokens 62,63 -> `0`, `1`      (indent level, ctocdef style ordinal)
//   token 64  value 56447 width 2 -> `COVER`
//   tokens 65,66 -> `Book`, `Cover`
//
// and for the index (record 538, payload at 0x34456):
//
//   token 31 value 56150 width 2 -> `cidelm`
//   token 32 value 4     width 1 -> word U+25BA, the declared field delimiter
//   token 34 value 56101 width 2 -> `cgpsep`
//   token 35 value 55302 width 2 -> words {U+25BA, 'A'}: the delimiter can be
//                                    glued into the next token's word list,
//                                    so fields are split on the delimiter
//                                    *word*, never on a token boundary
//   token 37 value 49    width 1 -> `citerm`
//   tokens 38-49                 -> U+25BA `adapter problems` U+25BA `1`
//                                    U+25BA `2.2.4`
//
// so an index term is `citerm <D> term <D> level <D> target...`, where <D> is
// the word `cidelm` declared and every field after the level is one more
// target for the same term.  A trailing empty field is a term with no target
// of its own (GC28-183 record 917: `//*DATASET statement` level 1); such a
// term is a parent whose children carry the targets.

#include "geist/detail/ir/book_topic_catalog_ir.hpp"
#include "geist/detail/lowering/document_ir.hpp"
#include "geist/detail/core/internal.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geist::detail {

enum class GeneratedTocIndexKindIR { contents, index };

// Typed disposition of one display line.  Extraction assigns exactly one to
// every line of every record in the topic, in source order, so the line
// vector is the conservation ledger for the whole topic.
enum class GeneratedTocIndexLineKindIR {
  topic_start,
  metadata,
  title,
  structural_anchor,
  toc_definition,
  toc_entry,
  index_delimiter,
  index_group,
  index_term,
  index_end,
  spacing,
  region_directive,
};

struct GeneratedTocIndexLineIR {
  GeneratedTocIndexLineKindIR kind = GeneratedTocIndexLineKindIR::metadata;
  std::string opcode;
  DocumentSourceSliceIR source;
};

// `ctocdef=<ordinal> <operands...>`: the reader's per-style row format for the
// TOC.  Retained losslessly as evidence; it drives presentation the Markdown
// lowering does not reproduce (boldness and the reader's blank-line grouping),
// so no semantics are inferred from it.
struct GeneratedTocDefinitionIR {
  std::uint32_t ordinal = 0;
  std::vector<std::string> operands;
  DocumentSourceSliceIR source;
};

struct GeneratedTocEntryIR {
  // `ctoce <depth> <style> <topic id> <title>`.  `depth` is the entry's nesting
  // level (0 at the top); `style` selects the `ctocdef` row format.
  std::uint32_t depth = 0;
  std::uint32_t style = 0;
  std::string topic_id;
  std::string title;
  DocumentSourceSliceIR source;
  std::vector<DocumentSourceSliceIR> topic_id_slices;
  std::vector<DocumentSourceSliceIR> title_slices;
};

// One target of an index term.  A target field is either a single topic id or
// the page range `<id> to <id>`, which BookServer serves as two links joined
// by the word `to` (hosted GC23-046 INDEX DT 19920330095121, `storage
// requirements / target system` -> `<a href="5.2.4">5.2.4</a> to
// <a href="5.3">5.3</a>`).  All 136 multi-word target fields in the corpus are
// exactly that three-word shape.
struct GeneratedIndexTargetIR {
  std::string topic_id;
  // Empty unless the field is a range, in which case this is its end.
  std::string range_end_topic_id;
  std::vector<DocumentSourceSliceIR> slices;
};

struct GeneratedIndexTermIR {
  // 1-based nesting level exactly as the source writes it.
  std::uint32_t level = 1;
  std::string term;
  std::vector<GeneratedIndexTargetIR> targets;
  DocumentSourceSliceIR source;
  std::vector<DocumentSourceSliceIR> term_slices;
};

struct GeneratedIndexGroupIR {
  // `cgpsep <D><label>`: the letter group separator ("A", "Special
  // Characters", ...).
  std::string label;
  DocumentSourceSliceIR source;
  std::vector<GeneratedIndexTermIR> terms;
};

struct GeneratedTocIndexTopicIR {
  GeneratedTocIndexKindIR kind = GeneratedTocIndexKindIR::contents;
  std::string heading_level;
  std::string title;
  DocumentSourceSliceIR heading_source;
  // `SR<id>` anchors the reader serves as `<a name="<id>">` (SC31-711 INDEX
  // `SRHDRINDEX`, GG24-395 INDEX `SRSPTPAGENO`).
  std::vector<std::pair<std::string, DocumentSourceSliceIR>> anchors;
  // The word `cidelm` declares as the index field delimiter.
  std::uint16_t delimiter = 0;
  std::vector<GeneratedTocDefinitionIR> definitions;
  std::vector<GeneratedTocEntryIR> entries;
  std::vector<GeneratedIndexGroupIR> groups;
  std::vector<GeneratedTocIndexLineIR> lines;
};

// Recognizes a generated CONTENTS/INDEX topic.  Fails closed on any display
// line it cannot type, on any control outside the grammar, and on any TOC
// entry whose topic id the book's own topic catalog does not carry.
std::optional<GeneratedTocIndexTopicIR> extract_generated_toc_index_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const BookTopicCatalogIR* catalog, std::string* error = nullptr);

// Canonical re-extraction verifier: rebuilds the model from the same source
// and requires exact structural equality, so a hand-built or mutated model is
// rejected.
bool verify_generated_toc_index_topic_ir(
    const std::vector<DecodedLogicalRecordSource>& records,
    const BookTopicCatalogIR* catalog, const GeneratedTocIndexTopicIR& topic,
    std::string* error = nullptr);

} // namespace geist::detail
