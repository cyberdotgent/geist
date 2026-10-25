// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "geist/boo.hpp"
#include "geist/detail/lowering/drawn_word_conservation.hpp"
#include "geist/detail/render/render_diagnostic_ir.hpp"
#include "geist/detail/core/internal.hpp"
#include "geist/detail/api/topic_link_targets.hpp"
#include "geist/detail/lowering/typed_route_inventory.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include "tool_version.hpp"

namespace {

std::string join_declines(const std::vector<std::string>& declines) {
  std::string output;
  for (const auto& decline : declines) {
    if (!output.empty())
      output += " | ";
    output += decline;
  }
  return output;
}

void usage() {
  std::cerr << "usage: bootrace <book.boo> <topic-id> "
               "[--all|--records|--segments|--fonts|--ir|--tokens|--lines]\n"
               "       bootrace <book.boo> --coverage\n"
               "       bootrace <book.boo> --links\n"
               "       bootrace <book.boo> --declines\n"
               "       bootrace <book.boo> --conservation [--rendered|--punctuation]\n"
               "       bootrace <book.boo> --conservation --rendered --punctuation\n"
               "       bootrace <book.boo> --margin-marks\n"
               "       bootrace <book.boo> <topic-id> --explain-offset <n>\n";
}

std::string hex4(const char32_t value) {
  std::ostringstream out;
  out << std::uppercase << std::hex;
  out.width(4);
  out.fill('0');
  out << static_cast<std::uint32_t>(value);
  return out.str();
}

std::string tsv_escape(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (const auto ch : value) {
    if (ch == '\\') {
      output += "\\\\";
    } else if (ch == '\t') {
      output += "\\t";
    } else if (ch == '\r') {
      output += "\\r";
    } else if (ch == '\n') {
      output += "\\n";
    } else {
      output.push_back(ch);
    }
  }
  return output;
}

std::string join_records(const std::vector<std::string>& records) {
  std::string output;
  for (std::size_t index = 0; index < records.size(); ++index) {
    if (index != 0) {
      output += " | ";
    }
    output += records[index];
  }
  return output;
}

} // namespace

int main(int argc, char** argv) {
  if (geist_tool::answered_version_request("bootrace", argc, argv)) {
    return 0;
  }

  if (argc != 3 && argc != 4 && argc != 5) {
    usage();
    return 2;
  }

  if (argc == 5 && std::string(argv[3]) == "--explain-offset") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto* entry = document.find_toc_entry(argv[2]);
      if (entry == nullptr) {
        std::cerr << "bootrace: BOO topic id was not found: " << argv[2]
                  << "\n";
        return 1;
      }
      geist::RenderTrace trace;
      const auto text = entry->markdown(trace);
      const auto offset =
          static_cast<std::size_t>(std::stoull(std::string(argv[4])));
      if (offset >= text.size()) {
        std::cerr << "bootrace: output offset is past the end of the render ("
                  << text.size() << " bytes)\n";
        return 1;
      }
      const auto* span = trace.span_at(offset);
      if (span == nullptr) {
        std::cerr << "bootrace: this topic is reproduced verbatim and "
                     "carries no typed provenance\n";
        return 1;
      }
      std::cout << "offset\t" << offset << "\n";
      std::cout << "character\t"
                << tsv_escape(std::string(1, text[offset])) << "\n";
      std::cout << "output_span\t" << span->output_begin << "\t"
                << span->output_end << "\t"
                << tsv_escape(text.substr(
                       span->output_begin,
                       span->output_end - span->output_begin))
                << "\n";
      std::cout << "role\t" << geist::render_trace_role_name(span->role)
                << "\n";
      std::cout << "reason\t" << tsv_escape(span->reason) << "\n";
      std::cout << "node\t" << tsv_escape(span->node_path) << "\n";
      std::cout << "derivation\t" << tsv_escape(span->derivation) << "\n";
      std::cout << "origin_detail\t" << tsv_escape(span->origin_detail)
                << "\n";
      if (span->slices.empty())
        std::cout << "source\tnone: this run is renderer output, not BOO "
                     "source text\n";
      geist::TraceSourceReader source_reader(document);
      for (const auto& slice : span->slices) {
        std::cout << "source\tlogical_record=" << slice.logical_record
                  << "\tsegment=" << slice.segment_index << "\ttokens="
                  << slice.token_begin << ":" << slice.token_end
                  << "\tboo_bytes=" << slice.byte_begin << ":"
                  << slice.byte_end;
        if (slice.character_end != 0)
          std::cout << "\tcharacters=" << slice.character_begin << ":"
                    << slice.character_end;
        std::cout << "\ttext=";
        try {
          std::cout << tsv_escape(source_reader.decode(slice));
        } catch (const std::exception& error) {
          std::cout << "<unreadable: " << error.what() << ">";
        }
        std::cout << "\n";
      }
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }

  // What every topic names, and -- for a topic that renders typed -- whether
  // the typed Document IR names the same things its legacy GML projection
  // does.  A `typed-missing` row is a gap in the typed lowering: an id the
  // book uses to reach the topic that the typed IR cannot state.
  if (argc == 3 && std::string(argv[2]) == "--links") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto kind_name = [](geist::LinkTargetKind kind) {
        switch (kind) {
        case geist::LinkTargetKind::figure:
          return "figure";
        case geist::LinkTargetKind::table:
          return "table";
        case geist::LinkTargetKind::anchor:
          break;
        }
        return "anchor";
      };
      const auto key = [&](const geist::LinkTarget& target) {
        return std::string(kind_name(target.kind)) + " " + target.id + " " +
               target.resource;
      };
      // What each indexed topic names, read off its Document IR. The comment
      // row preserves stable source identity even when a topic names nothing.
      std::cout << "topic\troute\tkind\tid\tresource\n";
      std::size_t named = 0;
      std::size_t topics_naming = 0;
      for (std::size_t source_index = 0;
           source_index < document.topics().size(); ++source_index) {
        const auto& source = document.topics()[source_index];
        const auto entry = document.topic_entry(source.id);
        const auto& diagnostic = entry.render_diagnostic();
        std::cout << "# topic\t" << tsv_escape(source.id)
                  << "\ttopic-index\t" << source_index << "\t"
                  << source.start_logical_record << "\t"
                  << source.end_logical_record << "\t"
                  << tsv_escape(diagnostic.route) << "\n";
        const auto& live = entry.link_targets();
        if (!live.empty()) ++topics_naming;
        for (const auto& target : live) {
          ++named;
          std::cout << tsv_escape(entry.id) << "\t"
                    << tsv_escape(diagnostic.route) << "\t"
                    << kind_name(target.kind) << "\t" << tsv_escape(target.id)
                    << "\t" << tsv_escape(target.resource) << "\n";
        }
      }
      std::cout << "# summary\tnamed=" << named << "\ttopics-naming="
                << topics_naming << "\ttopics=" << document.topics().size()
                << "\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }

  // Why each typed family turned a topic down. `--coverage` reports only the
  // decline that reached the reader, which names whichever check fired
  // first rather than the family that was closest to claiming the topic.
  // This reports every family's own reason for every topic, typed or not,
  // which is what a family-admission investigation needs.
  if (argc == 3 && std::string(argv[2]) == "--declines") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto inventory = document.typed_route_inventory();
      std::cout << "id\troute\tfamily\tdeclined\n";
      for (const auto& topic : inventory.topics) {
        for (const auto& declined : topic.declined)
          std::cout << tsv_escape(topic.id) << "\t"
                    << (topic.route == geist::detail::TypedRouteKind::typed
                            ? "typed"
                            : "legacy")
                    << "\t"
                    << tsv_escape(topic.family) << "\t"
                    << tsv_escape(declined) << "\n";
      }
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }

  // Does every word the source draws reach the render?  Coverage says which
  // route rendered a topic; this says whether the output is complete.  The
  // record's own display-line framing states what is drawn -- the length byte
  // that opens each line is the row-control slot and never display text, so it
  // is excluded whatever ordinary word the dictionary spells for it -- and the
  // ownership ledger removes the cells a control's opcode or operand consumes.
  // Everything left must survive into the Markdown.  A row per dropped word:
  // enumerating them is the point, since a count alone repeats the failure
  // this check exists to fix.
  //
  // `--rendered` adds one `# rendered` row per topic carrying the exact
  // Markdown text projection and HTML fragment the check compared, for the
  // corpus gate's style and structure fingerprints. The other rows are
  // unchanged, so the rendered rows can be split off before parsing them.
  // `--punctuation` reports the punctuation units (a word and the mark glued
  // to its end, `cc,`) the render does not carry: the word check cannot see
  // a lost comma.  A separate table, so the word report stays unchanged.
  if (argc == 4 && std::string(argv[2]) == "--conservation" &&
      std::string(argv[3]) == "--punctuation") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto report = document.drawn_word_conservation(false);
      std::cout << "topic\troute\tunit\tdrawn\temitted\tunaccounted"
                   "\tevidence\temitted_markdown\temitted_html\n";
      for (const auto& topic : report.topics)
        for (const auto& deficit : topic.punctuation_deficits)
          std::cout << tsv_escape(topic.id) << "\t" << tsv_escape(topic.route)
                    << "\t" << tsv_escape(deficit.unit) << "\t"
                    << deficit.drawn << "\t" << deficit.emitted << "\t"
                    << deficit.unaccounted << "\t"
                    << tsv_escape(deficit.evidence) << "\t"
                    << deficit.emitted_markdown << "\t"
                    << deficit.emitted_html << "\n";
      std::cout << "# summary\tchecked=" << report.topics_checked
                << "\tdropping-punctuation="
                << report.topics_dropping_punctuation
                << "\tunaccounted-punctuation="
                << report.unaccounted_punctuation << "\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }
  // `--rendered --punctuation` is the corpus gate's single pass: the word
  // report and the rendered rows, plus one `# punctuation` row per
  // punctuation unit a topic does not carry (topic, unit, drawn, emitted,
  // unaccounted).
  const auto gate_rows = argc == 5 && std::string(argv[3]) == "--rendered" &&
                         std::string(argv[4]) == "--punctuation";
  if ((argc == 3 || (argc == 4 && std::string(argv[3]) == "--rendered") ||
       gate_rows) &&
      std::string(argv[2]) == "--conservation") {
    try {
      const auto rendered = argc >= 4;
      const auto document = geist::BooDocument::open(argv[1]);
      const auto report = document.drawn_word_conservation(rendered);
      std::cout << "topic\troute\tword\tdrawn_opening\tdrawn_inline"
                   "\temitted\tunaccounted\tevidence\temitted_markdown"
                   "\tunaccounted_markdown\temitted_html\tunaccounted_html\n";
      for (const auto& topic : report.topics)
        std::cout << "# topic\t" << tsv_escape(topic.id) << "\t"
                  << tsv_escape(topic.source_kind) << "\t"
                  << topic.source_index << "\t"
                  << topic.source_start_logical_record << "\t"
                  << topic.source_end_logical_record << "\t"
                  << tsv_escape(topic.route) << "\n";
      if (rendered)
        for (const auto& topic : report.topics)
          std::cout << "# rendered\t" << tsv_escape(topic.id) << "\t"
                    << tsv_escape(topic.markdown_visible_text) << "\t"
                    << tsv_escape(topic.html_fragment) << "\n";
      if (gate_rows)
        for (const auto& topic : report.topics)
          for (const auto& deficit : topic.punctuation_deficits)
            std::cout << "# punctuation\t" << tsv_escape(topic.id) << "\t"
                      << tsv_escape(deficit.unit) << "\t" << deficit.drawn
                      << "\t" << deficit.emitted << "\t"
                      << deficit.unaccounted << "\n";
      for (const auto& topic : report.topics) {
        for (const auto& deficit : topic.deficits)
          std::cout << tsv_escape(topic.id) << "\t"
                    << tsv_escape(topic.route) << "\t"
                    << tsv_escape(deficit.word) << "\t"
                    << deficit.drawn_opening << "\t" << deficit.drawn_inline
                    << "\t" << deficit.emitted << "\t"
                    << deficit.unaccounted << "\t"
                    << tsv_escape(deficit.evidence) << "\t"
                    << deficit.emitted_markdown << "\t"
                    << deficit.unaccounted_markdown << "\t"
                    << deficit.emitted_html << "\t"
                    << deficit.unaccounted_html << "\n";
      }
      // Records whose payload does not tile into display lines have no decided
      // framing, so nothing about them is checked.  The count is reported
      // rather than folded into a clean result.
      for (const auto& topic : report.topics) {
        if (topic.unframed_records == 0) continue;
        std::cout << "# unframed\t" << tsv_escape(topic.id) << "\t"
                  << topic.unframed_records << "\t" << topic.records << "\n";
      }
      std::cout << "# summary\tchecked=" << report.topics_checked
                << "\tdropping=" << report.topics_dropping
                << "\tunaccounted=" << report.unaccounted_words
                << "\tforgiven=" << report.forgiven_words
                << "\tunframed-topics=" << report.topics_with_unframed_records
                << "\tmarkdown-dropping="
                << report.topics_dropping_markdown
                << "\tmarkdown-unaccounted="
                << report.unaccounted_markdown_words
                << "\thtml-dropping=" << report.topics_dropping_html
                << "\thtml-unaccounted=" << report.unaccounted_html_words
                << "\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }

  // Every revision mark the book's display lines store, one row per mark:
  // where it stands (a body row's margin or the `ST` revision column), the
  // character, and the line's display text.  The marks are what the source
  // draws; the census of what a reader does with them starts here.
  if (argc == 3 && std::string(argv[2]) == "--margin-marks") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto report = document.drawn_word_conservation(false);
      std::cout << "topic\troute\trecord\tline\tplace\tmark\ttext\n";
      std::size_t marks = 0;
      for (const auto& topic : report.topics)
        for (const auto& mark : topic.margin_marks) {
          ++marks;
          std::cout << tsv_escape(topic.id) << "\t" << tsv_escape(topic.route)
                    << "\t" << mark.logical_record << "\t" << mark.line << "\t"
                    << (mark.place == geist::detail::MarginMarkPlaceIR::title_column
                            ? "title"
                            : "row")
                    << "\t"
                    << (mark.mark < 0x80
                            ? std::string(1, static_cast<char>(mark.mark))
                            : "U+" + hex4(mark.mark))
                    << "\t" << tsv_escape(mark.source) << "\n";
        }
      std::cout << "# summary\tmarks=" << marks << "\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }

  if (argc == 3 && std::string(argv[2]) == "--coverage") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto inventory = document.typed_route_inventory();
      // `severity` and `degraded` are appended after the historical columns
      // so existing consumers keep their column positions. `route`, `family`
      // and `reason` are read out of the same RenderDiagnostic the exporter
      // and the `boo2git` manifest use.
      // `declined` is the whole per-family decline trace, appended last. The
      // `reason` column reports only the *last* family to speak (the prose
      // rejection, usually), so a specific family's refusal -- the glossary
      // catalog's, say -- was previously invisible on every topic another
      // family went on to claim.
      std::cout << "id\tlevel\troute\tfamily\treason\tclass\tsignature"
                   "\tseverity\tdegraded\tdeclined\tsource_kind"
                   "\tsource_index\tsource_start\tsource_end\n";
      for (const auto& topic : inventory.topics) {
        std::cout << tsv_escape(topic.id) << "\t" << topic.level << "\t"
                  << (topic.route == geist::detail::TypedRouteKind::typed
                          ? "typed"
                          : "legacy")
                  << "\t" << tsv_escape(topic.family) << "\t"
                  << tsv_escape(geist::detail::typed_route_reason(topic))
                  << "\t"
                  << tsv_escape(geist::detail::classify_topic_structure(
                         topic.id, topic.structure))
                  << "\t"
                  << tsv_escape(geist::detail::topic_structure_signature(
                         topic.structure))
                  << "\t" << geist::to_string(topic.diagnostic.severity)
                  << "\t"
                  << tsv_escape(geist::detail::format_render_degradations(
                         topic.diagnostic))
                  << "\t" << tsv_escape(join_declines(topic.declined))
                  << "\t" << topic.source_kind << "\t" << topic.source_index
                  << "\t" << topic.source_start_logical_record << "\t"
                  << topic.source_end_logical_record
                  << "\n";
      }
      std::cout << "# summary\ttyped=" << inventory.typed_count
                << "\tlegacy=" << inventory.legacy_count
                << "\ttotal=" << inventory.topics.size() << "\n";
      std::cout << "# severity";
      for (const auto& [severity, count] : inventory.by_severity)
        std::cout << "\t" << severity << "=" << count;
      std::cout << "\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "bootrace: " << error.what() << "\n";
      return 1;
    }
  }

  const std::string mode = argc == 4 ? argv[3] : "--all";
  const auto show_records = mode == "--all" || mode == "--records";
  const auto show_segments = mode == "--all" || mode == "--segments";
  const auto show_fonts = mode == "--all" || mode == "--fonts";
  const auto show_ir = mode == "--all" || mode == "--ir";
  const auto show_tokens = mode == "--tokens";
  const auto show_lines = mode == "--lines";
  if (!show_records && !show_segments && !show_fonts && !show_ir &&
      !show_tokens && !show_lines) {
    usage();
    return 2;
  }

  try {
    const auto document = geist::BooDocument::open(argv[1]);
    const auto normalized_topic_id = geist::detail::normalize_toc_id(argv[2]);
    const auto topic = std::find_if(
        document.topics().begin(), document.topics().end(),
        [&](const auto& candidate) {
          return candidate.id == normalized_topic_id ||
                 geist::detail::ascii_equals_case_insensitive(candidate.id,
                                                               argv[2]);
        });
    if (topic == document.topics().end()) {
      std::cerr << "bootrace: BOO topic id was not found: " << argv[2]
                << "\n";
      return 1;
    }

    const auto trace = document.trace_logical_records(argv[2]);

    const auto source_index = static_cast<std::size_t>(
        std::distance(document.topics().begin(), topic));
    std::cout << "# topic\t" << tsv_escape(topic->id) << "\t"
              << tsv_escape(topic->title) << "\n";
    std::cout << "# source\ttopic-index\t" << source_index << "\t"
              << topic->start_logical_record << "\t"
              << topic->end_logical_record << "\n";
    std::cout << "# logical_record_range\t" << topic->start_logical_record
              << "\t" << topic->end_logical_record << "\n";

    if (show_records) {
      std::cout << "# logical records\n";
      std::cout << "logical_record\tannotated_decoded_control_stream\n";
      for (const auto& record : trace) {
        std::cout << record.logical_record << "\t"
                  << tsv_escape(geist::detail::annotate_decoded_placeholders(
                         record.decoded_record))
                  << "\n";
      }
    }

    if (show_segments) {
      std::cout << "# decoded markup segments\n";
      std::cout << "logical_record\tsegment\tannotated_decoded_segment\n";
      for (const auto& record : trace) {
        for (std::size_t index = 0; index < record.segments.size(); ++index) {
          std::cout << record.logical_record << "\t" << index << "\t"
                    << tsv_escape(geist::detail::annotate_decoded_placeholders(
                           record.segments[index]))
                    << "\n";
        }
      }
    }

    if (show_fonts) {
      std::cout << "# font definitions\n";
      std::cout << "code\tstyle\n";
      for (const auto& [code, style] : document.font_definitions()) {
        std::cout << tsv_escape(code) << "\t" << tsv_escape(style) << "\n";
      }

      std::cout << "# font spans\n";
      std::cout << "logical_record\tsegment\tspan\toffset\tlength\tcode\t"
                   "style\ttext\tprojected_gml\n";
      for (const auto& record : trace) {
        for (const auto& span : record.font_spans) {
          std::cout << span.logical_record << "\t" << span.segment_index
                    << "\t" << span.span_index << "\t" << span.offset << "\t"
                    << span.length << "\t" << tsv_escape(span.code) << "\t"
                    << tsv_escape(span.style) << "\t"
                    << tsv_escape(span.text) << "\t"
                    << tsv_escape(span.projected_gml) << "\n";
        }
      }
    }

    if (show_tokens) {
      std::cout << "# source tokens\n";
      std::cout << "logical_record\tdetail\n";
      for (const auto& record : trace)
        for (const auto& token : record.ir_tokens)
          std::cout << record.logical_record << "\t" << tsv_escape(token)
                    << "\n";
    }

    if (show_lines) {
      std::cout << "# display lines\n";
      std::cout << "logical_record\tdetail\n";
      for (const auto& record : trace)
        for (const auto& line : record.ir_display_lines)
          std::cout << record.logical_record << "\t" << tsv_escape(line)
                    << "\n";
    }

    if (show_ir) {
      std::cout << "# typed source IR\n";
      std::cout << "logical_record\tkind\tdetail\n";
      for (const auto& record : trace) {
        for (const auto& segment : record.ir_control_segments)
          std::cout << record.logical_record << "\tsegment\t"
                    << tsv_escape(segment) << "\n";
        for (const auto& row : record.ir_physical_rows)
          std::cout << record.logical_record << "\trow\t"
                    << tsv_escape(row) << "\n";
        for (const auto& cell : record.ir_ownership_cells)
          std::cout << record.logical_record << "\townership\t"
                    << tsv_escape(cell) << "\n";
        for (const auto& block : record.ir_semantic_blocks)
          std::cout << record.logical_record << "\tsemantic\t"
                    << tsv_escape(block) << "\n";
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "bootrace: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
