#include "geist/document.hpp"
#include "test_failures.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

void require_contains(const std::string& haystack,
                      const std::string& needle,
                      const char* label) {
  if (haystack.find(needle) != std::string::npos) {
    return;
  }
  std::cerr << "missing " << label << ": " << needle << "\n";
  geist_test::record_failure();
  return;
}

void require_not_contains(const std::string& haystack,
                          const std::string& needle,
                          const char* label) {
  if (haystack.find(needle) == std::string::npos) {
    return;
  }
  std::cerr << "unexpected " << label << ": " << needle << "\n";
  geist_test::record_failure();
  return;
}

std::string topic_markdown(const geist::BooDocument& document,
                           const std::string& id) {
  return document.topic_markdown(id);
}

} // namespace

int main() {
  const auto book =
      std::filesystem::path(GEIST_FIXTURE_DIR) / "packet.boo";
  const auto document = geist::BooDocument::open(book);

  const auto title_page = topic_markdown(document, "TITLE");
  // `cz OFF TIPAGE` .. `cz OFF ETIPAGE` is the generated title-page
  // projection, not stored prose: the compiler laid the source prolog's
  // fields out as display rows and hosted BookServer re-flows them.  Hosted
  // (DT 20260614112503) serves
  //   `<B>Amateur</B> <B>Packet</B> <B>Radio</B>`
  //   `<B>A</B> <B>Complete</B> <B>Tutorial</B>`
  //   `<B>Evie</B> <B>Cooper</B>`
  //   `<p>` `Document Number 9963-0413-56`
  //   `<p>` `January 15, 2026`
  //   `<p>` `Evie Cooper`
  // -- the three title-block rows in one paragraph because the source stores
  // no blank row between them, and one paragraph per metadata field because
  // it stores a blank row before each.  The rows' own columns (57, 58, 66,
  // 49, 61, 66) are layout origin and do not survive.
  require_contains(title_page,
                   "**Amateur Packet Radio**  \n"
                   "**A Complete Tutorial**  \n"
                   "**Evie Cooper**\n"
                   "\n"
                   "Document Number 9963\\-0413\\-56\n"
                   "\n"
                   "January 15, 2026\n"
                   "\n"
                   "Evie Cooper",
                   "title page projection");
  require_not_contains(title_page,
                       "                              ",
                       "title page served at its stored columns");
  require_not_contains(title_page,
                       "Evie Cooper D**ocumen**",
                       "title page torn Document Number emphasis");
  require_not_contains(title_page,
                       "**t Number 9963-0413-56",
                       "title page metadata folded into title block");

  const auto cover = topic_markdown(document, "COVER");
  // The same projection under `cz OFF COVER`, and the counter-example that
  // proves the emphasis is read from the `CFONT` operands rather than from
  // which field a row holds: hosted serves `<B>Amateur</B> <B>Packet</B>
  // <B>Radio</B>` and `<B>A</B> <B>Complete</B> <B>Tutorial</B>` but plain
  // `Evie Cooper`, because the cover carries no triple on that row -- while
  // `TITLE` carries `cfont 66 4 2 71 6 2` on the same two words and bolds
  // them.  Every field is its own paragraph here; the source stores a blank
  // row before each.  The `U+2500` frame rows hosted draws as `<hr>` write no
  // character.
  require_contains(cover,
                   "**Amateur Packet Radio**\n"
                   "\n"
                   "**A Complete Tutorial**\n"
                   "\n"
                   "Evie Cooper\n"
                   "\n"
                   "Document Number 9963\\-0413\\-56\n"
                   "\n"
                   "Part Number 0413\\-56\n"
                   "\n"
                   "File Number PACKET",
                   "cover projection");
  require_not_contains(cover, "____", "cover frame rule drawn as text");
  require_not_contains(cover, "**Evie Cooper**",
                       "cover author emphasised without a CFONT triple");

  const auto base_stack = topic_markdown(document, "3.2");
  // Typed `CZ OFF XMP` example block.  Hosted 3.2 (DT 20260614112503)
  // opens the block with `<samp>#</samp> <samp>name</samp> ...`; the typed
  // renderer writes a plain fence.  The region keeps its own left margin:
  // hosted serves these rows at display column 5.
  require_contains(base_stack,
                   "```\n"
                   "     # name callsign speed paclen window description\n"
                   "     #----- -------- ----- ------ ------ -----------\n"
                   "     radio  WA4XYZ-1 1200  256    7      Real TNC\n"
                   "```",
                   "AX.25 axports example block");
  require_not_contains(base_stack,
                       "# ``n`ame",
                       "example line rendered as Markdown heading");
  // Hosted `<li> The  <I>interface</I>  <I>name</I>, ... device by<a
  // href="...#FTNFTNUNIQ21"> (12)</a>`: one HP1 phrase over both words.
  require_contains(base_stack,
                   "- The *interface name*, the name that the Linux kernel "
                   "knows the network device by "
                   "[\\(12\\)](<#FTNFTNUNIQ21>)",
                   "list item with nested footnote link");
  // Hosted keeps this one `<p>` across two display rows and marks the path
  // with `<kbd>`; the `SI` term between the rows is not a paragraph break.
  require_contains(base_stack,
                   "\n\nTo define an AX\\.25 port, edit "
                   "`/etc/ax25/axports`, and, use tabs for everything, not "
                   "spaces:\n\n",
                   "paragraph after first list is not merged into list item");
  require_not_contains(base_stack,
                       "Linux AX.25, Configuring Ports, AX.25",
                       "hidden subject-index term in body text");
  require_not_contains(base_stack,
                       "= To define an AX.25 port",
                       "decoded line marker in paragraph body");
  require_not_contains(base_stack,
                       "[The *interface name*, the name that the Linux "
                       "kernel knows the network device by",
                       "whole list item wrapped by nested footnote target");
  // Hosted serves this region at display column 5 too.
  require_contains(base_stack,
                   "```\n"
                   "     # ax25_name min_obs def_qual worst_qual verbose\n"
                   "     #---------- ------- -------- ---------- -------\n"
                   "     radio       5       192      100        0\n"
                   "```",
                   "NET/ROM broadcast example block");

  const auto generated_index = topic_markdown(document, "INDEX");
  require_contains(generated_index, "## A", "index group A");
  require_contains(generated_index,
                   "- AX\\.25 Protocol, [2\\.1](<#2.1>)",
                   "index AX.25 entry");
  require_contains(generated_index,
                   "- ROSE, [2\\.3](<#2.3>)",
                   "index final generated entry");
  require_contains(generated_index,
                   "  - Digipeater, [2\\.1\\.3](<#2.1.3>)",
                   "nested index entry");
  require_contains(generated_index,
                   "  - Advanced topics\n"
                   "    - IPIP tunnels, [4\\.5\\.1](<#4.5.1>)",
                   "targetless index parent hierarchy");
  require_not_contains(generated_index,
                       "have callsign",
                       "garbage after cendindex");
  require_not_contains(generated_index,
                       "cbacklevel",
                       "decoded garbage control text after cendindex");

  // `cz OFF FIG` .. `cz OFF EFIG` now delimits the figure region for the
  // typed route, so 1.3 lowers through the figure block: hosted (DT
  // 20260614112503) serves `<a name="FIGFIGUNIQ5">` with `<img ... alt=
  // "PICTURE 1">` and the caption `Figure 1. VHF/UHF LMR audio frequency
  // range` below it.  The typed span joiner also keeps a CFONT run that
  // covers consecutive words as one inline, so hosted's per-word
  // `<B><I>audio</B></I> <B><I>interface;</B></I>` becomes one emphasis.
  const auto figures = topic_markdown(document, "1.3");
  require_contains(figures,
                   "through its ***audio interface;*** it also likely "
                   "has the ability to ***key the radio***",
                   "HP3 spans in topic 1.3");
  require_contains(figures,
                   "**you cannot use your radio's VOX control for "
                   "bidirectional packet**",
                   "HP2 span around VOX/control");
  require_not_contains(figures,
                       "****VOX**** cont**rol for**",
                       "old torn VOX/control emphasis");
  // The audio callout is a `cz OFF LBLBOX` verbatim region, reproduced
  // character for character inside a fence the way hosted serves its
  // `<pre width="132"><!-- lblbox -->`.  Hosted bolds words inside that
  // `<pre>`; a preformatted block carries no inlines, so the region keeps
  // the words and drops the emphasis (recorded residual).
  require_contains(figures,
                   "by  tapping the discriminator and directly driving "
                   "the modulator. This",
                   "audio callout text inside the verbatim lblbox region");
  require_contains(figures,
                   "<a id=\"FIGFIGUNIQ5\"></a>",
                   "figure region anchor keeps the whole SRFIG id");
  require_contains(figures,
                   "](<resource:1>)",
                   "figure image resource block");
  require_contains(figures,
                   "Figure 1\\. VHF/UHF LMR audio frequency range",
                   "figure caption text");
  require_not_contains(figures,
                       "PICTURE 1 Figure 1. VHF/UHF LMR ![",
                       "inline image splitting figure caption");
  require_not_contains(figures,
                       "audio fre](resource:1) quency",
                       "word split around inline image");

  const auto original_packet = topic_markdown(document, "1.1");
  // Typed `SRFTN<id>` / `CZ FLOW FN` footnotes: the anchor keeps the whole
  // opcode after `SR` (the `cselect` target), and the compiled body's
  // trailing row terminator `.` is not display text.
  require_contains(original_packet,
                   "same CSMA medium access control technique\\.\n\n"
                   "<a id=\"FTNFTNUNIQ2\"></a>",
                   "footnote one has a single terminal period");
  require_contains(original_packet,
                   "November 22, 1977, the Internet was born\\.",
                   "footnote two has a single terminal period");
  require_not_contains(original_packet,
                       "technique..",
                       "footnote one doubled terminator");
  require_not_contains(original_packet,
                       "Internet was born..",
                       "footnote two doubled terminator");
  require_not_contains(original_packet,
                       "[Back](#fnref-FTNFTNUNIQ1)",
                       "BookServer footnote body has no local back link");

  // `cz OFF TABLE` .. `cz OFF ETABLE` now delimits the `SRTBLTBLUNIQ17`
  // envelope for the typed route, so the table lowers through the
  // fixed-table block.  Hosted (DT 20260614112503) gives class C the range
  // `192.0.0.0 - 223.255.255.255` and class E `240.0.0.0 -
  // 255.255.255.255`; the legacy route dropped both continuation cells.
  const auto address_classes = topic_markdown(document, "2.4.4");
  require_contains(address_classes,
                   "| Class | Range | Default Netmask |",
                   "IPv4 address-class table keeps all three columns");
  require_contains(address_classes,
                   "| C | 192\\.0\\.0\\.0 \\-<br>223\\.255\\.255\\.255 | "
                   "255\\.255\\.255\\.0 |",
                   "IPv4 class C range and netmask cells");
  require_contains(address_classes,
                   "| D | 224\\.0\\.0\\.0 \\-<br>239\\.255\\.255\\.255 | "
                   "none, used for<br>multicast |",
                   "IPv4 class D continuation cell ownership");
  require_contains(address_classes,
                   "| E | 240\\.0\\.0\\.0 \\-<br>255\\.255\\.255\\.255 | "
                   "none, experimental |",
                   "IPv4 class E range cell");
}
