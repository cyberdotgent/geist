#include "geist/boo.hpp"

#include <exception>
#include <iostream>
#include <string>

namespace {

void usage() {
  std::cerr << "usage: boorender <book.boo> [topic-id] (--md|--trace)\n";
}

std::string escape(const std::string& value) {
  std::string output;
  for (const auto ch : value) {
    if (ch == '\\') output += "\\\\";
    else if (ch == '\t') output += "\\t";
    else if (ch == '\r') output += "\\r";
    else if (ch == '\n') output += "\\n";
    else output.push_back(ch);
  }
  return output;
}

// One line per rendered output span: where it lands in the Markdown, what it
// is, which node produced it, and which BOO file bytes that node names.
void print_trace(const geist::BooDocument& document, const std::string& text,
                 const geist::RenderTrace& trace) {
  std::cout << "output\trole\treason\tnode\tderivation\tdetail\tsource\t"
               "rendered\tsource_text\n";
  for (const auto& span : trace.spans) {
    std::string source;
    std::string source_text;
    for (const auto& slice : span.slices) {
      if (!source.empty()) source += ',';
      source += "lr" + std::to_string(slice.logical_record) + ":tok" +
                std::to_string(slice.token_begin) + '-' +
                std::to_string(slice.token_end) + ":bytes" +
                std::to_string(slice.byte_begin) + '-' +
                std::to_string(slice.byte_end);
      if (slice.character_end != 0)
        source += ":chars" + std::to_string(slice.character_begin) + '-' +
                  std::to_string(slice.character_end);
      try {
        source_text += document.decode_trace_slice(slice);
      } catch (const std::exception& error) {
        source_text += "<unreadable: ";
        source_text += error.what();
        source_text += '>';
      }
    }
    std::cout << span.output_begin << '-' << span.output_end << '\t'
              << geist::render_trace_role_name(span.role) << '\t'
              << escape(span.reason) << '\t' << escape(span.node_path) << '\t'
              << escape(span.derivation) << '\t' << escape(span.origin_detail)
              << '\t' << source << '\t'
              << escape(text.substr(span.output_begin,
                                    span.output_end - span.output_begin))
              << '\t' << escape(source_text) << '\n';
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 3 && argc != 4) {
    usage();
    return 2;
  }

  try {
    const auto has_topic_id = argc == 4;
    const std::string mode = argv[has_topic_id ? 3 : 2];
    const auto document = geist::BooDocument::open(argv[1]);
    if (mode == "--md" || mode == "--trace") {
      const auto* entry =
          has_topic_id ? document.find_toc_entry(argv[2]) : nullptr;
      if (has_topic_id && entry == nullptr) {
        std::cerr << "boorender: BOO topic id was not found: " << argv[2]
                  << "\n";
        return 1;
      }
      if (mode == "--md") {
        std::cout << (has_topic_id ? entry->markdown() : document.markdown());
      } else {
        if (!has_topic_id) {
          std::cerr << "boorender: --trace needs a topic id\n";
          return 2;
        }
        geist::RenderTrace trace;
        const auto text = entry->markdown(trace);
        if (trace.spans.empty()) {
          std::cerr << "boorender: topic is rendered by the legacy route and "
                       "carries no typed provenance\n";
          return 1;
        }
        print_trace(document, text, trace);
      }
    } else {
      usage();
      return 2;
    }
  } catch (const std::exception& error) {
    std::cerr << "boorender: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
