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

file(READ "${OUTPUT}/glossary.md" glossary)
function(count_occurrences input_variable needle output_variable)
  string(LENGTH "${${input_variable}}" before_length)
  string(REPLACE "${needle}" "" without_matches "${${input_variable}}")
  string(LENGTH "${without_matches}" after_length)
  string(LENGTH "${needle}" needle_length)
  math(EXPR match_count
    "(${before_length} - ${after_length}) / ${needle_length}")
  set(${output_variable} "${match_count}" PARENT_SCOPE)
endfunction()

count_occurrences(glossary "\n# " heading_count)
count_occurrences(glossary "\n## " section_count)
count_occurrences(glossary "<a id=\"GLS " anchor_count)
count_occurrences(glossary "\n- **" list_item_count)
string(FIND "${glossary}"
  "<a id=\"GLS data link connection identifier (DLCI)\"></a>"
  dlci_anchor)
# The embedded fixed-layout object renders verbatim: hosted BookServer serves
# it as fixed columns of plain text inside the topic's <pre> and emits no
# <table> element on the page (DT 19941010174546).
string(FIND "${glossary}" "DLCI Values  Function" table_header)
string(FIND "${glossary}" "1-15         reserved" table_low)
string(FIND "${glossary}"
  "1023         in-channel layer management" table_high)
string(FIND "${glossary}"
  "<a id=\"GLS X.25 interface\"></a>" terminal_anchor)
string(FIND "${glossary}"
  "procedures described in the CCITT Recommendation X\\.25\\."
  terminal_definition)
string(FIND "${glossary}"
  "program can use the AIX NetView Service Point program to communicate with the NetView and NETCENTER programs"
  continuation_definition)
string(FIND "${glossary}" "???????????" padding_run)
string(FIND "${glossary}" "keys on the: keyboard" invented_colon_one)
string(FIND "${glossary}" "speed and: greater" invented_colon_two)
string(FIND "${glossary}" "operating system\\.: The" invented_colon_three)
string(FIND "${glossary}" "Recommendation X\\.25\\.\\." doubled_period)
string(FIND "${glossary}" "<pre>" legacy_pre)
string(FIND "${glossary}" "Subtopics:" legacy_menu)

if(NOT heading_count EQUAL 1 OR NOT section_count EQUAL 21 OR
   NOT anchor_count EQUAL 281 OR NOT list_item_count EQUAL 281 OR
   dlci_anchor EQUAL -1 OR table_header EQUAL -1 OR table_low EQUAL -1 OR
   table_high EQUAL -1 OR terminal_anchor EQUAL -1 OR
   terminal_definition EQUAL -1 OR continuation_definition EQUAL -1 OR
   NOT padding_run EQUAL -1 OR NOT invented_colon_one EQUAL -1 OR
   NOT invented_colon_two EQUAL -1 OR NOT invented_colon_three EQUAL -1 OR
   NOT doubled_period EQUAL -1 OR
   NOT legacy_pre EQUAL -1 OR NOT legacy_menu EQUAL -1)
  message(FATAL_ERROR
    "exported glossary lost canonical headings, terms, table, or terminal content")
endif()
