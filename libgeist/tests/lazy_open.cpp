#include "geist/document.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << "\n";
    std::exit(1);
  }
}

} // namespace

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";
  const auto document = geist::BooDocument::open(root / "QS3X36CM.BOO");

  const auto& directory = document.directory();
  require(directory.content_page_index_offset == 0x0e82,
          "unexpected content-page index offset");
  require(directory.logical_record_count == 0x00f1,
          "unexpected logical-record count");
  require(directory.stream_table_offset == 0x0068,
          "unexpected topic-start index offset");
  require(directory.stream_table_count == 10,
          "unexpected topic count");
  require(document.topics().size() == 10,
          "lightweight topic index has the wrong size");

  const auto* intro = document.find_toc_entry("1.0");
  require(intro != nullptr, "missing 1.0 TOC entry");
  require(intro->raw_records.empty(),
          "opening eagerly rendered a TOC topic body");
  const auto markdown = intro->markdown();
  require(markdown.find("# 1.0 Introduction") != std::string::npos,
          "lazy TOC entry rendering produced unexpected Markdown");

  const auto packet = geist::BooDocument::open(root / "packet.boo");
  const auto index = packet.topic_markdown("INDEX");
  require(index.find("## A") != std::string::npos,
          "direct lazy topic rendering lost the generated index");

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
  require(discontinued_markdown.find("## 2.6 Discontinued Support") !=
              std::string::npos,
          "split-header topic lost its TOC heading");
  require(discontinued_markdown.find("LU6.1 adapter") != std::string::npos,
          "split-header topic lost its body");

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

  const auto gg24_edition = split_header.topic_markdown("EDITION");
  require(gg24_edition.find("**First Edition (February 1995)**") !=
              std::string::npos &&
              gg24_edition.find("Version 5, Release 1 of IMS/ESA") !=
                  std::string::npos &&
              gg24_edition.find(
                  "Copyright International Business Machines Corporation "
                  "1995. All rights reserved.") != std::string::npos,
          "GG24 edition notice did not retain its decoded edition data");
  require(gg24_edition.find("May 1991") == std::string::npos,
          "GG24 edition notice retained fixture-specific replacement text");
  require(gg24_edition.find(
              "MVS/ESA Operating System.\n\nOrder publications") !=
              std::string::npos &&
              gg24_edition.find("comments may be addressed to:\n\nIBM "
                                "Corporation") != std::string::npos &&
              gg24_edition.find("San Jose, California 95193-0001\n\nWhen "
                                "you send information") != std::string::npos,
          "GG24 edition notice lost its fixed-row paragraph boundaries");
  require(gg24_edition.find("95193-0001(") == std::string::npos &&
              gg24_edition.find("obligation to you.*") == std::string::npos &&
              gg24_edition.find("obligation to you. ©") == std::string::npos,
          "GG24 edition notice leaked visual markers or merged copyright");

  const auto client_server = split_header.topic_markdown("8.5.5");
  require(client_server.find(
              "products, **IMS** **CS/2** and **IMS** **CS** **for** "
              "**Windows**") != std::string::npos,
          "marker-led CFONT row tore client/server product names");
  const auto message_routing = split_header.topic_markdown("9.4.7");
  require(message_routing.find("<I>every</I> <I>system</I> <I>involved</I> "
                               "<I>in</I> <I>the</I> <I>processing</I>") !=
              std::string::npos &&
              message_routing.find("cfont ") == std::string::npos,
          "fixed example lost CFONT styling or leaked its control");
  require(message_routing.find("LU6.2 device") ==
              message_routing.rfind("LU6.2 device"),
          "fixed example duplicated its styled continuation");
  const auto gg24_introduction = split_header.topic_markdown("1.0");
  for (const auto* expected : {"**Cost** **reduction**",
                               "**Remote** **site** **contingency**",
                               "**Open** **and** **distributed** **systems**",
                               "**Added** **value** **with** **protected** "
                               "**investment**"}) {
    require(gg24_introduction.find(expected) != std::string::npos,
            "visual-separator CFONT row tore a highlighted phrase");
  }
  const auto command_language = split_header.topic_markdown("4.2.5");
  require(command_language.find("**KEYWD** *keyword*,LAST=NO|YES") !=
              std::string::npos &&
              command_language.find("KEYW`D DATA`") == std::string::npos &&
              command_language.find("**BASE,AL**") == std::string::npos,
          "fixed command rows retained partial-token CFONT spans");
  const auto device_addresses = split_header.topic_markdown("7.3");
  require(device_addresses.find(
              "**DFS0762I** **OSAM** **(TAPE|DASD)** "
              "**(READ|WRITE)** **ERROR**") != std::string::npos &&
              device_addresses.find("DF**S0762I") == std::string::npos,
          "fixed message row retained partial-word CFONT spans");

  const struct {
    const char* topic;
    const char* phrase;
  } heading_body_cases[] = {
      {"5.0", "This chapter describes the IMS Version 5.1 enhancements"},
      {"5.1.1", "RSR requires the services of DBRC"},
      {"7.0", "those enhancements that do not fall into any of the previous"},
      {"8.0", "This chapter brings IMS V5.1 into true perspective"},
      {"10.1", "Before IMS 5.1, CICS users had the choice"},
      {"11.0", "The ability to access common data from many systems"},
  };
  for (const auto& regression : heading_body_cases) {
    const auto topic = split_header.topic_markdown(regression.topic);
    require(topic.find(regression.phrase) != std::string::npos,
            "heading-attached topic prose was discarded");
  }

  const auto dbctl_table = split_header.topic_markdown("10.2");
  require(dbctl_table.find(
              "| Processing Cost Enhancements | N-way data sharing | ** |") !=
              std::string::npos,
          "explicit table row boundaries were merged");
  require(dbctl_table.find("| Dropped Local DL/1 support |  |  |") !=
              std::string::npos,
          "final explicit table rows were discarded");

  const auto exit_table = split_header.topic_markdown("3.6.2");
  for (const auto* expected : {"DFSERA30", "DFSERA40", "DFSERA50",
                               "DFSERA60", "DFSERA70", "DSHRDSSN",
                               "UNDO  PHYSICAL REPLACE"}) {
    require(exit_table.find(expected) != std::string::npos,
            "table or fixed report lost a representative field");
  }

  const auto image_figure = split_header.topic_markdown("5.1.8");
  require(image_figure.find("![Resource 9](resource:9)") != std::string::npos,
          "image-backed figure lost its resource");
  require(image_figure.find("```text") == std::string::npos,
          "image-backed figure retained its duplicate ASCII placeholder");
  const auto delayed_image = split_header.topic_markdown("8.5.3");
  require(delayed_image.find("![Resource 25](resource:25)") !=
              std::string::npos,
          "picture selector without inline display text lost its resource");
  require(delayed_image.find("```text") == std::string::npos,
          "picture selector retained its duplicate ASCII placeholder");

  const auto rmf_reports = split_header.topic_markdown("3.3.4");
  for (const auto* expected : {"END/SEC   12.03", "ENDED     10826",
                               "RESPONSE TIME BREAKDOWN",
                               "INTERVAL 14.59.579", "RESOURCE GROUP=*NONE",
                               "Figure 13."}) {
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
  require(preface.find("implementing an IMS system") != std::string::npos &&
              preface.find("PREFACE.4 Acknowledgments") != std::string::npos,
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

  const auto web_demo = geist::BooDocument::open(root / "XWEBDEMO.boo");
  const auto web_title = web_demo.topic_markdown("TITLE");
  require(web_title.find("IBM BookManager BookServer") != std::string::npos &&
              web_title.find("Document Number XWEBDEMO") !=
                  std::string::npos &&
              web_title.find("©IBM Corporation 1995, 1997") !=
                  std::string::npos,
          "generated title-page controls displaced visible text");
  for (const auto* leaked : {"c.sp 3p p c", "<>", "<IMAGE>"}) {
    require(web_title.find(leaked) == std::string::npos,
            "generated title-page presentation marker leaked into Markdown");
  }
  const auto web_introduction = web_demo.topic_markdown("1.0");
  require(web_introduction.find(
              "The past several years have seen dramatic growth") !=
              std::string::npos &&
              web_introduction.find(
                  "In particular a strong trend towards the use") !=
                  std::string::npos,
          "generated body-row controls displaced introduction text");
  for (const auto* leaked : {"c.sp 3p p c", "// The past", ":H1"}) {
    require(web_introduction.find(leaked) == std::string::npos,
            "generated body-row presentation marker leaked into Markdown");
  }
  const auto web_advantages = web_demo.topic_markdown("1.1");
  require(web_advantages.find(
              "own or from multiple remote file systems. The actual") !=
              std::string::npos &&
              web_advantages.find("Services/ESA. These products") !=
                  std::string::npos,
          "continued fixed row lost advantages prose");
  for (const auto* leaked : {":H2", "not - part", "readers. = Therefore",
                             "access ' books",
                             "booksrv2.raleigh.ibm.com",
                             "operating systems book"}) {
    require(web_advantages.find(leaked) == std::string::npos,
            "continued fixed-row marker leaked into Markdown");
  }
  const auto web_opening = web_demo.topic_markdown("1.4");
  require(web_opening.find(
              "**Note:** Your ability to view or play the various media "
              "objects will depend **on** **the** **hardware** **and** "
              "**software** **configuration** **of** **your** "
              "**workstation.**") != std::string::npos &&
              web_opening.find("depend across") == std::string::npos,
          "fixed note continuation retained a carryover word or split row");
  const auto* web_working = web_demo.find_toc_entry("1.3");
  const auto* web_data = web_demo.find_toc_entry("1.4.3");
  require(web_working != nullptr && web_data != nullptr &&
              web_working->title == "How BookServer Works" &&
              web_data->title == "Data and Software",
          "selector-kind metadata leaked into a TOC title");
  require(web_demo.topic_markdown("1.0").find(
              "![Image](/bookmgr/product.gif)") != std::string::npos,
          "external product image selector was malformed");
  const auto web_external_pictures = web_demo.topic_markdown("1.4.1");
  require(web_external_pictures.find(
              "![Image](/bookmgr/monetcoq.jpg)") != std::string::npos,
          "inline external JPEG selector was malformed");
  require(web_external_pictures.find(
              "](/bookmgr/monetley.jpg)") != std::string::npos,
          "linked external JPEG selector lost its target");
  require(web_external_pictures.find("[Figure 3](#FIGMONET1)") !=
              std::string::npos &&
              web_external_pictures.find("[Figure 2](#FIGOVERVIE)") !=
                  std::string::npos,
          "external-picture cross-reference labels were torn");
  require(web_external_pictures.find("<IMAGE>") == std::string::npos &&
              web_external_pictures.find("<OTHER>") == std::string::npos &&
              web_external_pictures.find(
                  "/ An exciting new capability") == std::string::npos,
          "external-picture selector alternatives leaked into prose");
  const auto web_multimedia = web_demo.topic_markdown("1.4.2");
  require(web_multimedia.find("[Warp us out of here!](/bookmgr/entprise.mpg)") !=
              std::string::npos &&
              web_multimedia.find("[scream!](/bookmgr/scream1.wav)") !=
                  std::string::npos,
          "external multimedia selectors lost labels or targets");
  require(web_demo.topic_markdown("1.4.3").find(
              "[here](ftp://software.raleigh.ibm.com/os2/internet/webexplorer)") !=
              std::string::npos,
          "external FTP selector lost its label or target");
  require(web_demo.topic_markdown("1.4.4").find(
              "[The IBM Home Page](http://www.ibm.com/)") !=
              std::string::npos,
          "external HTTP selector lost its label or target");
  const auto web_figures = web_demo.topic_markdown("FIGURES");
  require(web_figures.find("c.sp 3p p c") == std::string::npos,
          "generated figure-list spacing control leaked into Markdown");
  require(web_figures.find("[1. BookManager product family 1.2]") !=
              std::string::npos &&
              web_figures.find(
                  "[3. External JPEG format image presented in-line 1.4.1]") !=
                  std::string::npos,
          "external-picture figure index retained selector metadata");

  const auto configuration_manager =
      geist::BooDocument::open(root / "SC41-485.boo");
  const auto configuration_errors =
      configuration_manager.topic_markdown("1.2.5");
  for (const auto* expected : {"CPF24B4 E", "CPF26A8 E", "CPF26A9 E",
                               "CPF26AA E", "CPF3C21 E", "CPF3C90 E",
                               "CPF3CF1 E", "CPF9872 E"}) {
    require(configuration_errors.find("- **" + std::string(expected) +
                                      "** — ") != std::string::npos,
            "definition list lost an error-code association");
  }
  const auto configuration_actions =
      configuration_manager.topic_markdown("1.1");
  require(configuration_actions.find("- **List** — ") !=
              std::string::npos &&
              configuration_actions.find("- **Retrieve** — ") !=
                  std::string::npos &&
              configuration_actions.find("(QDCLCFGD)") !=
                  std::string::npos,
          "definition list lost an action description");
  const auto configuration_qualifiers =
      configuration_manager.topic_markdown("1.2.2");
  for (const auto* expected : {"***APPC** APPC controllers",
                               "***FR** Frame relay lines",
                               "***LANPRT** LAN printer devices",
                               "***OPT** Optical devices",
                               "***SNUF** SNA upline facility devices"}) {
    require(configuration_qualifiers.find(expected) != std::string::npos,
            "visual-bar CFONT row tore a configuration qualifier");
  }
  require(configuration_qualifiers.find("**|**") == std::string::npos,
          "visual-bar CFONT row highlighted its structural marker");
  require(configuration_qualifiers.find("cselect") == std::string::npos &&
              configuration_qualifiers.find("<BOOK>") == std::string::npos &&
              configuration_qualifiers.find("SC41-4801/4801") !=
                  std::string::npos,
          "cross-book table selector metadata leaked or lost its target");

  const auto sclm = geist::BooDocument::open(root / "SC34-425.boo");
  const auto sclm_messages = sclm.topic_markdown("APPENDIX1.5.3");
  require(sclm_messages.find("<pre>") != std::string::npos &&
              sclm_messages.find("FLM00000 MESSAGE ID") !=
                  std::string::npos &&
              sclm_messages.find("<a id=\"MSG FLM00101\"></a>") !=
                  std::string::npos &&
              sclm_messages.find("**FLM00000**") == std::string::npos,
          "SCLM message catalog was flattened into styled prose");
  const auto sclm_mnotes = sclm.topic_markdown("APPENDIX1.5.4");
  require(sclm_mnotes.find("<pre>") != std::string::npos &&
              sclm_mnotes.find("ACCT AND EXPACCT NAMES SAME") !=
                  std::string::npos &&
              sclm_mnotes.find("**ACCT**") == std::string::npos,
          "SCLM MNOTE catalog was flattened into styled prose");
  const auto sclm_glossary = sclm.topic_markdown("GLOSSARY");
  require(sclm_glossary.find("<pre>") != std::string::npos &&
              sclm_glossary.find("access key.  An identifier") !=
                  std::string::npos,
          "SCLM glossary lost its fixed-layout rows");
  const auto pli_example = sclm.topic_markdown("1.9.2");
  require(pli_example.find("```text") != std::string::npos &&
              pli_example.find("SCLM SERVICE PROCEDURES") !=
                  std::string::npos &&
              pli_example.find("**SCLM**") == std::string::npos,
          "fixed PL/I figure was rendered as inline emphasis");

  const auto sort_reference =
      geist::BooDocument::open(root / "PRG1SORT.boo");
  const auto collating = sort_reference.topic_markdown("C.1");
  require(collating.find("<a id=\"NCS\"></a>") != std::string::npos &&
              collating.find("<a id=\"EBCDIC2\"></a>") !=
                  std::string::npos &&
              collating.find("<a id=\"EBCDIC3\"></a>") !=
                  std::string::npos &&
              collating.find("```text") != std::string::npos &&
              collating.find("Order in") != std::string::npos,
          "collating-sequence figures lost their fixed rows");

  const auto smf_layout =
      geist::BooDocument::open(root / "SH12-565.boo")
          .topic_markdown("APPENDIX1.8");
  require(smf_layout.find("number of *triplets*. Each triplet") !=
              std::string::npos &&
              smf_layout.find("**2** Delete **3** Query") !=
                  std::string::npos,
          "SMF fixed rows retained shifted CFONT spans");
  require(smf_layout.find("trip*lets") == std::string::npos &&
              smf_layout.find("Del**e**te") == std::string::npos,
          "SMF fixed rows retained torn words");

  const auto rexx_tokens =
      geist::BooDocument::open(root / "SC24-546.boo")
          .topic_markdown("2.1.3");
  require(rexx_tokens.find("sequence including *any* characters") !=
              std::string::npos &&
              rexx_tokens.find("charac*ter*") == std::string::npos &&
              rexx_tokens.find("t`w`o") == std::string::npos,
          "REXX prose retained partial-word CFONT spans");

  const auto problem_determination =
      geist::BooDocument::open(root / "SC31-711.boo");
  const auto intended_audience =
      problem_determination.topic_markdown("PREFACE.1");
  require(intended_audience.find(
              "You should read this book if you are a network administrator, "
              "planner, or\n   operator who will use LNM for AIX.") !=
              std::string::npos &&
              intended_audience.find(
                  "Before reading this book, you should have a general "
                  "understanding of") != std::string::npos,
          "ST title repair lost PREFACE.1 fixed-body prose");
  require(intended_audience.find(
              "about LNM for AIX messages and traps.\n\n   Before reading") !=
              std::string::npos &&
              intended_audience.find("or >") == std::string::npos &&
              intended_audience.find("of )") == std::string::npos,
          "ST title repair lost a paragraph break or leaked row markers");
  const auto filters = problem_determination.topic_markdown("3.3");
  for (const auto* expected : {
           "event display. One kind of filter",
           "enterprise ID of the agent",
           "**Note:** Use care when making any changes",
           "Warning: Do not modify the filters",
       }) {
    require(filters.find(expected) != std::string::npos,
            "fixed filter prose lost text or semantic boundaries");
  }
  for (const auto* leaked : {"display.( One", "displayed.- You",
                             "event > cards", "of < the agent",
                             "action    Note", "application    Warning",
                             "directory, connection"}) {
    require(filters.find(leaked) == std::string::npos,
            "fixed filter row marker or carryover token leaked");
  }
  const auto customer_form = problem_determination.topic_markdown("2.4.1");
  require(customer_form.find("| Field | Value |") != std::string::npos &&
              customer_form.find("| Customer number |  |") !=
                  std::string::npos &&
              customer_form.find("| LNM for AIX component ID |  |") !=
                  std::string::npos &&
              customer_form.find("| Problem symptoms |  |") !=
                  std::string::npos,
          "fixed customer form lost its field/value rows");
  require(customer_form.find(", ,") == std::string::npos,
          "fixed customer form collapsed back into punctuation");
  const auto customer_checklist =
      problem_determination.topic_markdown("2.4.5");
  for (const auto* expected : {"- Customer number",
                               "- LNM for AIX component ID",
                               "- Describe the symptoms of the problem"}) {
    require(customer_checklist.find(expected) != std::string::npos,
            "fixed customer checklist lost an item");
  }
  require(customer_checklist.find("```text") == std::string::npos,
          "fixed customer checklist was emitted as a code block");
  const auto network_checklist =
      problem_determination.topic_markdown("2.4.8");
  for (const auto* expected : {
           "- Which AIX NetView/6000 applications were running",
           "- Number of stations", "- Number of bridges",
           "- Number of concentrators",
           "- Number of objects in the OVw database (use the command "
           "**ovobjprint** **|** **head**)",
           "- Number of objects to hold in ovwdb cache",
           "- Number of seconds between storing data"}) {
    require(network_checklist.find(expected) != std::string::npos,
            "complex fixed questionnaire lost an item or continuation");
  }
  const auto frame_relay = problem_determination.topic_markdown("4.3.5");
  require(frame_relay.find("This section lists the Frame Relay traps") !=
              std::string::npos &&
              frame_relay.find("- **1** — **Description:** DLCI state change") !=
                  std::string::npos,
          "message definition lost its introduction or label association");
  const auto token_ring_traps =
      problem_determination.topic_markdown("4.2.2");
  require(token_ring_traps.find(
              "- **5** — **Description:** The status of a port") !=
              std::string::npos &&
              token_ring_traps.find(
                  "display the current configuration and send the trap") !=
                  std::string::npos &&
              token_ring_traps.find(
                  "- **805306379** — **Description:** Temporary beaconing") !=
                  std::string::npos,
          "fixed trap rows retained torn CFONT spans");
  require(token_ring_traps.find("< Descrip") == std::string::npos &&
              token_ring_traps.find("**cur**r") == std::string::npos &&
              token_ring_traps.find("**configura**tion") ==
                  std::string::npos,
          "fixed trap rows leaked a marker or stale CFONT span");

  const auto generic_traps = problem_determination.topic_markdown("4.1.1");
  require(generic_traps.find(
              "- **2** — **Description:** linkDown **LNM** **for** **AIX** "
              "**Response:** Mark the agent unknown") != std::string::npos &&
              generic_traps.find("processed by the LNM OS/2 agent application") !=
                  std::string::npos,
          "SRMSG generic-trap rows or introduction were not reconstructed");
  require(generic_traps.find("SRMSG") == std::string::npos &&
              generic_traps.find("**>**") == std::string::npos &&
              generic_traps.find("then < attempt") == std::string::npos,
          "SRMSG metadata or fixed-row markers leaked into generic traps");

  const auto bridge_traps = problem_determination.topic_markdown("4.3.2");
  const auto bridge_intro = std::string(
      "These traps are defined under the 1.3.6.1.4.1.2.6.21.3.2 "
      "enterprise ID");
  require(bridge_traps.find(bridge_intro) != std::string::npos &&
              bridge_traps.find(bridge_intro) ==
                  bridge_traps.rfind(bridge_intro) &&
              bridge_traps.find("**256** "
                                "**(snmp_br_dot1dStpPortState)**") !=
                  std::string::npos,
          "bridge trap introduction duplicated or lost its message rows");
  require(bridge_traps.find("SRMSG") == std::string::npos &&
              bridge_traps.find("????????") == std::string::npos &&
              bridge_traps.find("resource name.)") == std::string::npos,
          "bridge trap metadata or trailing row marker leaked into Markdown");

  const auto redirected_traps =
      problem_determination.topic_markdown("4.3.4");
  require(redirected_traps.find("```text") == std::string::npos &&
              redirected_traps.find(
                  "- **1** — **Description:** Link Alarm") !=
                  std::string::npos &&
              redirected_traps.find("SRMSG") == std::string::npos &&
              redirected_traps.find("bridge application") !=
                  std::string::npos &&
              redirected_traps.find("bridge can application") ==
                  std::string::npos,
          "redirected trap catalog remained preformatted or leaked metadata");

  const auto symbolic_traps =
      problem_determination.topic_markdown("4.1.2");
  require(symbolic_traps.find("<pre>") == std::string::npos &&
              symbolic_traps.find("suggested actions. The traps") !=
                  std::string::npos &&
              symbolic_traps.find(
                  "<a id=\"MSG bridgeHistoryDataComplete\"></a>") !=
                  std::string::npos &&
              symbolic_traps.find("LNMOS2AgentNotResponding") !=
                  std::string::npos,
          "symbolic SRMSG catalog remained fixed or leaked its intro marker");

  const auto rmonitor_publications =
      problem_determination.topic_markdown("BACK_1.5");
  require(rmonitor_publications.find(
              "Using RMONitor for AIX (SC31-7115)") != std::string::npos &&
              rmonitor_publications.find(
                  "Using RMONitor Agent for OS/2 (SC31-7116)") !=
                  std::string::npos,
          "RMONitor publication CFONT rows disappeared during title repair");
  const auto sna_publications =
      problem_determination.topic_markdown("BACK_1.10");
  for (const auto* expected : {
           "AIX SNA Server/6000 User's Guide (SC31-7002)",
           "AIX SNA Server/6000 Configuration Reference (SC31-7014)",
           "AIX SNA Server/6000 Transaction Program Reference (SC31-7003)"}) {
    require(sna_publications.find(expected) != std::string::npos,
            "SNA publication CFONT row disappeared during title repair");
  }

  const auto performance_files =
      problem_determination.topic_markdown("1.4");
  require(performance_files.find(
              "`/usr/lpp/lnm/reports/lnmlnmemon/dir_name` directory") !=
              std::string::npos &&
              performance_files.find("c.cp") == std::string::npos,
          "performance-file prose lost its pending-font continuation");

  const auto lnm_messages = problem_determination.topic_markdown("5.0");
  for (const auto* expected : {
           "<a id=\"MSG 062\"></a>",
           "<a id=\"MSG 1000-1999\"></a>",
           "<a id=\"MSG 2389\"></a>",
           "<a id=\"MSG 2502\"></a>",
           "Restart** **the** **Concentrator** **view",
           "concentrator** **view** **is** **set** **to** **unknown",
           "has** **been** **removed** **from** **the** **database",
           "This message usually indicates that the other process failed",
       }) {
    require(lnm_messages.find(expected) != std::string::npos,
            "LNM message catalog lost an anchor or styled row text");
  }
  for (const auto* leaked : {
           "MSG 062 ", "Meaning:action", "Meaning:and", "AN Action",
           "Restart** **the** a **Concentrator", "view** agent **is",
           "removed** by **from", "iubd<",
       }) {
    require(lnm_messages.find(leaked) == std::string::npos,
            "LNM message catalog retained row carryover or metadata");
  }

  const auto lnm_glossary =
      problem_determination.topic_markdown("GLOSSARY");
  for (const auto* expected : {
           "<a id=\"GLS accelerator\"></a>",
           "<a id=\"GLS managed node\"></a>",
           "<a id=\"GLS wildcard character\"></a>",
       }) {
    require(lnm_glossary.find(expected) != std::string::npos,
            "LNM glossary lost its term-specific anchor");
  }
  require(lnm_glossary.find("adapter    active") == std::string::npos &&
              lnm_glossary.find("address    inactive") ==
                  std::string::npos &&
              lnm_glossary.find("alternative to a and") ==
                  std::string::npos &&
              lnm_glossary.find("may address    be stored") ==
                  std::string::npos &&
              lnm_glossary.find(
                  "GLS Consultative Committee on International Telegraph "
                  "and Telephone (CCITT) action") == std::string::npos,
          "LNM glossary retained fixed-row carryover");
  require(lnm_glossary.find("The IBM Dictionary of Computing") !=
              std::string::npos &&
              lnm_glossary.find(
                  "This glossary includes terms and definitions from:") ==
                  lnm_glossary.rfind(
                      "This glossary includes terms and definitions from:"),
          "LNM glossary introduction was lost or duplicated");
}
