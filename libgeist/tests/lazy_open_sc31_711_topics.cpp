#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto problem_determination =
      geist::BooDocument::open(root / "SC31-711.boo");
  // NOTICES is a drawn `___ Note ___` box: the region's rows now lower as a
  // preformatted block that reproduces hosted (DT 19941010174546) character
  // for character inside its `<pre>`, so the `CSELECT` link and the `<B>`
  // label are dropped exactly as they are in every other drawn box.
  const auto notices = problem_determination.topic_markdown("NOTICES");
  require(notices.find(
              "Before using this document, read the general information") !=
                  std::string::npos &&
              notices.find("\"Notices\" in topic FRONT_1") != std::string::npos,
          "ordinary CFONT overflow trimming dropped notice prose");
  require(notices.find("___ Note ___") != std::string::npos,
          "NOTICES lost its drawn box outline");
  const auto how_to_use =
      problem_determination.topic_markdown("PREFACE.2");
  require(how_to_use.find("[action Chapter 3") == std::string::npos &&
              how_to_use.find("[adapter Chapter 4") == std::string::npos &&
              how_to_use.find(
                  "[Chapter 3, \"Understanding Logs, Traps, and Filters\"") !=
                  std::string::npos &&
              how_to_use.find("[Chapter 4, \"Traps\" in topic 4.0]") !=
                  std::string::npos,
          "selector display rows retained source-owned marker tokens");
  for (const auto* nested : {
           "2.1.1", "2.1.2", "2.1.3", "2.1.4", "4.1.1", "4.1.2",
           "4.1.3", "4.2.1", "4.2.2", "4.3.1", "4.3.2", "4.3.3",
           "4.3.4", "4.3.5",
       }) {
    require(markdown_visible_text(problem_determination.topic_markdown(nested))
                    .find(std::string("### ") + nested + " ") !=
                std::string::npos,
            "leading anchor displaced a nested topic heading");
  }
  const auto trademarks =
      problem_determination.topic_markdown("FRONT_1.1");
  // FRONT_1.1 renders through the typed prose family (the `c.cp` control it
  // carries has display text after its operand); the renderer escapes
  // Markdown punctuation.  Word for word equal to hosted DT 19941010174546
  // and to the legacy route.
  require(trademarks.find(
              "The following terms, denoted by a double asterisk "
              "\\(\\*\\*\\) at "
              "their first occurrence in this publication, are trademarks "
              "of other companies:") != std::string::npos,
          "visible CCP trademark paragraph was dropped");
  // The box renders verbatim, line for line with hosted, which serves it
  // inside `<pre width="80">` and emits no `<table>` element.
  for (const auto* pair : {
           "   | IBM                                | NetView               "
           "            |",
           "   | AIX                                | SystemView            "
           "            |",
           "   | PS/2                               | OS/2                  "
           "            |",
           "   | RISC System/6000                   | RS/6000               "
           "            |",
           "   | NETCENTER                          | RT                    "
           "            |",
       }) {
    require(trademarks.find(pair) != std::string::npos,
            "headerless trademark box lost a paired row");
  }
  require(trademarks.find("| Field | Value |") == std::string::npos &&
              trademarks.find("| IBM |  |") == std::string::npos &&
              trademarks.find("| NetView |  |") == std::string::npos,
          "headerless trademark box retained the legacy singleton schema");
  // The second, rule-less `Term / Trademark of` grid is declined by the
  // fixed-table block, so the prose family reflows its display rows into one
  // paragraph.  Hosted serves them as aligned `<pre>` columns; no word is
  // lost, but the column structure is (recorded in
  // AnalysisNotes/prose-topic-front-matter-2026-08-29.md as the one
  // structural regression of that slice).
  require(trademarks.find(
              "*Term Trademark of* DynaText Electronic Book Technologies, "
              "Inc\\. Motif Open Software Foundation, Inc\\.") !=
              std::string::npos,
          "terminal styled grid lost a heading or a paired row");
  // PREFACE.1 renders through the typed prose family: the two hosted
  // paragraphs flow (row markers `>` / `)` are typed marker slots) and the
  // renderer escapes sentence punctuation.
  const auto intended_audience =
      problem_determination.topic_markdown("PREFACE.1");
  require(intended_audience.find(
              "You should read this book if you are a network administrator, "
              "planner, or operator who will use LNM for AIX\\.") !=
              std::string::npos &&
              intended_audience.find(
                  "Before reading this book, you should have a general "
                  "understanding of") != std::string::npos,
          "typed prose lost PREFACE.1 body prose");
  require(intended_audience.find(
              "about LNM for AIX messages and traps\\.\n\nBefore reading") !=
              std::string::npos &&
              intended_audience.find("or >") == std::string::npos &&
              intended_audience.find("of )") == std::string::npos,
          "typed prose lost a paragraph break or leaked row markers");
  const auto generated_index = problem_determination.topic_markdown("INDEX");
  for (const auto* expected : {
           "- adapter problems, [2\\.2\\.4](<#2.2.4>)",
           "    - deleting agents, [2\\.3\\.1\\.5](<#2.3.1.5>)",
           "- trademarks, [FRONT\\_1\\.1](<#FRONT_1.1>)",
       }) {
    require(generated_index.find(expected) != std::string::npos,
            "generated index lost a linked hierarchy entry");
  }
  require(generated_index.find("- LNM OS/2 agent application\n"
                               "  - problems\n"
                               "    - adapter problems") != std::string::npos,
          "generated index lost targetless parent hierarchy");
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
  // 2.4.1/2.4.4 are `SRTBL` problem-determination forms whose answer cells
  // are ruled off with horizontal box words. No column model is proven for
  // them, so the typed route reproduces the region verbatim -- which is
  // exactly what hosted BookServer serves inside `<pre>`
  // (DT=19941010174546, fetched 2026-08-29). The legacy renderer used to
  // invent a `Field | Value` header and expand `&ballot.` to `[ ]`; hosted
  // does neither.
  const auto customer_form = problem_determination.topic_markdown("2.4.1");
  for (const auto* expected : {
           "   | Customer number                           |"
           "                            |",
           "   |                                           | "
           "__________________________ |",
           "   |___________________________________________|"
           "____________________________|",
           "   | LNM for AIX component ID                  |"
           "                            |",
           "   | Problem symptoms                          |"
           "                            |",
       }) {
    require(customer_form.find(expected) != std::string::npos,
            "fixed customer form lost a hosted display line");
  }
  require(customer_form.find(", ,") == std::string::npos,
          "fixed customer form collapsed back into punctuation");
  // 2.4.5 / 2.4.7 render through the typed prose family: each `__`
  // checklist row is its own paragraph with the ballot glyph kept as hosted
  // BookServer prints it (escaped for Markdown).
  const auto customer_checklist =
      problem_determination.topic_markdown("2.4.5");
  for (const auto* expected : {"\\_\\_ Customer number",
                               "\\_\\_ LNM for AIX component ID",
                               "\\_\\_ Describe the symptoms of the problem"}) {
    require(customer_checklist.find(expected) != std::string::npos,
            "fixed customer checklist lost an item");
  }
  require(customer_checklist.find("```text") == std::string::npos,
          "fixed customer checklist was emitted as a code block");
  const auto netview_form = problem_determination.topic_markdown("2.4.4");
  for (const auto* expected : {
           // The reader itself leaves `&ballot.` unresolved in this book.
           "   | Which mode was AIX NetView/6000 operating | &ballot.  Read"
           "             |",
           "   | in at the time of the problem?            | &ballot.  "
           "Read-Write       |",
           "   | \xc2\xb0   Number of objects in the OVw database |"
           "                            |",
           "   |     (use the command ovobjprint | head)   | "
           "__________________________ |",
           "   | \xc2\xb0   Number of objects to hold in ovwdb    | "
           "__________________________ |",
           "   |     cache                                 |"
           "                            |",
           "   | \xc2\xb0   Number of seconds between storing     |"
           "                            |",
           "   |     data to the GTMD database             |"
           "                            |",
       }) {
    require(netview_form.find(expected) != std::string::npos,
            "source-owned fixed form display line was split or moved");
  }
  require(netview_form.find("| a |") == std::string::npos &&
              netview_form.find("| address |") == std::string::npos &&
              netview_form.find("????????") == std::string::npos &&
              netview_form.find("______________________") != std::string::npos,
          "fixed form leaked out-of-grid text or lost visible fill rules");
  const auto reader_questionnaire =
      problem_determination.topic_markdown("COMMENTS");
  require(reader_questionnaire.find("the information in this book?") !=
              std::string::npos,
          "fixed questionnaire lost literal question punctuation");
  const auto network_checklist =
      problem_determination.topic_markdown("2.4.8");
  // Typed form, verified against hosted DT 19941010174546: the `__` ballot
  // rows are questions in their own right (the legacy route turned them into
  // list items and dropped their `?`), and the bullet rows below them keep
  // the bold command run hosted serves as `<B>ovobjprint</B> <B>|</B>
  // <B>head</B>`.
  for (const auto* expected : {
           "\\_\\_ Which AIX NetView/6000 applications were running at the "
           "time of the problem?",
           "\\_\\_ What is the size of the network you are managing?",
           "- Number of stations", "- Number of bridges",
           "- Number of concentrators",
           "- Number of objects in the OVw database \\(use the command "
           "**ovobjprint \\| head**\\)",
           "- Number of objects to hold in ovwdb cache",
           "- Number of seconds between storing data"}) {
    require(network_checklist.find(expected) != std::string::npos,
            "complex fixed questionnaire lost an item or continuation");
  }
  // 2.4.7: the third item's CFONT continuation row (`/usr/OV`, LR78
  // segment 9) and the fourth ballot row (LR78 segment 10) are folded into
  // the checklist from typed row evidence; the CFONT spans land on `/usr/OV`
  // and `/tmp` as the hosted reader draws them.
  const auto hardware_checklist =
      problem_determination.topic_markdown("2.4.7");
  for (const auto* expected : {
           "\\_\\_ Amount of memory \\(RAM\\) installed",
           "\\_\\_ Amount of paging space available",
           "\\_\\_ Amount of free space available in the file system that "
           "contains `/usr/OV`\n\n",
           "\\_\\_ Amount of free space available in `/tmp`"}) {
    require(hardware_checklist.find(expected) != std::string::npos,
            "hardware checklist lost an item or its CFONT continuation");
  }
  require(hardware_checklist.find("`free`") == std::string::npos &&
              hardware_checklist.find("\n`/usr/OV`") == std::string::npos,
          "hardware checklist leaked its ballot glyph or misplaced a span");
  // 2.4.6: the source trademark marker `Motif**` (FRONT_1.1 documents the
  // `**` convention) is literal text, escaped so Markdown cannot read it as
  // emphasis.
  const auto software_checklist =
      problem_determination.topic_markdown("2.4.6");
  require(software_checklist.find("\\_\\_ Motif\\*\\* and X11") !=
              std::string::npos,
          "software checklist trademark marker was not escaped");
  const auto additional_information =
      problem_determination.topic_markdown("2.4.9");
  const auto support_intro = std::string(
      "Have the following information available when you call the IBM "
      "Technical Support center:");
  const auto support_intro_begin = additional_information.find(support_intro);
  require(support_intro_begin != std::string::npos &&
              additional_information.find(
                  support_intro,
                  support_intro_begin + support_intro.size()) ==
                  std::string::npos &&
              additional_information.find("\\_\\_ lnmstatus") !=
                  std::string::npos,
          "additional-information form lost its prose prefix or item");
  const auto frame_relay = markdown_visible_text(problem_determination.topic_markdown("4.3.5"));
  require(frame_relay.find("This section lists the Frame Relay traps") !=
              std::string::npos &&
              frame_relay.find("- **1** — **Description:** DLCI state change") !=
                  std::string::npos,
          "message definition lost its introduction or label association");
  const auto token_ring_traps =
      markdown_visible_text(problem_determination.topic_markdown("4.2.2"));
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

  const auto generic_traps = markdown_visible_text(problem_determination.topic_markdown("4.1.1"));
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

  const auto agent_traps = markdown_visible_text(problem_determination.topic_markdown("4.1.3"));
  const auto agent_first_trap = agent_traps.find("<a id=\"MSG 001\"></a>");
  require(agent_first_trap != std::string::npos,
          "LNM OS/2 agent trap catalog lost its first entry anchor");
  const auto agent_intro = agent_traps.substr(0, agent_first_trap);
  for (const auto* paragraph : {
           "\n**Note:** These traps correspond to the DFIPD messages that are "
           "generated by the LNM OS/2 agent program and have the same number. "
           "You can look up a trap in the Messages chapter",
           "\nWhen the LNM for AIX Response indicates that the status will be "
           "set to a particular setting",
           "may be overriden by the state of another resource.\n",
           "\nThe traps in this section are defined under the "
           "1.3.6.1.4.1.2.6.20 enterprise ID, which is associated with LNM "
           "OS/2 agent.\n",
           "\nThe redirected traps are defined under the "
           "1.3.6.1.4.1.2.6.21.1.1 enterprise ID, which is associated with "
           "LNM for AIX.\n",
       }) {
    require(agent_intro.find(paragraph) != std::string::npos,
            "LNM OS/2 agent trap introduction lost a restored paragraph");
  }
  require(agent_traps.find("resource.as") == std::string::npos &&
              agent_traps.find("agent.bridge") == std::string::npos &&
              agent_traps.find("actions. When") == std::string::npos,
          "LNM OS/2 agent trap introduction leaked row markers or merged "
          "paragraphs");
  require(agent_traps.find(
              "The status of the segment is not changed to normal because, "
              "although the backup path has been recovered") !=
                  std::string::npos &&
              agent_traps.find(
                  "Issue run commands to get status of module and port. "
                  "Update status") != std::string::npos,
          "LNM OS/2 agent trap responses split at record continuations");

  const auto bridge_traps = markdown_visible_text(problem_determination.topic_markdown("4.3.2"));
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
      markdown_visible_text(problem_determination.topic_markdown("4.3.4"));
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

  const auto fddi_traps = markdown_visible_text(problem_determination.topic_markdown("4.4"));
  const auto fddi_intro =
      std::string("For more information about the data associated with each "
                  "of these traps");
  const auto fddi_intro_begin = fddi_traps.find(fddi_intro);
  require(fddi_intro_begin != std::string::npos &&
              fddi_traps.find(fddi_intro,
                              fddi_intro_begin + fddi_intro.size()) ==
                  std::string::npos &&
              // The anchor is the whole SRMSG operand, exactly as hosted
              // serves it (`<a name="MSG 1 (fddiRPUNoResponse)">`,
              // DT 19941010174546), not just its first word.
              fddi_traps.find("<a id=\"MSG 1 (fddiRPUNoResponse)\"></a>",
                              fddi_intro_begin) != std::string::npos,
          "FDDI catalog introduction lost single ownership or first entry");
  require(fddi_traps.find(
              "ringInoperative cleared in segment. All stations on the ring "
              "are reset to their previous status") != std::string::npos &&
              fddi_traps.find(
                  "The status of the resource is set to normal. Forward to "
                  "AIX NetView/6000 with resource name.") !=
                  std::string::npos,
          "FDDI response continuation rows lost their text or ownership");
  require(fddi_traps.find("segment. / All") == std::string::npos &&
              fddi_traps.find("domain.- The status") == std::string::npos,
          "FDDI response continuation leaked physical-row markers");
  require(fddi_traps.find(
              "Forward to AIX NetView/6000 with resource name.") !=
                  std::string::npos &&
              fddi_traps.find("\nwith resource name.") == std::string::npos &&
              fddi_traps.find(
                  "has been received from an Remote Program Upload (RPU) "
                  "Loader.") != std::string::npos,
          "FDDI soft-wrapped rows were split into separate paragraphs");

  const auto agent_problems =
      problem_determination.topic_markdown("2.3.1");
  require(agent_problems.find("correspond to DFI message numbers") !=
              std::string::npos &&
              agent_problems.find("DFI > message") == std::string::npos,
          "selector projection retained its fixed-row marker field");

  const auto filesystem_space =
      problem_determination.topic_markdown("2.4.3");
  require(filesystem_space.find(
              "   | Amount of free space available in the     |"
              "                            |\n"
              "   | file system that contains /usr/OV         | "
              "__________________________ |") != std::string::npos,
          "active-table CFONT continuation lost the /usr/OV row");
  require(filesystem_space.find(
              "Amount of free space available in /tmp") !=
              std::string::npos &&
              filesystem_space.find(
                  "contains /usr/OV<br>Amount of free space available in "
                  "/tmp") == std::string::npos,
          "active-table CFONT continuation merged the separate /tmp row");

  const auto symbolic_traps =
      markdown_visible_text(problem_determination.topic_markdown("4.1.2"));
  require(symbolic_traps.find("<pre>") == std::string::npos &&
              symbolic_traps.find("suggested actions.\n\nThe traps") !=
                  std::string::npos &&
              symbolic_traps.find(
                  "<a id=\"MSG bridgeHistoryDataComplete\"></a>") !=
                  std::string::npos &&
              symbolic_traps.find("logged by LNM for AIX") !=
                  std::string::npos &&
              symbolic_traps.find("logged by action LNM") ==
                  std::string::npos &&
              symbolic_traps.find("LNMOS2AgentNotResponding") !=
                  std::string::npos,
          "symbolic SRMSG catalog remained fixed or leaked its intro marker");
  const auto rmonitor_publications =
      markdown_visible_text(problem_determination.topic_markdown("BACK_1.5"));
  require(rmonitor_publications.find(
              "Using RMONitor for AIX (SC31-7115)") != std::string::npos &&
              rmonitor_publications.find(
                  "Using RMONitor Agent for OS/2 (SC31-7116)") !=
                  std::string::npos,
          "RMONitor publication CFONT rows disappeared during title repair");
  const auto sna_publications =
      markdown_visible_text(problem_determination.topic_markdown("BACK_1.10"));
  for (const auto* expected : {
           "AIX SNA Server/6000 User's Guide (SC31-7002)",
           "AIX SNA Server/6000 Configuration Reference (SC31-7014)",
           "AIX SNA Server/6000 Transaction Program Reference (SC31-7003)"}) {
    require(sna_publications.find(expected) != std::string::npos,
            "SNA publication CFONT row disappeared during title repair");
  }
  const auto lnm_publications =
      markdown_visible_text(problem_determination.topic_markdown("BACK_1.1"));
  for (const auto* expected : {
           "Getting Started with LAN Network Manager for AIX (SC31-7109)",
           "Using LAN Network Manager for AIX (SC31-7110)",
           "LAN Network Manager for AIX Reference (SC31-7111)"}) {
    const auto first = lnm_publications.find(expected);
    require(first != std::string::npos &&
                lnm_publications.find(expected, first + 1) ==
                    std::string::npos,
            "LAN publication CFONT row received dual ST ownership");
  }
  require(lnm_publications.find("(SC31-7109))") == std::string::npos &&
              lnm_publications.find(
                  "Getting Started with LAN Network Manager for AIX "
                  "(SC31-7109)\n\nThis book is intended") !=
                  std::string::npos,
          "LAN publication identifier punctuation or title ownership failed");
  const auto lnm_reference_tail = std::string(
      "management problems and provides a list of messages and traps sent "
      "by LNM for AIX. It also describes the files that are used for segment "
      "performance data, daemons, processes, and executables.");
  const auto lnm_reference_tail_begin =
      lnm_publications.find(lnm_reference_tail);
  require(lnm_reference_tail_begin != std::string::npos &&
              lnm_publications.find(
                  lnm_reference_tail,
                  lnm_reference_tail_begin + lnm_reference_tail.size()) ==
                  std::string::npos,
          "LAN publication lost its cross-record description continuation");
  const auto netcenter_publications =
      markdown_visible_text(problem_determination.topic_markdown("BACK_1.11"));
  for (const auto* expected : {
           "NETCENTER Operator Tutorial (GC75-0109)",
           "NETCENTER Graphic Network Monitor Service Point Interface "
           "Installation (SC75-0111)"}) {
    const auto first = netcenter_publications.find(expected);
    require(first != std::string::npos &&
                netcenter_publications.find(expected, first + 1) ==
                    std::string::npos,
            "NETCENTER publication CFONT row received dual ST ownership");
  }
  const auto comments_to_ibm =
      problem_determination.topic_markdown("BACK_2");
  for (const auto* expected : {
           "If you prefer to send comments by mail",
           "If you prefer to send comments by FAX"}) {
    const auto first = comments_to_ibm.find(expected);
    require(first != std::string::npos &&
                comments_to_ibm.find(expected, first + 1) ==
                    std::string::npos,
            "fixed comment instructions received dual ownership");
  }
  require(comments_to_ibm.find("you.adapter") == std::string::npos,
          "fixed comment row leaked its alphabetic marker field");
  require(netcenter_publications.find("(GC75-0109)=") == std::string::npos,
          "NETCENTER publication leaked a terminal fixed-row marker");
  const auto fddi_publications =
      markdown_visible_text(problem_determination.topic_markdown("BACK_1.7"));
  require(fddi_publications.find(
              "FDDI SNMP Proxy Agent User's Guide (GC17-0383)") !=
              std::string::npos &&
              fddi_publications.find("(GC17-0383)bridge") ==
                  std::string::npos &&
              fddi_publications.find("American National Standards Institute") !=
                  std::string::npos,
          "FDDI publication row lost content or leaked its marker field");
  const auto motif_publications =
      markdown_visible_text(
          problem_determination.topic_markdown("BACK_1.12.2"));
  require(motif_publications.find("publications:agent") == std::string::npos &&
              motif_publications.find("OSF/Motif Series (5 volumes)") !=
                  std::string::npos &&
              motif_publications.find("Programmer's Guide (ISBN 0-13-640509-6)") !=
                  std::string::npos,
          "OSF/Motif publication rows lost content or leaked marker fields");

  const auto performance_files =
      problem_determination.topic_markdown("1.4");
  require(performance_files.find(
              "`/usr/lpp/lnm/reports/lnmlnmemon/dir_name` directory") !=
              std::string::npos &&
              performance_files.find("c.cp") == std::string::npos,
          "performance-file prose lost its pending-font continuation");
}
