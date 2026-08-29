#include "geist/boo.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/typed_route_inventory.hpp"

#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

void usage() {
  std::cerr << "usage: bootrace <book.boo> <topic-id> "
               "[--all|--records|--segments|--fonts|--ir|--tokens]\n"
               "       bootrace <book.boo> --coverage\n";
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
  if (argc != 3 && argc != 4) {
    usage();
    return 2;
  }

  if (argc == 3 && std::string(argv[2]) == "--coverage") {
    try {
      const auto document = geist::BooDocument::open(argv[1]);
      const auto inventory = document.typed_route_inventory();
      std::cout << "id\tlevel\troute\tfamily\treason\tclass\tsignature\n";
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
                  << "\n";
      }
      std::cout << "# summary\ttyped=" << inventory.typed_count
                << "\tlegacy=" << inventory.legacy_count
                << "\ttotal=" << inventory.topics.size() << "\n";
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
  if (!show_records && !show_segments && !show_fonts && !show_ir &&
      !show_tokens) {
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
      std::cout << "logical_record\tannotated_decoded_control_stream\t"
                   "normalized_gml_records\n";
      for (const auto& record : trace) {
        std::cout << record.logical_record << "\t"
                  << tsv_escape(geist::detail::annotate_decoded_placeholders(
                         record.decoded_record))
                  << "\t"
                  << tsv_escape(join_records(record.normalized_gml_records))
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
