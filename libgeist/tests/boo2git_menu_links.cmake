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

# Hosted packet 1.0 (DT=20260614112503): `Subtopics:` followed by the three
# child topics. The exporter rewrites each `<#id>` target to the child's file
# and must not leave an id-prefixed target behind, nor repeat the topic title
# as a second heading.
file(READ "${OUTPUT}/1-0.md" typed_menu)
string(FIND "${typed_menu}"
  "Subtopics:\n\n- [1\\.1 Original Packet Radio](1-1.md)"
  typed_resolved)
string(FIND "${typed_menu}" "](<" typed_raw)
string(FIND "${typed_menu}" "# An Introduction to Packet Radio" duplicate_heading)
if(typed_resolved EQUAL -1 OR NOT typed_raw EQUAL -1 OR
   NOT duplicate_heading EQUAL -1)
  message(FATAL_ERROR
    "canonical typed menu Subtopics lead, id-prefixed target, or "
    "anchor-leading heading was not exported: "
    "resolved=${typed_resolved} raw=${typed_raw} "
    "duplicate=${duplicate_heading}")
endif()

# A deeper menu whose children are themselves nested topics.
file(READ "${OUTPUT}/6-3.md" nested_menu)
string(FIND "${nested_menu}"
  "- [6\\.3\\.1 Interfaces](6-3-1.md)" nested_resolved)
string(FIND "${nested_menu}" "](<" nested_raw)
if(nested_resolved EQUAL -1 OR NOT nested_raw EQUAL -1)
  message(FATAL_ERROR
    "nested menu target rewriting regressed: "
    "resolved=${nested_resolved} raw=${nested_raw}")
endif()

# A body cross reference to another topic's `SR<id>` anchor resolves to that
# topic's file plus the fragment, not to a bare in-page anchor.
file(READ "${OUTPUT}/6-2.md" cross_reference)
string(FIND "${cross_reference}"
  "[\"Web Locations of Packet](a-0.md) [Radio Software\" in topic A\\.0](a-0.md)"
  cross_resolved)
string(FIND "${cross_reference}" "](<#HDRURLS>)" cross_raw)
if(cross_resolved EQUAL -1 OR NOT cross_raw EQUAL -1)
  message(FATAL_ERROR
    "body cross-reference target rewriting regressed: "
    "resolved=${cross_resolved} raw=${cross_raw}")
endif()
