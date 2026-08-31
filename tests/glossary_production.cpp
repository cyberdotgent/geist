#include "geist/document.hpp"
#include "geist/detail/glossary_catalog_ir.hpp"
#include "geist/detail/glossary_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/layout_ir.hpp"
#include "geist/detail/ownership_ir.hpp"
#include "test_failures.hpp"

#include <filesystem>
#include <iostream>
#include <regex>
#include <string>

// The glossary family, on the one book that may be redistributed (#59).
//
// `packet` GLOSSARY is the acceptance fixture of issue #69.  Before that
// issue the glossary family declined every glossary in the corpus but one,
// and this topic fell through to the verbatim route: seven letter dividers
// reproduced as box art inside a fence, one anchor for the whole topic, and
// the operands of the `cz FLOW GD` region leaking into the text.
//
// The target shape is `SC31-711` GLOSSARY, which the repository owner
// verified against hosted BookServer, and which hosted reproduces here:
// `.../BOOKS/packet/GLOSSARY?DT=20260614112503` serves seven letter headings
// (`<hr><center><b><font size="+4">C</font></b></center><hr>`), eight
// `<a name="GLS ...">` anchors and no box art at all.

namespace {

void require(bool condition, const std::string &message) {
  if (condition)
    return;
  std::cerr << message << '\n';
  geist_test::record_failure();
}

std::size_t count(const std::string &text, const std::regex &pattern) {
  return static_cast<std::size_t>(
      std::distance(std::sregex_iterator(text.begin(), text.end(), pattern),
                    std::sregex_iterator()));
}

} // namespace

int main() {
  const auto book = std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
  const auto document = geist::BooDocument::open(book);
  const auto markdown = document.topic_markdown("GLOSSARY");

  // The topic is claimed, not merely reproduced.
  const auto *entry = document.find_toc_entry("GLOSSARY");
  require(entry != nullptr, "packet has no GLOSSARY topic");
  if (entry != nullptr)
    require(entry->render_diagnostic().severity == geist::RenderSeverity::typed,
            "packet GLOSSARY is not typed, it is " +
                std::string(geist::to_string(
                    entry->render_diagnostic().severity)));

  // One `## <letter>` heading per letter divider: the source draws the letter
  // as `| C |` box art, and hosted renders it as a heading.  This is the
  // documented exception to reproducing fixed-layout art verbatim.
  require(count(markdown, std::regex("(?:^|\n)## [A-Z]\n")) == 7,
          "packet GLOSSARY should carry seven letter headings");
  for (const auto *letter : {"C", "I", "L", "O", "P", "Q", "S"})
    require(markdown.find(std::string("\n## ") + letter + "\n") !=
                std::string::npos,
            std::string("letter heading ## ") + letter + " is missing");

  // No box art survives, in any form.
  require(markdown.find("|___|") == std::string::npos,
          "a letter divider's box art is still in the output");
  require(markdown.find("```") == std::string::npos,
          "the glossary is no longer a verbatim block, so it carries no fence");

  // One anchor per term, named as hosted names it, and the term set in
  // source order.
  require(count(markdown, std::regex("<a id=\"GLS ")) == 8,
          "packet GLOSSARY should carry one anchor per term");
  require(markdown.find("<a id=\"GLS \"") == std::string::npos,
          "the empty `GLS` anchor of a letter divider is still emitted");
  for (const auto *term : {"Constellation Diagram", "CSS", "ISM Band", "LoRa",
                           "OSI Model", "PSK", "QAM", "SNR"}) {
    require(markdown.find(std::string("<a id=\"GLS ") + term + "\">") !=
                std::string::npos,
            std::string("anchor GLS ") + term + " is missing");
    require(markdown.find(std::string("**") + term + ":**") !=
                std::string::npos,
            std::string("term ") + term + " is not a bold list term");
  }

  // The `cz FLOW GD` region draws nothing; its operands are the control's,
  // not the topic's words.
  require(markdown.find("FLOW") == std::string::npos,
          "a cz region's operands leaked into the glossary text");

  // Words the row model leaves outside every physical row are still the
  // definition's.  `used` sits between two rows of one display line, and
  // `40` is carried in a numeric marker slot; the verbatim route printed
  // both, so the typed route may not drop them.
  require(markdown.find("often used for ISM band") != std::string::npos,
          "the definition of CSS lost the word between two of its rows");
  require(markdown.find("for the 40 MHz band") != std::string::npos,
          "the definition of ISM Band lost its numeric marker-slot word");

  // The same shape read off the typed IR rather than the Markdown, so a
  // renderer change cannot quietly satisfy the assertions above.
  {
    geist::detail::LogicalDecodeContext context;
    context.bytes = geist::detail::read_file(book);
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
    context.decoded_records =
        geist::detail::decode_experimental_logical_records(
            context.bytes, context.directory, &context.record_payload_ranges);
    const auto sources = geist::detail::decode_logical_record_sources(
        context, entry->start_logical_record, entry->end_logical_record);
    const auto layout = geist::detail::extract_layout_ir(sources);
    std::string error;
    const auto ownership =
        geist::detail::build_verified_ownership_ir(sources, layout, &error);
    require(ownership.has_value(),
            "packet GLOSSARY ownership is not canonical: " + error);
    if (ownership) {
      const auto catalog = geist::detail::extract_glossary_catalog_ir(
          sources, layout, *ownership, &error);
      require(catalog.has_value(),
              "the glossary family declines packet GLOSSARY: " + error);
      if (catalog) {
        require(catalog->sections.size() == 7,
                "the catalog should carry seven letter sections");
        require(catalog->entries.size() == 8,
                "the catalog should carry eight terms");
        require(catalog->items.size() == 15,
                "sections and terms should interleave in source order");
        // packet's glossary has no introduction prose at all; the family
        // used to require the fully articulated citation-list shape of
        // SC31-711 and declined everything else.
        require(catalog->introduction.shape ==
                    geist::detail::GlossaryIntroductionShapeIR::paragraphs,
                "packet GLOSSARY has no citation list to claim");
        require(catalog->introduction.paragraphs.empty(),
                "packet GLOSSARY has no introduction prose");
        require(catalog->introduction.title == "Glossary",
                "the glossary title is not conserved");
        require(geist::detail::verify_glossary_catalog_ir(
                    sources, layout, *ownership, *catalog, &error),
                "the extracted catalog does not verify: " + error);
      }
    }
  }

  std::cout << "glossary production tests passed\n";
  return 0;
}
