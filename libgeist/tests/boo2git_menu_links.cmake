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

file(READ "${OUTPUT}/5-6.md" typed_menu)
string(FIND "${typed_menu}"
  "[Functions Supported by the VM/VSE Interface](5-6-1.md)"
  typed_resolved)
string(FIND "${typed_menu}" "](<5.6.1>)" typed_raw)
string(FIND "${typed_menu}" "# VM/VSE Interface" duplicate_heading)
if(typed_resolved EQUAL -1 OR NOT typed_raw EQUAL -1 OR
   NOT duplicate_heading EQUAL -1)
  message(FATAL_ERROR
    "canonical typed menu target or anchor-leading heading was not exported")
endif()

file(READ "${OUTPUT}/6-2-1.md" legacy_topic)
string(FIND "${legacy_topic}"
  "topic 6.4.1](6-4-1.md)" legacy_resolved)
string(FIND "${legacy_topic}" "](#6.4.1)" legacy_raw)
if(legacy_resolved EQUAL -1 OR NOT legacy_raw EQUAL -1)
  message(FATAL_ERROR
    "legacy anchor-style topic target rewriting regressed")
endif()
