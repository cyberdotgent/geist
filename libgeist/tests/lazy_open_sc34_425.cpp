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
  const auto sclm_mnotes = sclm.topic_markdown("APPENDIX1.5.4");
  require(sclm_mnotes.find("<pre>") != std::string::npos &&
              sclm_mnotes.find("ACCT AND EXPACCT NAMES SAME") !=
                  std::string::npos &&
              sclm_mnotes.find("**ACCT**") == std::string::npos,
          "SCLM MNOTE catalog was flattened into styled prose");
  const auto sclm_glossary = sclm.topic_markdown("GLOSSARY");
  require(sclm_glossary.find("<pre>") != std::string::npos &&
              sclm_glossary.find("access key.  An identifier") !=
                  std::string::npos,
          "SCLM glossary lost its fixed-layout rows");
  const auto pli_example = sclm.topic_markdown("1.9.2");
  require(pli_example.find("```text") != std::string::npos &&
              pli_example.find("SCLM SERVICE PROCEDURES") !=
                  std::string::npos &&
              pli_example.find("**SCLM**") == std::string::npos,
          "fixed PL/I figure was rendered as inline emphasis");
}
