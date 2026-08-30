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

file(READ "${OUTPUT}/5-0.md" messages)
function(count_occurrences input_variable needle output_variable)
  string(LENGTH "${${input_variable}}" before_length)
  string(REPLACE "${needle}" "" without_matches "${${input_variable}}")
  string(LENGTH "${without_matches}" after_length)
  string(LENGTH "${needle}" needle_length)
  math(EXPR match_count
    "(${before_length} - ${after_length}) / ${needle_length}")
  set(${output_variable} "${match_count}" PARENT_SCOPE)
endfunction()

string(FIND "${messages}"
  "[Chapter 2, \"Problem](2-0.md)"
  first_resolved)
string(FIND "${messages}"
  "[Determination\" in topic 2\\.0](2-0.md)"
  second_resolved)
count_occurrences(messages "](2-0.md)" resolved_count)
string(FIND "${messages}" "(<#HDRPROBS>)" raw_target)
count_occurrences(messages "\n# " heading_count)
count_occurrences(messages "\n**Meaning:**" meaning_count)
count_occurrences(messages "\n**Action:**" action_count)
count_occurrences(messages "<a id=\"MSG " message_anchor_count)
string(FIND "${messages}"
  "After exiting the AIX NetView/6000 graphical interface, stop LNM for AIX\\. Then execute ovstop followed by ovstart\\. Use ovstatus to verify the AIX NetView/6000 daemons are running\\. Restart LNM for AIX\\."
  message_203_action)
string(FIND "${messages}"
  "**Action:** Refer to the man page for usage\\."
  message_218_action)
# MSG807's command listing renders verbatim: hosted serves it as plain
# preformatted lines inside the topic's <pre width="80"> and emits no <table>
# element on the page (DT 19941010174546).
string(FIND "${messages}"
  "```\nCommand type Command\n23006 LAN ADP LIST SEG=<segment number>"
  message_807_table)
string(FIND "${messages}"
  "- /usr/lpp/lnm/databases contains lnmlnmemgr\\.pdf\n- /usr/lib/nls/msg/"
  message_739_list)
string(FIND "${messages}"
  "```\nApplication Action\nCP Consult the nettl log"
  message_508_preformatted)
string(FIND "${messages}" "Restart the a Concentrator" bad_terminal_a)
string(FIND "${messages}" "view agent is set to unknown" bad_terminal_agent)
string(FIND "${messages}" "removed by from the database" bad_terminal_by)

if(first_resolved EQUAL -1 OR second_resolved EQUAL -1 OR
   NOT resolved_count EQUAL 2 OR NOT raw_target EQUAL -1 OR
   NOT heading_count EQUAL 1 OR NOT meaning_count EQUAL 396 OR
   NOT action_count EQUAL 396 OR NOT message_anchor_count EQUAL 396 OR
   message_203_action EQUAL -1 OR message_218_action EQUAL -1 OR
   message_807_table EQUAL -1 OR message_739_list EQUAL -1 OR
   message_508_preformatted EQUAL -1 OR
   NOT bad_terminal_a EQUAL -1 OR NOT bad_terminal_agent EQUAL -1 OR
   NOT bad_terminal_by EQUAL -1)
  message(FATAL_ERROR
    "typed message links, single heading, or semantic sections were not exported")
endif()
