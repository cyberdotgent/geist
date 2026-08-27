#include "geist/document.hpp"
#include "lazy_open_support.hpp"

#include <filesystem>
#include <string>

int main() {
  const auto root = std::filesystem::path(GEIST_REPO_ROOT) / "BOO";

  const auto web_demo = geist::BooDocument::open(root / "XWEBDEMO.boo");
  const auto web_title = web_demo.topic_markdown("TITLE");
  require(web_title.find("IBM BookManager BookServer") != std::string::npos &&
              web_title.find("Document Number XWEBDEMO") !=
                  std::string::npos &&
              web_title.find("©IBM Corporation 1995, 1997") !=
                  std::string::npos,
          "generated title-page controls displaced visible text");
  for (const auto* leaked : {"c.sp 3p p c", "<>", "<IMAGE>"}) {
    require(web_title.find(leaked) == std::string::npos,
            "generated title-page presentation marker leaked into Markdown");
  }
  const auto web_introduction = web_demo.topic_markdown("1.0");
  require(web_introduction.find(
              "The past several years have seen dramatic growth") !=
              std::string::npos &&
              web_introduction.find(
                  "In particular a strong trend towards the use") !=
                  std::string::npos,
          "generated body-row controls displaced introduction text");
  for (const auto* leaked : {"c.sp 3p p c", "// The past", ":H1"}) {
    require(web_introduction.find(leaked) == std::string::npos,
            "generated body-row presentation marker leaked into Markdown");
  }
  require(web_introduction.find(
              "How BookServer works <http://booksrv2.raleigh.ibm.com/>") !=
              std::string::npos &&
              web_introduction.find("Opening books to the world.") !=
                  std::string::npos,
          "menu IR removed meaningful compact terminal title content");
  const auto web_advantages = web_demo.topic_markdown("1.1");
  require(web_advantages.find(
              "own or from multiple remote file systems. The actual") !=
              std::string::npos &&
              web_advantages.find("Services/ESA. These products") !=
                  std::string::npos,
          "continued fixed row lost advantages prose");
  for (const auto* leaked : {":H2", "not - part", "readers. = Therefore",
                             "access ' books",
                             "booksrv2.raleigh.ibm.com",
                             "operating systems book"}) {
    require(web_advantages.find(leaked) == std::string::npos,
            "continued fixed-row marker leaked into Markdown");
  }
  const auto web_opening = web_demo.topic_markdown("1.4");
  require(web_opening.find(
              "**Note:** Your ability to view or play the various media "
              "objects will depend **on** **the** **hardware** **and** "
              "**software** **configuration** **of** **your** "
              "**workstation.**") != std::string::npos &&
              web_opening.find("depend across") == std::string::npos,
          "fixed note continuation retained a carryover word or split row");
  const auto* web_working = web_demo.find_toc_entry("1.3");
  const auto* web_data = web_demo.find_toc_entry("1.4.3");
  require(web_working != nullptr && web_data != nullptr &&
              web_working->title == "How BookServer Works" &&
              web_data->title == "Data and Software",
          "selector-kind metadata leaked into a TOC title");
  require(web_demo.topic_markdown("1.0").find(
              "![Image](/bookmgr/product.gif)") != std::string::npos,
          "external product image selector was malformed");
  const auto web_external_pictures = web_demo.topic_markdown("1.4.1");
  require(web_external_pictures.find(
              "![Image](/bookmgr/monetcoq.jpg)") != std::string::npos,
          "inline external JPEG selector was malformed");
  require(web_external_pictures.find(
              "](/bookmgr/monetley.jpg)") != std::string::npos,
          "linked external JPEG selector lost its target");
  require(web_external_pictures.find("[Figure 3](#FIGMONET1)") !=
              std::string::npos &&
              web_external_pictures.find("[Figure 2](#FIGOVERVIE)") !=
                  std::string::npos,
          "external-picture cross-reference labels were torn");
  require(web_external_pictures.find("<IMAGE>") == std::string::npos &&
              web_external_pictures.find("<OTHER>") == std::string::npos &&
              web_external_pictures.find(
                  "/ An exciting new capability") == std::string::npos,
          "external-picture selector alternatives leaked into prose");
  const auto web_multimedia = web_demo.topic_markdown("1.4.2");
  require(web_multimedia.find("[Warp us out of here!](/bookmgr/entprise.mpg)") !=
              std::string::npos &&
              web_multimedia.find("[scream!](/bookmgr/scream1.wav)") !=
                  std::string::npos,
          "external multimedia selectors lost labels or targets");
  require(web_demo.topic_markdown("1.4.3").find(
              "[here](ftp://software.raleigh.ibm.com/os2/internet/webexplorer)") !=
              std::string::npos,
          "external FTP selector lost its label or target");
  require(web_demo.topic_markdown("1.4.4").find(
              "[The IBM Home Page](http://www.ibm.com/)") !=
              std::string::npos,
          "external HTTP selector lost its label or target");
  const auto web_figures = web_demo.topic_markdown("FIGURES");
  require(web_figures.find("c.sp 3p p c") == std::string::npos,
          "generated figure-list spacing control leaked into Markdown");
  require(web_figures.find(
              "[1. BookManager product family 1.2](#FIGBIGPIC)") !=
              std::string::npos &&
              web_figures.find(
                  "[3. External JPEG format image presented in-line 1.4.1]"
                  "(#FIGMONET1)") != std::string::npos,
          "external-picture figure index retained selector metadata");
}
