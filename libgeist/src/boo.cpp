#include "geist/boo.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace geist {
namespace {

std::vector<std::uint8_t> read_leading_bytes(std::ifstream& input,
                                             std::size_t count) {
  std::vector<std::uint8_t> bytes(count);
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  bytes.resize(static_cast<std::size_t>(input.gcount()));
  return bytes;
}

} // namespace

BooDocument BooDocument::open(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("failed to open BOO file: " + path.string());
  }

  input.seekg(0, std::ios::end);
  const auto end = input.tellg();
  if (end < 0) {
    throw std::runtime_error("failed to determine BOO file size: " +
                             path.string());
  }

  input.seekg(0, std::ios::beg);

  BooDocument document;
  document.metadata_.path = path;
  document.metadata_.file_size = static_cast<std::uint64_t>(end);
  document.metadata_.leading_bytes = read_leading_bytes(input, 16);
  return document;
}

const BooMetadata& BooDocument::metadata() const noexcept {
  return metadata_;
}

const std::vector<TocEntry>& BooDocument::table_of_contents() const noexcept {
  return toc_;
}

const std::vector<ResourceEntry>& BooDocument::resources() const noexcept {
  return resources_;
}

std::string BooDocument::render_chapter_markdown(
    const std::string& chapter_id) const {
  std::ostringstream output;
  output << "# " << (chapter_id.empty() ? "BOO chapter" : chapter_id) << "\n\n";
  output << "_Chapter rendering is not implemented yet._\n";
  return output.str();
}

std::string bytes_to_hex(const std::vector<std::uint8_t>& bytes) {
  std::ostringstream output;
  output << std::hex << std::setfill('0');

  for (std::size_t i = 0; i < bytes.size(); ++i) {
    if (i != 0) {
      output << ' ';
    }
    output << std::setw(2) << static_cast<unsigned>(bytes[i]);
  }

  return output.str();
}

} // namespace geist
