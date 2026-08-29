#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto sclm = geist::BooDocument::open(root / "SC34-425.boo");
  const auto sclm_messages = sclm.topic_markdown("APPENDIX1.5.3");
  require(sclm_messages.find("<pre>") != std::string::npos &&
              sclm_messages.find("FLM00000 MESSAGE ID") !=
                  std::string::npos &&
              sclm_messages.find("<a id=\"MSG FLM00101\"></a>") !=
                  std::string::npos &&
              sclm_messages.find("**FLM00000**") == std::string::npos,
          "SCLM message catalog was flattened into styled prose");
  // APPENDIX1.5.4 now renders through the typed prose family, which keeps the
  // `CFONT` phrases as one emphasis inline each, exactly as hosted DT
  // 19921112160049 styles them (`<B>ACCT</B> <B>AND</B> ... <B>FLMALTC:</B>
  // <I>aaaaaaaa</I>`, then `<B>Macro:</B>  FLMAEND`).
  const auto sclm_mnotes = sclm.topic_markdown("APPENDIX1.5.4");
  require(sclm_mnotes.find("**ACCT AND EXPACCT NAMES SAME IN FLMCNTRL, "
                           "FLMALTC:** *aaaaaaaa*") != std::string::npos &&
              sclm_mnotes.find("**Macro:** FLMAEND") != std::string::npos &&
              sclm_mnotes.find("**ACCT**") == std::string::npos,
          "SCLM MNOTE catalog was flattened into styled prose");
  // GLOSSARY now renders through the typed glossary family: each entry keeps
  // its `GLS <term>` anchor and the `CFONT` over the term, as hosted DT
  // 19921112160049 serves it (`<a name="GLS access key">   <B>access</B>
  // <B>key</B>.  An identifier used to restrict access to a member.</a>`).
  const auto sclm_glossary = sclm.topic_markdown("GLOSSARY");
  require(sclm_glossary.find("<a id=\"GLS access key\"></a>") !=
                  std::string::npos &&
              sclm_glossary.find(
                  "**access key**\\. An identifier used to restrict access to "
                  "a member") != std::string::npos,
          "SCLM glossary lost its fixed-layout rows");
  // 1.9.2 renders through the typed prose route (one composed drawn-figure
  // span): hosted DT 19921112160049 serves `<a name="FIGFIGUNIQ63">` and the
  // program inside `<pre width="80">`, which the figure block lowers to a
  // bare fence.
  const auto pli_example = sclm.topic_markdown("1.9.2");
  require(pli_example.find("```") != std::string::npos &&
              pli_example.find("<a id=\"FIGFIGUNIQ63\"></a>") !=
                  std::string::npos &&
              pli_example.find("SCLM SERVICE PROCEDURES") !=
                  std::string::npos &&
              pli_example.find("**SCLM**") == std::string::npos,
          "fixed PL/I figure was rendered as inline emphasis");
}
