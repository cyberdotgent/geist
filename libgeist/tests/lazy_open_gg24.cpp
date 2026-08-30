#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  // GG24-4302 stores the SH boundary for 2.6 separately from its CTopicN,
  // CHdLevel, and ST metadata.  The boundary must still resolve the TOC
  // entry to the correct topic body instead of extending 2.5 through 2.6.
  const auto split_header =
      geist::BooDocument::open(root / "GG24-4302-00.boo");
  const auto* discontinued = split_header.find_toc_entry("2.6");
  require(discontinued != nullptr, "missing split-header 2.6 TOC entry");
  require(discontinued->raw_records.empty(),
          "opening eagerly rendered split-header topic body");
  const auto discontinued_markdown = split_header.topic_markdown("2.6");
  require(discontinued_markdown.find("## 2\\.6 Discontinued Support") !=
              std::string::npos,
          "split-header topic lost its TOC heading");
  // The typed route keeps the two `<p>` paragraphs hosted BookServer serves
  // for this topic (DT 19950308184737); the legacy route ran them together.
  require(discontinued_markdown.find(
              "before migrating to IMS 5\\.1\\.\n\nThe LU6\\.1 adapter") !=
              std::string::npos,
          "split-header topic lost its body");
  const auto split_header_index = split_header.topic_markdown("INDEX");
  require(split_header_index.find(
              "## Special Characters\n\n"
              "- /DIS TRAN architected for OTMA, [6\\.1\\.2](<#6.1.2>)") !=
              std::string::npos &&
              split_header_index.find(
                  "- AOI callable services, [4\\.1\\.2\\.1](<#4.1.2.1>), "
                  "[4\\.1\\.2\\.3](<#4.1.2.3>)") != std::string::npos,
          "generated index lost punctuation terms or multiple targets");

  // Only the directory-declared content run contains topic logical records.
  // GG24 has later class-0x0001 pages belonging to another stream; decoding
  // those pages used to extend the final COMMENTS topic by 1,304 junk records.
  require(split_header.decoded_logical_records().size() ==
              split_header.directory().logical_record_count,
          "decoder included logical records outside the content run");
  const auto* comments = split_header.find_toc_entry("COMMENTS");
  require(comments != nullptr, "missing COMMENTS TOC entry");
  require(comments->end_logical_record - comments->start_logical_record == 4,
          "COMMENTS topic absorbed a non-content logical-record stream");
  const auto comments_markdown = comments->markdown();
  require(comments_markdown.size() < 5000,
          "COMMENTS topic expanded into fabricated structures");
  require(comments_markdown.find("questionnaire") != std::string::npos &&
              comments_markdown.find("QUALITY @ WTSCPOK") !=
                  std::string::npos,
          "COMMENTS topic lost its questionnaire body");

  // EDITION renders through the typed prose family (front-matter `vnotice`
  // heading form, hosted DT 19950308184737 serves it as `<H1> EDITION
  // Edition Notice</H1>`); the typed renderer escapes Markdown punctuation
  // and keeps the `\u00a9` glyph hosted prints before `Copyright`.
  const auto gg24_edition = split_header.topic_markdown("EDITION");
  require(gg24_edition.find("**First Edition \\(February 1995\\)**") !=
              std::string::npos &&
              gg24_edition.find("Version 5, Release 1 of IMS/ESA") !=
                  std::string::npos &&
              gg24_edition.find(
                  "Copyright International Business Machines Corporation "
                  "1995\\. All rights reserved\\.") != std::string::npos,
          "GG24 edition notice did not retain its decoded edition data");
  require(gg24_edition.find("May 1991") == std::string::npos,
          "GG24 edition notice retained fixture-specific replacement text");
  require(gg24_edition.find(
              "MVS/ESA Operating System\\.\n\nOrder publications") !=
              std::string::npos &&
              gg24_edition.find("comments may be addressed to:\n\nIBM "
                                "Corporation") != std::string::npos &&
              gg24_edition.find("San Jose, California 95193\\-0001\n\nWhen "
                                "you send information") != std::string::npos,
          "GG24 edition notice lost its fixed-row paragraph boundaries");
  require(gg24_edition.find("95193-0001(") == std::string::npos &&
              gg24_edition.find("obligation to you.*") == std::string::npos &&
              gg24_edition.find("obligation to you. ©") == std::string::npos,
          "GG24 edition notice leaked visual markers or merged copyright");

  const auto client_server = split_header.topic_markdown("8.5.5");
  // 8.5.5 now renders through the typed prose family: the `CFONT` phrase is
  // one emphasis inline over the whole styled run, as hosted BookServer
  // styles it (DT 19950308184737 `<B>IMS</B> <B>CS/2</B> and <B>IMS</B>
  // <B>CS</B> <B>for</B> <B>Windows</B>`), and the objective bullets that the
  // legacy route flowed into the paragraph are a real list.
  require(client_server.find("products, **IMS CS/2** and **IMS CS for "
                             "Windows**") != std::string::npos,
          "marker-led CFONT row tore client/server product names");
  require(client_server.find(
              "- Protect and build on the existing investment in IMS 3270 "
              "applications") != std::string::npos,
          "8.5.5 lost the objective list");
  // 9.4.7 renders through the typed prose family: the HP1 span over the
  // phrase is one emphasis inline, as hosted BookServer italicizes it (the
  // legacy route drew it as a per-word <I> example block).
  const auto message_routing = split_header.topic_markdown("9.4.7");
  require(message_routing.find("*every system involved in the processing*") !=
              std::string::npos &&
              message_routing.find("cfont ") == std::string::npos,
          "prose paragraph lost CFONT styling or leaked its control");
  require(message_routing.find("LU6.2 device") ==
              message_routing.rfind("LU6.2 device"),
          "fixed example duplicated its styled continuation");
  const auto gg24_introduction = split_header.topic_markdown("1.0");
  // Same phrase-level emphasis in 1.0's visual-separator rows.
  for (const auto* expected : {"**Cost reduction**",
                               "**Remote site contingency**",
                               "**Open and distributed systems**",
                               "**Added value with protected investment**"}) {
    require(gg24_introduction.find(expected) != std::string::npos,
            "visual-separator CFONT row tore a highlighted phrase");
  }
  const auto command_language = split_header.topic_markdown("4.2.5");
  require(command_language.find("**KEYWD** *keyword*,LAST=") !=
              std::string::npos &&
              command_language.find("KEYW`D DATA`") == std::string::npos &&
              command_language.find("**BASE,AL**") == std::string::npos,
          "fixed command rows retained partial-token CFONT spans");
  const auto device_addresses = split_header.topic_markdown("7.3");
  require(device_addresses.find("**DFS0762I OSAM ") != std::string::npos &&
              device_addresses.find("DF**S0762I") == std::string::npos,
          "fixed message row retained partial-word CFONT spans");

  const struct {
    const char* topic;
    const char* phrase;
  } heading_body_cases[] = {
      {"5.0", "This chapter describes the IMS Version 5"},
      {"5.1.1", "RSR requires the services of DBRC"},
      {"7.0", "those enhancements that do not fall into any of the previous"},
      {"8.0", "This chapter brings IMS V5\\.1 into true perspective"},
      {"10.1", "Before IMS 5\\.1, CICS users had the choice"},
      {"11.0", "The ability to access common data from many systems"},
  };
  for (const auto& regression : heading_body_cases) {
    const auto topic = split_header.topic_markdown(regression.topic);
    require(topic.find(regression.phrase) != std::string::npos,
            "heading-attached topic prose was discarded");
  }

  // Typed route (prose composition over the fixed-table block): the region is
  // reproduced line for line as hosted BookServer serves it inside
  // `<pre width="80">` (DT 19950308184737), rules included; the legacy
  // renderer merged the spanning group row into the row after it.
  const auto dbctl_table = split_header.topic_markdown("10.2");
  require(dbctl_table.find(
              "   | Processing Cost Enhancements                             "
              "                     |                   |                  |")
                  != std::string::npos &&
              dbctl_table.find(
                  "   | N-way data sharing                                   "
                  "                         |         ** "
                  "       |        **        |") != std::string::npos,
          "explicit table row boundaries were merged");
  require(dbctl_table.find(
              "   | Dropped Local DL/1 support                               "
              "                     |                   |         *        |")
                  != std::string::npos,
          "final explicit table rows were discarded");

  const auto exit_table = split_header.topic_markdown("3.6.2");
  for (const auto* expected : {"DFSERA30", "DFSERA40", "DFSERA50",
                               "DFSERA60", "DFSERA70", "DSHRDSSN",
                               "UNDO  PHYSICAL REPLACE"}) {
    require(exit_table.find(expected) != std::string::npos,
            "table or fixed report lost a representative field");
  }

  const auto image_figure = split_header.topic_markdown("5.1.8");
  // Typed route: the figure block keeps the caption as the image alt text
  // and the picture selector's resource id, as hosted BookServer serves it
  // (DT 19950308184737: `<img src=".../P9.GIF" alt="PICTURE 9">` under
  // `Figure 20. RSR Components`).
  require(image_figure.find("![Figure 20\\. RSR Components](<resource:9>)") !=
              std::string::npos,
          "image-backed figure lost its resource");
  require(image_figure.find("```text") == std::string::npos,
          "image-backed figure retained its duplicate ASCII placeholder");
  const auto delayed_image = split_header.topic_markdown("8.5.3");
  require(delayed_image.find("![Figure 42\\. TCP/IP Access](<resource:25>)") !=
              std::string::npos,
          "picture selector without inline display text lost its resource");
  require(delayed_image.find("```text") == std::string::npos,
          "picture selector retained its duplicate ASCII placeholder");

  const auto rmf_reports = split_header.topic_markdown("3.3.4");
  for (const auto* expected : {"END/SEC   12.03", "ENDED     10826",
                               "RESPONSE TIME BREAKDOWN",
                               "INTERVAL 14.59.579", "RESOURCE GROUP=*NONE",
                               "Figure 13\\."}) {
    require(rmf_reports.find(expected) != std::string::npos,
            "fixed report lost a representative row");
  }

  for (const auto& entry : split_header.table_of_contents()) {
    const auto markdown = entry.markdown();
    for (const auto* leaked : {"c.cc ", "cmenu", "cmitem", "cemenu",
                               "cfont ", "ctopicn", "cparent",
                               "cforwardlevel", "cbacklevel", "csummary",
                               "chdlevel", "csourcefn", ":H3", ":H4"}) {
      require(markdown.find(leaked) == std::string::npos,
              "generated BookManager control leaked into Markdown");
    }
  }
  const auto preface = split_header.topic_markdown("PREFACE");
  // PREFACE now renders through the typed prose family, which escapes
  // Markdown punctuation; hosted (DT 19950308184737) serves the same words.
  require(preface.find("implementing an IMS system") != std::string::npos &&
              preface.find("PREFACE\\.4 Acknowledgments") != std::string::npos,
          "control suppression discarded preface prose or menu entries");
  require(preface.find("```text") == std::string::npos,
          "generated menu prose remained in an unintended code fence");

  const auto product_overview =
      geist::BooDocument::open(root / "GG24-395.boo");
  for (const auto* topic : {"3.2.1", "3.2.3", "3.3.1", "3.3.3", "3.3.4",
                            "3.3.7", "3.3.8", "3.3.9", "3.3.10",
                            "3.3.11", "3.3.12", "3.3.13", "3.3.15",
                            "3.3.16", "3.3.18"}) {
    require(product_overview.topic_markdown(topic).find("](resource:") !=
                std::string::npos,
            "picture selector in a table lost its BOO resource");
  }
  require(product_overview.topic_markdown("3.3.7").find(
              "The NetView product has versions") != std::string::npos,
          "text following an image-bearing table was lost");
  require(product_overview.topic_markdown("3.3.11").find(
              "IBM Workstation Data Save Facility/VM") != std::string::npos,
          "image-bearing table prose split across records was lost");
  const auto distributed_security =
      product_overview.topic_markdown("3.3.15");
  require(distributed_security.find(
              "provides a consistent security administration interface") !=
              std::string::npos &&
              distributed_security.find("PICTURE 91") == std::string::npos,
          "image-bearing table prose was replaced by picture metadata");
}
