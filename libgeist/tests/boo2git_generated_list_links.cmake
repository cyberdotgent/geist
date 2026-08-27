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
string(REPLACE "]("
  "" figures_without_links "${figures}")
string(LENGTH "${figures_without_links}" figures_without_links_length)
math(EXPR link_count
  "(${figures_length} - ${figures_without_links_length}) / 2")
string(FIND "${figures}"
  "[1\\.  Five Styles of Client/Server Computing   1\\.1\\.3](1.png)"
  first_resolved)
string(FIND "${figures}"
  "[11\\.  Open Blueprint Model   2\\.3\\.3](11.png)"
  embedded_marker_resolved)
string(FIND "${figures}" "(<#FIGFDSS101>)" raw_target)
string(FIND "${figures}" "[| 11" raw_decoration)
# 81 generated entries plus three navigation links at both the top and bottom.
if(NOT link_count EQUAL 87 OR first_resolved EQUAL -1 OR
   embedded_marker_resolved EQUAL -1 OR NOT raw_target EQUAL -1 OR
   NOT raw_decoration EQUAL -1)
  message(FATAL_ERROR
    "typed generated-list labels or exporter target resolution regressed: "
    "links=${link_count} first=${first_resolved} "
    "marker=${embedded_marker_resolved} raw=${raw_target} "
    "decoration=${raw_decoration}")
endif()
