#include "geist/detail/typed_route_inventory.hpp"

#include "geist/detail/book_topic_catalog_ir.hpp"
#include "geist/detail/internal.hpp"
#include "geist/detail/topic_document_lowering.hpp"
#include "geist/document.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace geist::detail {
namespace {

bool bullet_word(const std::string &word) {
  return word == "o" || word == "-" || word == "*" || word == "\xc2\xb0" ||
         word == "\xe2\x80\xa2";
}

bool ordinal_word(const std::string &word) {
  if (word.size() < 2 || word.size() > 4)
    return false;
  const auto terminator = word.back();
  if (terminator != '.' && terminator != ')')
    return false;
  const auto body = word.substr(0, word.size() - 1);
  if (body.size() == 1 && std::isalpha(static_cast<unsigned char>(body[0])))
    return true;
  return std::all_of(body.begin(), body.end(), [](const unsigned char ch) {
    return std::isdigit(ch) != 0;
  });
}

// A physical row opens a list item when its first visible word is a bullet or
// an ordinal marker followed by further text on the same row.
bool list_item_row(const std::string &visible_text) {
  std::istringstream words(visible_text);
  std::string first;
  std::string second;
  if (!(words >> first) || !(words >> second))
    return false;
  return bullet_word(first) || ordinal_word(first);
}

std::string segment_text(const DecodedLogicalRecordSource &record,
                         const OutputRangeIR &range) {
  const auto text = token_words_to_ascii(record.assembled.words);
  if (range.begin >= text.size() || range.end <= range.begin)
    return {};
  return text.substr(range.begin, std::min(range.end, text.size()) -
                                      range.begin);
}

bool image_selector(const DecodedLogicalRecordSource &record,
                    const ControlSegmentIR &segment,
                    const std::set<std::string> &resource_ids) {
  std::istringstream operands(segment_text(record, segment.operand_range));
  std::string column;
  std::string length;
  std::string target;
  if (operands >> column >> length >> target &&
      resource_ids.count(ascii_lower(target)) != 0)
    return true;
  const auto payload = trim_ascii(segment_text(record, segment.payload_range));
  return ascii_lower(payload.substr(0, 7)) == "<image>";
}

} // namespace

TopicStructureIR extract_topic_structure_ir(
    const std::vector<DecodedLogicalRecordSource> &sources,
    const LayoutIR &layout, const std::set<std::string> &resource_ids) {
  TopicStructureIR structure;
  for (const auto &record : sources) {
    for (const auto &segment : record.control_segments) {
      if (segment.malformed)
        ++structure.malformed_controls;
      switch (segment.kind) {
      case BookControlKind::heading_level:
        if (structure.heading_kind.empty())
          structure.heading_kind = ascii_lower(
              trim_ascii(segment_text(record, segment.operand_range)));
        break;
      case BookControlKind::select:
        if (image_selector(record, segment, resource_ids))
          ++structure.image_selectors;
        else
          ++structure.selectors;
        break;
      case BookControlKind::font:
        ++structure.font_controls;
        break;
      case BookControlKind::table_start:
        ++structure.table_controls;
        break;
      case BookControlKind::menu_start:
      case BookControlKind::menu_item:
      case BookControlKind::menu_end:
        ++structure.menu_controls;
        break;
      case BookControlKind::message_start:
        ++structure.message_controls;
        break;
      case BookControlKind::structural:
        if (ascii_lower(segment.opcode.substr(0, 5)) == "srfig")
          ++structure.figure_controls;
        else
          ++structure.other_structural_controls;
        break;
      default:
        break;
      }
    }
  }
  for (const auto &run : layout.runs) {
    for (const auto &row : run.rows) {
      ++structure.rows;
      if (row.start == PhysicalRowStartKind::placeholder_wrap)
        ++structure.placeholder_rows;
      if (list_item_row(row.visible_text))
        ++structure.list_rows;
    }
  }
  return structure;
}

std::string topic_structure_signature(const TopicStructureIR &structure) {
  std::vector<std::string> features;
  if (structure.list_rows > 0)
    features.push_back("lists");
  // '?' placeholder rows are not table evidence on their own: the same
  // placeholder marks soft display-line wraps inside ordinary prose.
  if (structure.table_controls > 0)
    features.push_back("tables");
  if (structure.selectors > 0)
    features.push_back("selectors");
  if (structure.figure_controls > 0 || structure.image_selectors > 0)
    features.push_back("figures");
  if (structure.menu_controls > 0)
    features.push_back("menu");
  if (structure.message_controls > 0)
    features.push_back("messages");
  if (features.empty())
    return "prose";
  std::string signature;
  for (const auto &feature : features)
    signature += (signature.empty() ? "" : "+") + feature;
  return signature;
}

// Precedence: generated reader topics, then front matter, then the body
// feature set. A body with exactly one feature takes that feature's class;
// two or more are "mixed".
std::string classify_topic_structure(const std::string &topic_id,
                                     const TopicStructureIR &structure) {
  const auto id = ascii_lower(topic_id);
  const auto &kind = structure.heading_kind;
  if (kind == ":toc" || kind == ":figlist" || kind == ":tlist" ||
      id == "contents" || id == "index" || id == "figures" || id == "tables")
    return "generated (TOC/INDEX/FIGURES/TABLES)";
  if (kind == ":cover" || kind == ":title" || kind == ":tipage" ||
      kind == ":vnotice" || kind == ":notices" || kind == ":edition" ||
      id == "cover" || id == "edition" || id == "notices")
    return "title/edition front matter";
  if (structure.rows == 0)
    return "title only (no body rows)";
  const auto signature = topic_structure_signature(structure);
  if (signature == "prose")
    return "heading + prose paragraphs only";
  if (signature == "lists")
    return "prose + lists";
  if (signature == "tables")
    return "contains fixed rows/tables";
  if (signature == "selectors")
    return "contains selectors/cross-references";
  if (signature == "figures")
    return "contains figures/images";
  if (signature == "menu")
    return "menu";
  if (signature == "messages")
    return "messages";
  return "mixed";
}

std::string normalize_rejection_reason(std::string reason) {
  std::string output;
  output.reserve(reason.size());
  for (std::size_t index = 0; index < reason.size();) {
    const auto ch = static_cast<unsigned char>(reason[index]);
    if (ch == '0' && index + 1 < reason.size() &&
        (reason[index + 1] == 'x' || reason[index + 1] == 'X')) {
      index += 2;
      while (index < reason.size() &&
             std::isxdigit(static_cast<unsigned char>(reason[index])) != 0)
        ++index;
      output += "0x#";
    } else if (std::isdigit(ch) != 0) {
      // A dotted topic number such as 2.4.3.3 collapses to one '#'.
      while (index < reason.size() &&
             (std::isdigit(static_cast<unsigned char>(reason[index])) != 0 ||
              (reason[index] == '.' && index + 1 < reason.size() &&
               std::isdigit(static_cast<unsigned char>(reason[index + 1])) !=
                   0)))
        ++index;
      output += '#';
    } else if (ch == '\'' || ch == '"') {
      const auto close = reason.find(static_cast<char>(ch), index + 1);
      if (close == std::string::npos) {
        output += reason.substr(index);
        break;
      }
      output += static_cast<char>(ch);
      output += '*';
      output += static_cast<char>(ch);
      index = close + 1;
    } else {
      output += static_cast<char>(ch);
      ++index;
    }
  }
  return output;
}

std::string typed_route_reason(const TypedRouteTopicIR &topic) {
  if (topic.route == TypedRouteKind::typed)
    return {};
  if (!topic.rejection.empty())
    return topic.rejection;
  std::string reason;
  for (const auto &declined : topic.declined)
    reason += (reason.empty() ? "" : " | ") + declined;
  return reason.empty() ? "no typed family recognized the source" : reason;
}

} // namespace geist::detail

namespace geist {

detail::TypedRouteInventoryIR BooDocument::typed_route_inventory() const {
  using namespace detail;
  TypedRouteInventoryIR inventory;
  std::set<std::string> resource_ids;
  for (const auto &resource : resources_)
    resource_ids.insert(ascii_lower(resource.id));

  for (const auto &entry : toc_) {
    const auto *catalog_entry =
        find_book_topic_catalog_entry(*topic_catalog_ir_, entry.id);
    if (catalog_entry == nullptr || !catalog_entry->topic_header)
      continue;
    const auto &header = *catalog_entry->topic_header;
    const auto &info = topics_.at(header.topic_info_index);

    TypedRouteTopicIR topic;
    topic.id = entry.id;
    topic.title = entry.title;
    topic.level = entry.level;

    TopicIdentityIR identity;
    identity.id = entry.id;
    identity.title = entry.title;
    identity.heading_level = info.heading_level;
    identity.topic_number = info.topic_number;
    identity.start_logical_record = info.start_logical_record;
    identity.end_logical_record = info.end_logical_record;

    const auto sources = decode_logical_record_sources(
        *decode_context_, info.start_logical_record, info.end_logical_record);
    topic.structure = extract_topic_structure_ir(
        sources, extract_layout_ir(sources), resource_ids);

    TypedLoweringTraceIR trace;
    const auto document = try_lower_topic_to_document_ir(
        identity, sources, topic_catalog_ir_.get(), &topic.rejection, &trace);
    topic.family = trace.family;
    topic.declined = std::move(trace.declined);
    topic.route = document ? TypedRouteKind::typed : TypedRouteKind::legacy;
    if (document) {
      ++inventory.typed_count;
      ++inventory.typed_by_family[topic.family];
    } else {
      ++inventory.legacy_count;
    }
    inventory.topics.push_back(std::move(topic));
  }
  return inventory;
}

} // namespace geist
