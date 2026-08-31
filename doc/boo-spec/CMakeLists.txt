# The BOO format specification, as a PDF.
#
# Documentation tooling is optional: when it is missing this file configures
# cleanly, prints why, and simply does not define the target. Nothing here is
# part of `all`, so a missing doc tool can never fail a build.
#
# Build it with:   cmake --build <build-dir> --target boo-spec-pdf
# Result:          <build-dir>/libgeist/doc/boo-spec/boo-spec.pdf
#
# Point GEIST_A2X_EXECUTABLE (or GEIST_ASCIIDOCTOR_PDF_EXECUTABLE) at a
# non-existent path to exercise the degraded configuration.

set(GEIST_BOO_SPEC_CHAPTERS
  scope.adoc
  container.adoc
  pages.adoc
  logical-controls.adoc
  encoding.adoc
  topics.adoc
  toc.adoc
  markup.adoc
  assets.adoc
  gdf.adoc
  mmr.adoc
  web-images.adoc
)

set(GEIST_BOO_SPEC_SOURCES boo-spec.adoc ${GEIST_BOO_SPEC_CHAPTERS} boo-spec.sty)
set(GEIST_BOO_SPEC_ABS_SOURCES "")
foreach(_geist_spec_file IN LISTS GEIST_BOO_SPEC_SOURCES)
  list(APPEND GEIST_BOO_SPEC_ABS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/${_geist_spec_file}")
endforeach()

# The AsciiDoc.py toolchain: a2x drives asciidoc -> DocBook -> LaTeX -> PDF
# through dblatex. This is the path the document is written and verified for.
find_program(GEIST_A2X_EXECUTABLE NAMES a2x a2x.py
  DOC "AsciiDoc.py a2x driver used to build the BOO format specification")
find_program(GEIST_DBLATEX_EXECUTABLE NAMES dblatex
  DOC "dblatex, the DocBook-to-LaTeX backend used by a2x --format=pdf")
find_program(GEIST_PDFLATEX_EXECUTABLE NAMES pdflatex
  DOC "pdflatex, the TeX engine dblatex drives by default")
# Asciidoctor is not required, and is not what the sources are verified
# against, but it is used when the AsciiDoc.py toolchain is absent.
find_program(GEIST_ASCIIDOCTOR_PDF_EXECUTABLE NAMES asciidoctor-pdf
  DOC "asciidoctor-pdf, an alternative renderer for the specification")

mark_as_advanced(
  GEIST_A2X_EXECUTABLE
  GEIST_DBLATEX_EXECUTABLE
  GEIST_PDFLATEX_EXECUTABLE
  GEIST_ASCIIDOCTOR_PDF_EXECUTABLE
)

# find_program does not search, and does not check, when the cache entry is
# already set. A path given on the command line may therefore not exist, which
# is exactly how the missing-toolchain path is exercised. Treat it as absent
# rather than emitting a build rule that cannot run.
foreach(_geist_spec_tool
    GEIST_A2X_EXECUTABLE
    GEIST_DBLATEX_EXECUTABLE
    GEIST_PDFLATEX_EXECUTABLE
    GEIST_ASCIIDOCTOR_PDF_EXECUTABLE)
  if(${_geist_spec_tool} AND NOT EXISTS "${${_geist_spec_tool}}")
    message(STATUS
      "BOO format specification: ${_geist_spec_tool} is set to "
      "'${${_geist_spec_tool}}', which does not exist; treating it as missing")
    set(${_geist_spec_tool} "${_geist_spec_tool}-NOTFOUND")
  endif()
endforeach()

set(_geist_spec_pdf "${CMAKE_CURRENT_BINARY_DIR}/boo-spec.pdf")

if(GEIST_A2X_EXECUTABLE AND GEIST_DBLATEX_EXECUTABLE
    AND GEIST_PDFLATEX_EXECUTABLE)
  # a2x warns that --destination-dir is ignored for PDF and writes the result
  # beside its input, so build from a staging copy rather than the source tree.
  set(_geist_spec_stage "${CMAKE_CURRENT_BINARY_DIR}/stage")

  add_custom_command(
    OUTPUT "${_geist_spec_pdf}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_geist_spec_stage}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${GEIST_BOO_SPEC_ABS_SOURCES} "${_geist_spec_stage}"
    COMMAND "${GEIST_A2X_EXECUTABLE}"
      --format=pdf
      --no-xmllint
      "--dblatex-opts=-s ${_geist_spec_stage}/boo-spec.sty -P doc.collab.show=0 -P latex.output.revhistory=0"
      "${_geist_spec_stage}/boo-spec.adoc"
    COMMAND ${CMAKE_COMMAND} -E copy
      "${_geist_spec_stage}/boo-spec.pdf" "${_geist_spec_pdf}"
    DEPENDS ${GEIST_BOO_SPEC_ABS_SOURCES}
    COMMENT "Building the BOO format specification with a2x and dblatex"
    VERBATIM
  )
  set(_geist_spec_renderer "a2x + dblatex")

elseif(GEIST_ASCIIDOCTOR_PDF_EXECUTABLE)
  add_custom_command(
    OUTPUT "${_geist_spec_pdf}"
    COMMAND "${GEIST_ASCIIDOCTOR_PDF_EXECUTABLE}"
      -a "reproducible"
      -o "${_geist_spec_pdf}"
      "${CMAKE_CURRENT_SOURCE_DIR}/boo-spec.adoc"
    DEPENDS ${GEIST_BOO_SPEC_ABS_SOURCES}
    COMMENT "Building the BOO format specification with asciidoctor-pdf"
    VERBATIM
  )
  set(_geist_spec_renderer "asciidoctor-pdf")
endif()

if(DEFINED _geist_spec_renderer)
  add_custom_target(boo-spec-pdf DEPENDS "${_geist_spec_pdf}")
  message(STATUS
    "BOO format specification: target 'boo-spec-pdf' available (${_geist_spec_renderer})")
else()
  set(_geist_spec_missing "")
  if(NOT GEIST_A2X_EXECUTABLE)
    list(APPEND _geist_spec_missing "a2x")
  endif()
  if(NOT GEIST_DBLATEX_EXECUTABLE)
    list(APPEND _geist_spec_missing "dblatex")
  endif()
  if(NOT GEIST_PDFLATEX_EXECUTABLE)
    list(APPEND _geist_spec_missing "pdflatex")
  endif()
  list(JOIN _geist_spec_missing ", " _geist_spec_missing)
  message(STATUS
    "BOO format specification: target 'boo-spec-pdf' unavailable, "
    "not found: ${_geist_spec_missing} (and no asciidoctor-pdf fallback). "
    "The specification sources in ${CMAKE_CURRENT_SOURCE_DIR} still build "
    "with any AsciiDoc toolchain; nothing else in this project needs them.")
endif()
