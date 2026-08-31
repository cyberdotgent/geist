#include "geist/detail/lowering/topic_identity.hpp"

namespace geist::detail {

TopicIdentityIR make_topic_identity(const TocEntry &entry) {
  TopicIdentityIR identity;
  identity.id = entry.id;
  identity.title = entry.title;
  identity.heading_level = entry.heading_level;
  identity.topic_number = entry.topic_number;
  identity.start_logical_record = entry.start_logical_record;
  identity.end_logical_record = entry.end_logical_record;
  return identity;
}

bool topic_identity_has_body(const TopicIdentityIR &identity) {
  return identity.start_logical_record != 0 &&
         identity.end_logical_record > identity.start_logical_record;
}

} // namespace geist::detail
