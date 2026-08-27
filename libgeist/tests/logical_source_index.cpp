#include "geist/detail/internal.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

bool catalog_contains(const geist::detail::PublicationCatalogIR &catalog,
                      const std::string &expected) {
  return std::any_of(catalog.entries.begin(), catalog.entries.end(),
                     [&](const auto &entry) {
                       return entry.text.find(expected) != std::string::npos;
                     });
}

bool catalog_entries_are_distinct(
    const geist::detail::PublicationCatalogIR &catalog, const std::string &left,
    const std::string &right) {
  auto left_entry = catalog.entries.size();
  auto right_entry = catalog.entries.size();
  for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
    if (catalog.entries[index].text.find(left) != std::string::npos)
      left_entry = index;
    if (catalog.entries[index].text.find(right) != std::string::npos)
      right_entry = index;
  }
  return left_entry < catalog.entries.size() &&
         right_entry < catalog.entries.size() && left_entry != right_entry;
}

std::size_t resident_kib() {
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmRSS:") {
      std::size_t value = 0;
      status >> value;
      return value;
    }
    std::getline(status, key);
  }
  return 0;
}

void verify_book(const std::filesystem::path &path, std::uint32_t first,
                 std::uint32_t end, bool benchmark = false,
                 std::optional<bool> expected_publication = std::nullopt) {
  const auto started = std::chrono::steady_clock::now();
  geist::detail::LogicalDecodeContext context;
  context.bytes = geist::detail::read_file(path);
  const auto directory_page = geist::detail::read_be16(context.bytes, 0);
  const auto base =
      static_cast<std::size_t>(directory_page) * geist::boo_page_size;
  context.directory.page_number = directory_page;
  context.directory.token_threshold = context.bytes[base + 0x14];
  context.directory.token_map_offset =
      geist::detail::read_be16(context.bytes, base + 0x22);
  context.directory.dictionary_start_page =
      geist::detail::read_be16(context.bytes, base + 0x28);
  context.directory.dictionary_page_count =
      geist::detail::read_be16(context.bytes, base + 0x2e);
  context.directory.logical_record_count =
      geist::detail::read_be16(context.bytes, base + 0x36);
  context.directory.content_page_count =
      geist::detail::read_be16(context.bytes, base + 0x38);
  context.directory.content_start_page =
      geist::detail::read_be16(context.bytes, base + 0x3a);

  context.decoded_records = geist::detail::decode_experimental_logical_records(
      context.bytes, context.directory, &context.record_payload_ranges);
  const auto opened = std::chrono::steady_clock::now();
  const auto open_rss = resident_kib();
  require(context.record_payload_ranges.size() ==
              context.decoded_records.size(),
          "payload index and decoded records diverged");
  require(context.record_payload_ranges.size() ==
              context.directory.logical_record_count,
          "payload index does not cover the directory record count");

  const auto sources =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto first_source = std::chrono::steady_clock::now();
  const auto source_rss = resident_kib();
  require(sources.size() == end - first,
          "candidate-local source slice has the wrong record count");
  const auto layout = geist::detail::extract_layout_ir(sources);
  std::string layout_error;
  const auto layout_valid =
      geist::detail::verify_layout_ir(sources, layout, &layout_error);
  require(layout_valid, layout_error.empty() ? "layout IR verification failed"
                                             : layout_error.c_str());
  const auto ownership = geist::detail::build_ownership_ir(sources, layout);
  std::string ownership_error;
  const auto ownership_valid = geist::detail::verify_ownership_ir(
      sources, layout, ownership, &ownership_error);
  require(ownership_valid, ownership_error.empty()
                               ? "ownership IR verification failed"
                               : ownership_error.c_str());
  const auto publication =
      geist::detail::extract_publication_catalog_ir(sources, layout, ownership);
  if (expected_publication)
    require(publication.has_value() == *expected_publication,
            *expected_publication
                ? "verified publication fixture failed semantic admission"
                : "cross-book prose/list negative entered publication IR");
  if (publication) {
    std::string publication_error;
    require(!publication->title_source_rows.empty() &&
                !publication->introduction_source_rows.empty(),
            "publication heading text lost physical-row provenance");
    if (path.filename() == "SC31-711.boo" && first == 519)
      require(publication->title_source_rows.front() ==
                  publication->introduction_source_rows.front(),
              "split publication heading did not retain its shared source row");
    require(geist::detail::verify_publication_catalog_ir(
                sources, layout, ownership, *publication, &publication_error),
            publication_error.empty()
                ? "publication catalog IR verification failed"
                : publication_error.c_str());
    auto publication_without_title_source = *publication;
    publication_without_title_source.title_source_rows.clear();
    require(!geist::detail::verify_publication_catalog_ir(
                sources, layout, ownership, publication_without_title_source),
            "publication verifier admitted missing title provenance");
    require(geist::detail::format_publication_catalog_ir(*publication)
                    .find("sources=") != std::string::npos,
            "publication catalog IR has no stable provenance projection");
  }
  if (path.filename() == "SC31-711.boo" && first == 435) {
    std::string glossary_error;
    const auto glossary = geist::detail::extract_glossary_introduction_ir(
        sources, layout, ownership, &glossary_error);
    require(glossary.has_value(),
            glossary_error.empty()
                ? "glossary introduction did not enter semantic IR"
                : glossary_error.c_str());
    require(glossary && glossary->title == "Glossary" &&
                glossary->lead.text ==
                    "This glossary includes terms and definitions from:" &&
                glossary->sources.size() == 5 &&
                glossary->cross_references.size() == 6,
            "glossary IR lost a semantic introduction section");
    require(glossary &&
                glossary->sources[0].text.find("ANSI X3.172-1990") !=
                    std::string::npos &&
                glossary->sources[1].text.find("2001 Pennsylvania") !=
                    std::string::npos &&
                glossary->sources[2].text.find("working papers") !=
                    std::string::npos &&
                glossary->sources[3].text ==
                    "The Network Working Group Request for Comments: 1208." &&
                glossary->sources[4].text.find(
                    "The IBM Dictionary of Computing") == 0,
            "glossary IR lost or contaminated a source citation");
    require(glossary &&
                glossary->cross_references.front().text.find(
                    "Contrast with:") == 0 &&
                glossary->cross_references.back().text.find(
                    "Deprecated term for:") == 0,
            "glossary IR lost a cross-reference explanation");
    require(glossary &&
                geist::detail::verify_glossary_introduction_ir(
                    sources, layout, ownership, *glossary, &glossary_error),
            glossary_error.empty() ? "glossary IR verification failed"
                                   : glossary_error.c_str());
    require(glossary &&
                geist::detail::format_glossary_introduction_ir(*glossary).find(
                    "sources=3:0") != std::string::npos,
            "glossary IR trace omitted physical-row provenance");
    if (glossary) {
      auto mutated = *glossary;
      mutated.sources.front().text += " changed";
      require(!geist::detail::verify_glossary_introduction_ir(
                  sources, layout, ownership, mutated),
              "glossary IR verifier admitted mutated semantic text");
    }
  }
  if (path.filename() == "SC31-711.boo" && first == 172) {
    std::string message_error;
    const auto catalog = geist::detail::extract_message_catalog_ir(
        sources, layout, ownership, &message_error);
    require(catalog.has_value(),
            message_error.empty() ? "message catalog did not enter semantic IR"
                                  : message_error.c_str());
    require(catalog && catalog->entries.size() == 396 &&
                catalog->entries.front().id == "023" &&
                catalog->entries[1].id == "062" &&
                catalog->entries.back().id == "2505",
            "message catalog IR lost its canonical entry sequence");
    require(catalog &&
                std::all_of(
                    catalog->entries.begin(), catalog->entries.end(),
                    [](const auto &entry) {
                      return entry.sections.size() == 2 &&
                             entry.sections[0].kind ==
                                 geist::detail::MessageSectionKind::meaning &&
                             entry.sections[1].kind ==
                                 geist::detail::MessageSectionKind::action;
                    }),
            "message catalog IR lost Meaning/Action section order");
    require(
        catalog &&
            std::all_of(
                catalog->entries.begin(), catalog->entries.end(),
                [](const auto &entry) {
                  if (entry.headline.text.empty())
                    return false;
                  std::map<geist::detail::MessageSourceRowIR, std::size_t>
                      owned;
                  for (const auto &row : entry.headline.source_rows)
                    ++owned[row];
                  for (const auto &continuation : entry.headline_continuations)
                    for (const auto &row : continuation.source_rows)
                      ++owned[row];
                  for (const auto &section : entry.sections) {
                    if (section.paragraphs.empty() ||
                        std::any_of(
                            section.paragraphs.begin(),
                            section.paragraphs.end(),
                            [](const auto &paragraph) {
                              return paragraph.text.empty() ||
                                     (paragraph.source_rows.empty() &&
                                      paragraph.source_segments.empty() &&
                                      paragraph.source_slices.empty());
                            }))
                      return false;
                    for (const auto &paragraph : section.paragraphs)
                      for (const auto &row : paragraph.source_rows)
                        ++owned[row];
                  }
                  for (const auto &row : entry.suppressed_source_rows)
                    ++owned[row];
                  if (owned.size() != entry.source_rows.size())
                    return false;
                  if (std::any_of(
                          entry.source_rows.begin(), entry.source_rows.end(),
                          [&](const auto &row) {
                            const auto found = owned.find(row);
                            return found == owned.end() || found->second != 1;
                          }))
                    return false;
                  return std::all_of(
                      owned.begin(), owned.end(),
                      [](const auto &item) { return item.second == 1; });
                }),
        "message entry ledgers do not conserve each physical row exactly once");
    const auto message_072 =
        std::find_if(catalog->entries.begin(), catalog->entries.end(),
                     [](const auto &entry) { return entry.id == "072"; });
    require(catalog && message_072 != catalog->entries.end() &&
                message_072->headline.text.find("System call connect failed") !=
                    std::string::npos &&
                message_072->sections[0].paragraphs.front().text.find(
                    "establish communication with a server") !=
                    std::string::npos &&
                message_072->sections[1].paragraphs.front().text.find(
                    "If this error becomes critical") != std::string::npos,
            "message catalog IR lost source-spanning headline or section text");
    require(catalog && std::any_of(
                           catalog->entries.begin(), catalog->entries.end(),
                           [](const auto &entry) {
                             return std::any_of(
                                 entry.sections.begin(), entry.sections.end(),
                                 [](const auto &section) {
                                   return section.recovered_record_continuation;
                                 });
                           }),
            "message catalog IR did not expose split-record label recovery");
    require(catalog &&
                geist::detail::verify_message_catalog_ir(
                    sources, layout, ownership, *catalog, &message_error),
            message_error.empty() ? "message catalog IR verification failed"
                                  : message_error.c_str());
    require(catalog && geist::detail::format_message_catalog_ir(*catalog).find(
                           "message_catalog entries=396") != std::string::npos,
            "message catalog IR trace omitted the canonical catalog size");
    if (catalog) {
      auto mutated = *catalog;
      mutated.entries.front().id = "24";
      require(!geist::detail::verify_message_catalog_ir(sources, layout,
                                                        ownership, mutated),
              "message catalog verifier admitted a mutated message ID");
      mutated = *catalog;
      mutated.entries.front().sections.front().paragraphs.front().text +=
          " changed";
      require(!geist::detail::verify_message_catalog_ir(sources, layout,
                                                        ownership, mutated),
              "message catalog verifier admitted mutated section text");
    }
  }
  for (std::size_t index = 0; index < sources.size(); ++index) {
    const auto logical_record = first + index;
    require(sources[index].logical_record == logical_record,
            "source slice lost logical-record ownership");
    std::string ir_error;
    require(geist::detail::verify_token_ir(sources[index].ir, &ir_error),
            ir_error.empty() ? "token IR verification failed"
                             : ir_error.c_str());
    require(sources[index].ir.logical_record == logical_record,
            "token IR lost logical-record ownership");
    require(geist::detail::project_token_words(sources[index].ir) ==
                sources[index].tokens,
            "resolved token compatibility projection diverged from token IR");
    require(geist::detail::project_encoded_tokens(sources[index].ir) ==
                sources[index].encoded_tokens,
            "encoded token compatibility projection diverged from token IR");
    require(sources[index].encoded_tokens.size() ==
                sources[index].tokens.size(),
            "encoded and resolved token streams diverged");
    const auto &range = context.record_payload_ranges[logical_record - 1];
    auto payload_offset = static_cast<std::size_t>(range.begin);
    for (const auto &encoded : sources[index].encoded_tokens) {
      require(encoded.width == 1 || encoded.width == 2,
              "encoded token width is invalid");
      std::uint16_t value = context.bytes[payload_offset++];
      if (encoded.width == 2) {
        value = static_cast<std::uint16_t>((value << 8) |
                                           context.bytes[payload_offset++]);
      }
      require(value == encoded.value,
              "encoded token identity differs from payload bytes");
    }
    require(payload_offset == range.end,
            "encoded tokens do not consume the exact payload slice");
    require(
        geist::detail::token_words_to_ascii(sources[index].assembled.words) ==
            context.decoded_records[logical_record - 1],
        "source assembly differs from initial record decode");
    std::string segment_error;
    require(geist::detail::verify_control_segments(
                sources[index].assembled, sources[index].control_segments,
                &segment_error),
            segment_error.empty() ? "control segment verification failed"
                                  : segment_error.c_str());
  }

  if (path.filename() == "SC31-711.boo" && first == 528) {
    require(publication.has_value(),
            "FDDI publication stream did not enter the semantic IR");
    require(publication->entries.size() == 7,
            "FDDI publication IR did not preserve seven publications");
    require(catalog_contains(*publication,
                             "X3T9/90-X3T9.5/84-49 REV 6.2 May 18, 1990") &&
                catalog_contains(*publication,
                                 "X3T9/92-X3T9.5/84-49 REV 7.2 June 25, 1992"),
            "FDDI publication IR merged or lost the independent ANSI rows");
    auto non_c_sources = sources;
    for (auto &record : non_c_sources) {
      for (const auto &segment : record.control_segments) {
        if (segment.kind != geist::detail::BookControlKind::font)
          continue;
        for (auto word = segment.operand_range.begin;
             word < segment.operand_range.end; ++word) {
          if (record.assembled.words[word] == 'C')
            record.assembled.words[word] = 'E';
        }
      }
    }
    require(geist::detail::extract_publication_catalog_ir(
                non_c_sources, layout, ownership)
                .has_value(),
            "font operand spelling incorrectly remained publication semantic "
            "evidence");
    auto mismatched_layout = layout;
    const auto font_run = std::find_if(
        mismatched_layout.runs.begin(), mismatched_layout.runs.end(),
        [](const auto &run) {
          return run.control_kind == geist::detail::BookControlKind::font &&
                 !run.rows.empty();
        });
    require(font_run != mismatched_layout.runs.end(),
            "publication fixture has no font run to mutate");
    if (font_run != mismatched_layout.runs.end()) {
      ++font_run->rows.front().segment_index;
      require(!geist::detail::extract_publication_catalog_ir(
                  sources, mismatched_layout, ownership),
              "publication admission ignored an inexact source/run envelope");
    }
    auto opaque_ownership = ownership;
    const auto visible_cell = std::find_if(
        opaque_ownership.cells.begin(), opaque_ownership.cells.end(),
        [](const auto &cell) {
          return cell.disposition ==
                     geist::detail::SourceDisposition::visible_content &&
                 cell.run != 0;
        });
    require(visible_cell != opaque_ownership.cells.end(),
            "publication fixture has no visible ownership cell to mutate");
    if (visible_cell != opaque_ownership.cells.end()) {
      visible_cell->disposition = geist::detail::SourceDisposition::opaque;
      require(!geist::detail::extract_publication_catalog_ir(
                  sources, layout, opaque_ownership),
              "publication admission ignored opaque printable ownership");
    }
    std::size_t ansi_rows = 0;
    for (const auto &run : layout.runs) {
      for (const auto &row : run.rows) {
        if (row.marker && row.marker->decoded_text == "bridge" &&
            row.visible_text.find("American National Standards Institute") !=
                std::string::npos) {
          ++ansi_rows;
        }
      }
    }
    require(ansi_rows == 2,
            "layout IR did not preserve the two independent ANSI rows");
  }
  if (path.filename() == "SC31-711.boo" && first == 70) {
    const std::map<std::string, std::string> titles{
        {"2.4.1", "Customer Information"},
        {"2.4.2", "Software Version Levels and Applied PTFs on the LNM for "
                  "AIX Workstation"},
        {"2.4.3", "Hardware Configuration of the LNM for AIX Workstation"},
        {"2.4.4", "AIX NetView/6000 Considerations"},
        {"2.4.5", "Customer Information"},
        {"2.4.6", "Software Version Levels and Applied PTFs on the LNM for "
                  "AIX Workstation"},
        {"2.4.7", "Hardware Configuration on the LNM for AIX Workstation"},
        {"2.4.8", "AIX NetView/6000 Considerations"},
        {"2.4.9", "Additional Problem Information"},
    };
    std::string menu_error;
    const auto menu =
        geist::detail::extract_menu_ir(sources, titles, &menu_error);
    require(menu.has_value(), menu_error.empty() ? "CMENU did not enter menu IR"
                                                 : menu_error.c_str());
    require(menu && menu->items.size() == titles.size(),
            "menu IR did not preserve all CMITEM targets");
    require(menu &&
                std::count_if(menu->items.begin(), menu->items.end(),
                              [](const auto &item) {
                                return item.terminal_marker_token.has_value();
                              }) == 4,
            "menu IR did not isolate the four terminal source tokens");
    require(menu && geist::detail::verify_menu_ir(sources, titles, *menu,
                                                  &menu_error),
            menu_error.empty() ? "menu IR verification failed"
                               : menu_error.c_str());
    require(menu && geist::detail::format_menu_ir(*menu).find(
                        "terminal_marker_token=") != std::string::npos,
            "menu IR trace omitted terminal-token provenance");
    if (menu) {
      auto mutated = *menu;
      mutated.items.front().text += " changed";
      require(!geist::detail::verify_menu_ir(sources, titles, mutated),
              "menu IR verifier admitted mutated semantic text");
    }
    auto incorrect_titles = titles;
    incorrect_titles["2.4.5"] = "Incorrect Canonical Title";
    require(!geist::detail::extract_menu_ir(sources, incorrect_titles),
            "menu IR admitted a payload that did not match its canonical "
            "target title");
  }
  if (path.filename() == "SC31-711.boo" && first == 519) {
    require(publication.has_value(),
            "general publication stream did not enter the semantic IR");
    if (publication && !catalog_contains(*publication, "management problems")) {
      for (const auto &entry : publication->entries)
        std::cerr << "publication entry: " << entry.text << '\n';
    }
    require(catalog_contains(*publication, "management problems"),
            "publication IR lost its cross-record continuation");
    const auto continuation = std::find_if(
        layout.runs.begin(), layout.runs.end(), [](const auto &run) {
          return std::any_of(
              run.rows.begin(), run.rows.end(), [](const auto &row) {
                return row.logical_record == 520 &&
                       row.continues_previous_record &&
                       row.visible_text.find("management problems") !=
                           std::string::npos;
              });
        });
    if (continuation == layout.runs.end()) {
      for (const auto &run : layout.runs) {
        for (const auto &row : run.rows) {
          std::cerr << geist::detail::format_physical_row_ir(row) << '\n';
        }
      }
    }
    require(continuation != layout.runs.end(),
            "layout IR lost the LR519-to-LR520 publication continuation");
  }
  if (path.filename() == "SC31-711.boo" && first == 537) {
    require(publication.has_value(),
            "bridge publication stream did not enter the semantic IR");
    require(publication->entries.size() == 5 &&
                catalog_contains(*publication, "IBM 8229 Bridge Manual") &&
                catalog_contains(*publication, "GG24-4334"),
            "bridge publication IR lost its markerless or final entry");
    const auto markerless = std::any_of(
        layout.runs.begin(), layout.runs.end(), [](const auto &run) {
          return std::any_of(
              run.rows.begin(), run.rows.end(), [](const auto &row) {
                return row.start == geist::detail::PhysicalRowStartKind::
                                        control_payload &&
                       row.visible_text.find("IBM 8229 Bridge Manual") !=
                           std::string::npos;
              });
        });
    if (!markerless) {
      for (const auto &run : layout.runs) {
        for (const auto &row : run.rows) {
          std::cerr << geist::detail::format_physical_row_ir(row) << '\n';
        }
      }
    }
    require(markerless,
            "layout IR lost the markerless LR537 control-payload row");
  }
  if (path.filename() == "SC31-711.boo" && first == 524) {
    require(publication.has_value() && publication->entries.size() == 1 &&
                catalog_contains(*publication, "SH11-3067"),
            "hub publication IR lost its markerless source row");
  }
  if (path.filename() == "SC31-711.boo" && first == 526) {
    require(publication.has_value() &&
                catalog_contains(*publication, "SZ27-3710") &&
                catalog_contains(*publication, "GA27-3905"),
            "token-ring publication IR lost a first or wrapped row");
  }
  if (path.filename() == "SC31-711.boo" && first == 529) {
    require(publication.has_value() &&
                catalog_contains(*publication, "GC23-2201") &&
                catalog_contains(*publication, "GC23-2203"),
            "AIX publication IR lost a first or wrapped row");
  }
  if (path.filename() == "SC31-711.boo" && first == 535) {
    require(publication.has_value() &&
                catalog_entries_are_distinct(
                    *publication, "Prentice-Hall, 1989",
                    "Programming and Applications with Xt"),
            "X Window publication IR merged independent publications");
  }
  if (path.filename() == "GG24-395.boo" && first == 580) {
    require(publication.has_value() && publication->entries.size() == 2 &&
                catalog_contains(*publication, "SH24-5264") &&
                catalog_contains(*publication, "GH24-5259"),
            "LFS/ESA publication IR lost a cross-book catalog entry");
  }
  if (path.filename() == "SC09-138.boo" && first == 2267) {
    require(publication.has_value() && publication->entries.size() == 9 &&
                catalog_contains(*publication, "SC09-1308") &&
                catalog_contains(*publication, "GC09-1417"),
            "C/370 publication IR lost a cross-book catalog entry");
  }
  if (path.filename() == "QSYSINFO.BOO" && first == 743) {
    require(publication.has_value() && publication->entries.size() == 4 &&
                catalog_contains(*publication, "SX41-0005") &&
                catalog_contains(*publication, "SC41-0008") &&
                catalog_contains(*publication, "SX41-0007") &&
                catalog_contains(*publication, "SC41-0009"),
            "QSYSINFO manual catalog lost an independent publication entry");
    require(std::all_of(publication->entries.begin(),
                        publication->entries.end(), [](const auto &entry) {
                          return entry.paragraphs.size() == 1 &&
                                 entry.text.rfind("PC Support/400:", 0) == 0 &&
                                 entry.text.find('$') == std::string::npos;
                        }),
            "QSYSINFO manual names and citations lost their aligned fields");
  }

  const auto *cached_dictionary = context.source_dictionary.get();
  const auto repeated =
      geist::detail::decode_logical_record_sources(context, first, end);
  const auto repeated_source = std::chrono::steady_clock::now();
  require(context.source_dictionary.get() == cached_dictionary &&
              repeated.size() == sources.size(),
          "repeated source query did not reuse dictionary state");
  if (benchmark) {
    const auto millis = [](auto begin, auto finish) {
      return std::chrono::duration_cast<std::chrono::microseconds>(finish -
                                                                   begin)
                 .count() /
             1000.0;
    };
    std::cout << path.filename().string()
              << " open_ms=" << millis(started, opened)
              << " first_source_ms=" << millis(opened, first_source)
              << " repeat_source_ms=" << millis(first_source, repeated_source)
              << " open_rss_kib=" << open_rss
              << " source_rss_kib=" << source_rss << " index_bytes="
              << context.record_payload_ranges.size() *
                     sizeof(geist::detail::LogicalRecordPayloadRange)
              << "\n";
  }
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const bool benchmark = std::getenv("GEIST_BENCH_SOURCE_INDEX") != nullptr;
  if (std::getenv("GEIST_PUBLICATION_IR_ONLY") != nullptr) {
    verify_book(root / "SC31-711.boo", 528, 529, benchmark, true);
    verify_book(root / "GG24-395.boo", 580, 581, benchmark, true);
    verify_book(root / "SC09-138.boo", 2267, 2270, benchmark, true);
    verify_book(root / "QSYSINFO.BOO", 743, 744, benchmark, true);
    verify_book(root / "SG24-204.boo", 18, 19, benchmark, false);
    verify_book(root / "QS3X36CM.BOO", 7, 9, benchmark, false);
    return 0;
  }
  verify_book(root / "SC31-711.boo", 19, 21, benchmark);
  verify_book(root / "SC31-711.boo", 22, 24, benchmark);
  verify_book(root / "SC31-711.boo", 70, 71, benchmark);
  verify_book(root / "SC31-711.boo", 172, 435, benchmark);
  verify_book(root / "SC31-711.boo", 435, 438, benchmark);
  verify_book(root / "SC31-711.boo", 519, 521, benchmark);
  verify_book(root / "SC31-711.boo", 524, 525, benchmark);
  verify_book(root / "SC31-711.boo", 526, 528, benchmark);
  verify_book(root / "SC31-711.boo", 528, 529, benchmark);
  verify_book(root / "SC31-711.boo", 529, 530, benchmark);
  verify_book(root / "SC31-711.boo", 535, 536, benchmark);
  verify_book(root / "SC31-711.boo", 537, 538, benchmark);
  verify_book(root / "SG24-204.boo", 1, 2, benchmark);
  verify_book(root / "GG24-395.boo", 580, 581, benchmark, true);
  verify_book(root / "SC09-138.boo", 2267, 2270, benchmark, true);
  verify_book(root / "ACPZMST1.boo", 17, 18, benchmark, false);
  verify_book(root / "FA1PLMM0.boo", 600, 601, benchmark, false);
  verify_book(root / "SC26-457.boo", 36, 37, benchmark, false);
  verify_book(root / "SG24-204.boo", 481, 482, benchmark, false);
  verify_book(root / "QSYSINFO.BOO", 743, 744, benchmark, true);
  verify_book(root / "SG24-204.boo", 18, 19, benchmark, false);
  // This AS/400 prose topic has an H2 title followed by cfont rows, but its
  // source envelope contains an independent text run. It was formerly rejected
  // incidentally by non-C font operands and must remain outside publication IR
  // now that style spelling is deliberately irrelevant.
  verify_book(root / "QS3X36CM.BOO", 7, 9, benchmark, false);
}
