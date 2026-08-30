#include "geist/boo.hpp"
#include "geist/detail/render_diagnostic_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/topic_link_targets.hpp"
#include "geist/detail/typed_route_inventory.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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
               "       bootrace <book.boo> <topic-id> --explain-offset <n>\n";
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
          std::cout << tsv_escape(document.decode_trace_slice(slice));
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
      // What each topic names, read off its Document IR. A topic that
      // renders verbatim names nothing and is reported with no rows.
      std::cout << "topic\troute\tkind\tid\tresource\n";
      std::size_t named = 0;
      std::size_t topics_naming = 0;
      for (const auto& entry : document.table_of_contents()) {
        const auto& live = entry.link_targets();
        if (!live.empty()) ++topics_naming;
        for (const auto& target : live) {
          ++named;
          std::cout << tsv_escape(entry.id) << "\t"
                    << tsv_escape(entry.render_diagnostic().route) << "\t"
                    << kind_name(target.kind) << "\t" << tsv_escape(target.id)
                    << "\t" << tsv_escape(target.resource) << "\n";
        }
      }
      std::cout << "# summary\tnamed=" << named << "\ttopics-naming="
                << topics_naming << "\n";
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
                   "\tseverity\tdegraded\tdeclined\n";
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
    const auto* entry = document.find_toc_entry(argv[2]);
    if (entry == nullptr) {
      std::cerr << "bootrace: BOO topic id was not found: " << argv[2]
                << "\n";
      return 1;
    }

    const auto trace = document.trace_logical_records(argv[2]);

    std::cout << "# topic\t" << tsv_escape(entry->id) << "\t"
              << tsv_escape(entry->title) << "\n";
    std::cout << "# logical_record_range\t" << entry->start_logical_record
              << "\t" << entry->end_logical_record << "\n";

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
