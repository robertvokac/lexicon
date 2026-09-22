#pragma once
// Wiki links in item content: [[Title]], [[Title [disambiguation]]] and
// [[Title|shown text]], the last two combinable. Code blocks and code spans
// are left alone, so [[x]] in an example stays text.
#include <string>
#include <string_view>
#include <vector>

namespace lexicon {
struct WikiLink {
  std::size_t start = 0; // byte offsets of the whole [[...]] in the text
  std::size_t end = 0;
  std::string title;
  std::string disambiguation;
  std::string label; // the shown text, or empty for the title
};

std::vector<WikiLink> findWikiLinks(std::string_view markdown);

// The text with every wiki link turned into a Markdown link to `scheme` plus
// the percent-encoded "Title [disambiguation]", so any Markdown renderer
// shows it as a link the client can recognise.
std::string wikiLinksToMarkdown(std::string_view markdown, std::string_view scheme);

// Splits "Title [disambiguation]", the form of a wiki link target and of the
// item title suggestions.
WikiLink parseWikiTarget(std::string_view target);
} // namespace lexicon
