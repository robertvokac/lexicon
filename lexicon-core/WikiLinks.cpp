#include "WikiLinks.h"

#include "Validation.h"

#include <regex>

namespace lexicon {
namespace {
bool blank(std::string_view line) {
  return line.find_first_not_of(" \t\r") == std::string_view::npos;
}

// The fence a line opens or closes: its character and length, or 0.
std::pair<char, std::size_t> fence(std::string_view line) {
  std::size_t indent = 0;
  while (indent < line.size() && indent < 4 && line[indent] == ' ') ++indent;
  if (indent == 4 || indent >= line.size()) return {0, 0};
  const char marker = line[indent];
  if (marker != '`' && marker != '~') return {0, 0};
  std::size_t length = 0;
  while (indent + length < line.size() && line[indent + length] == marker) ++length;
  return length >= 3 ? std::pair{marker, length} : std::pair<char, std::size_t>{0, 0};
}

// [[title]], [[title [disambiguation]]], [[title|label]], [[title [d]|label]]
const std::regex &linkPattern() {
  static const std::regex pattern(R"(\[\[([^\[\]|\n]+?)(?:\s*\[([^\[\]\n]*)\])?(?:\s*\|([^\[\]\n]+))?\]\])");
  return pattern;
}

void scanLine(std::string_view text, std::size_t offset, std::size_t length,
              std::vector<WikiLink> &links) {
  std::size_t position = offset;
  const std::size_t end = offset + length;
  while (position < end) {
    const char ch = text[position];
    if (ch == '`') {
      // A code span: the same number of backticks closes it.
      std::size_t run = 0;
      while (position + run < end && text[position + run] == '`') ++run;
      const std::string closing(run, '`');
      std::size_t search = position + run;
      std::size_t close = std::string_view::npos;
      while (search < end) {
        const auto found = text.find(closing, search);
        if (found == std::string_view::npos || found >= end) break;
        std::size_t after = found + run;
        if (after < end && text[after] == '`') {
          while (after < end && text[after] == '`') ++after;
          search = after;
          continue;
        }
        close = found;
        break;
      }
      position = close == std::string_view::npos ? position + run : close + run;
      continue;
    }
    if (ch == '[' && position + 1 < end && text[position + 1] == '[') {
      std::cmatch match;
      if (std::regex_search(text.data() + position, text.data() + end, match, linkPattern(),
                            std::regex_constants::match_continuous)) {
        WikiLink link;
        link.start = position;
        link.end = position + static_cast<std::size_t>(match.length(0));
        link.title = trim(match.str(1));
        link.disambiguation = trim(match.str(2));
        link.label = trim(match.str(3));
        if (!link.title.empty()) {
          links.push_back(std::move(link));
          position += static_cast<std::size_t>(match.length(0));
          continue;
        }
      }
    }
    if (ch == '\\' && position + 1 < end) {
      position += 2; // An escaped character is never the start of anything.
      continue;
    }
    ++position;
  }
}

std::string percentEncode(std::string_view text) {
  static constexpr char digits[] = "0123456789ABCDEF";
  std::string encoded;
  for (unsigned char ch : text) {
    const bool unreserved = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                            (ch >= '0' && ch <= '9') || ch == '-' || ch == '.' || ch == '_' || ch == '~';
    if (unreserved) {
      encoded += static_cast<char>(ch);
    } else {
      encoded += '%';
      encoded += digits[ch >> 4];
      encoded += digits[ch & 15];
    }
  }
  return encoded;
}

std::string escapeLabel(std::string_view text) {
  std::string escaped;
  for (char ch : text) {
    if (ch == '\\' || ch == '[' || ch == ']' || ch == '*' || ch == '_' || ch == '`' || ch == '<')
      escaped += '\\';
    escaped += ch;
  }
  return escaped;
}
} // namespace

std::vector<WikiLink> findWikiLinks(std::string_view markdown) {
  std::vector<WikiLink> links;
  std::pair<char, std::size_t> openFence{0, 0};
  bool previousBlank = true;
  bool indentedCode = false;
  std::size_t start = 0;
  while (start <= markdown.size()) {
    auto newline = markdown.find('\n', start);
    if (newline == std::string_view::npos) newline = markdown.size();
    const auto line = markdown.substr(start, newline - start);
    const auto marker = fence(line);
    if (openFence.first != 0) {
      if (marker.first == openFence.first && marker.second >= openFence.second &&
          line.find_first_not_of(" \t\r`~") == std::string_view::npos)
        openFence = {0, 0};
    } else if (marker.first != 0) {
      openFence = marker;
    } else {
      const bool indented = line.starts_with("    ") || line.starts_with("\t");
      indentedCode = indented && !blank(line) && (previousBlank || indentedCode);
      if (!indentedCode) scanLine(markdown, start, line.size(), links);
    }
    previousBlank = blank(line);
    if (newline == markdown.size()) break;
    start = newline + 1;
  }
  return links;
}

std::string wikiLinksToMarkdown(std::string_view markdown, std::string_view scheme) {
  std::string result;
  std::size_t copied = 0;
  for (const auto &link : findWikiLinks(markdown)) {
    result.append(markdown.substr(copied, link.start - copied));
    const auto target = link.disambiguation.empty()
        ? link.title : link.title + " [" + link.disambiguation + "]";
    result += '[';
    result += escapeLabel(link.label.empty() ? link.title : link.label);
    result += "](";
    result += scheme;
    result += percentEncode(target);
    result += ')';
    copied = link.end;
  }
  result.append(markdown.substr(copied));
  return result;
}

WikiLink parseWikiTarget(std::string_view target) {
  WikiLink link;
  const auto text = trim(target);
  const auto open = text.rfind(" [");
  if (!text.empty() && text.back() == ']' && open != std::string::npos) {
    link.title = trim(text.substr(0, open));
    link.disambiguation = trim(text.substr(open + 2, text.size() - open - 3));
  } else {
    link.title = text;
  }
  return link;
}
} // namespace lexicon
