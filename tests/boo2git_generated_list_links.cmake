# Copyright 2026 Yvan Janssens
# SPDX-License-Identifier: Apache-2.0

if(NOT DEFINED BOO2GIT OR NOT DEFINED BOOK OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "BOO2GIT, BOOK, and OUTPUT are required")
endif()

file(MAKE_DIRECTORY "${OUTPUT}")
execute_process(
  COMMAND "${BOO2GIT}" -f "${BOOK}" "${OUTPUT}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE stdout
  ERROR_VARIABLE stderr
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "boo2git failed (${result}): ${stdout}${stderr}")
endif()

file(READ "${OUTPUT}/figures.md" figures)
string(LENGTH "${figures}" figures_length)
string(REPLACE "](" "" figures_without_links "${figures}")
string(LENGTH "${figures_without_links}" figures_without_links_length)
math(EXPR link_count
  "(${figures_length} - ${figures_without_links_length}) / 2")
# A generated-list entry whose target is a book resource resolves to the
# exported PNG, not to the raw object id.
string(FIND "${figures}"
  "[1\\.  VHF/UHF LMR audio frequency range   1\\.3](1.png)"
  first_resolved)
string(FIND "${figures}"
  "[9\\.  LoRa Frame Format   7\\.1\\.3](9.png)"
  last_resolved)
string(FIND "${figures}" "(<#FIGFIGUNIQ5>)" raw_target)
string(FIND "${figures}" "[| 1" raw_decoration)
# 9 generated entries plus three navigation links at both the top and bottom.
if(NOT link_count EQUAL 15 OR first_resolved EQUAL -1 OR
   last_resolved EQUAL -1 OR NOT raw_target EQUAL -1 OR
   NOT raw_decoration EQUAL -1)
  message(FATAL_ERROR
    "typed generated-list labels or exporter target resolution regressed: "
    "links=${link_count} first=${first_resolved} last=${last_resolved} "
    "raw=${raw_target} decoration=${raw_decoration}")
endif()

# A generated-list entry whose target is an in-book anchor resolves to the
# owning topic's file plus a fragment.
file(READ "${OUTPUT}/tables.md" tables)
string(FIND "${tables}"
  "[1\\.  IPv4 Address Classes   2\\.4\\.4](2-4-4.md#TBLTBLUNIQ17)"
  table_resolved)
string(FIND "${tables}" "(<#TBLTBLUNIQ17>)" table_raw)
if(table_resolved EQUAL -1 OR NOT table_raw EQUAL -1)
  message(FATAL_ERROR
    "generated-list anchor targets were not rewritten to file+fragment: "
    "resolved=${table_resolved} raw=${table_raw}")
endif()
