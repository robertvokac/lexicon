// Wiki links: what is a link, what is code, and the Markdown they become.
#include "WikiLinks.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool condition, const std::string &message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}
} // namespace

int main() {
  using lexicon::findWikiLinks;
  const auto links = findWikiLinks("See [[Monoid]], [[Monoid [algebra]]] and [[Semigroup|semigroups]].\n"
                                   "Also [[ Group [algebra] | groups ]].");
  check(links.size() == 4, "four links are found");
  if (links.size() == 4) {
    check(links[0].title == "Monoid" && links[0].disambiguation.empty() && links[0].label.empty(), "a plain link");
    check(links[1].title == "Monoid" && links[1].disambiguation == "algebra", "a link with a disambiguation");
    check(links[2].title == "Semigroup" && links[2].label == "semigroups", "a link with its own text");
    check(links[3].title == "Group" && links[3].disambiguation == "algebra" && links[3].label == "groups",
          "spaces around the parts are ignored");
  }
  check(findWikiLinks("Code `[[not a link]]` and ``a ` [[b]]`` here.").empty(), "code spans are skipped");
  check(findWikiLinks("```cpp\nstd::vector<int> v[[3]];\n[[Inside]]\n```\n").empty(), "fenced code is skipped");
  check(findWikiLinks("~~~~\n```\n[[Inside]]\n~~~~\n[[After]]").size() == 1, "a fence closes with its own kind");
  check(findWikiLinks("Text\n\n    [[Indented code]]\n").empty(), "indented code is skipped");
  check(findWikiLinks("\\[[Escaped]]").empty(), "an escaped bracket starts no link");
  check(findWikiLinks("[[]] [[ ]] [[a\nb]]").empty(), "empty and broken links are text");
  check(findWikiLinks("`unclosed [[Link]]").size() == 1, "an unclosed backtick is text");

  const auto markdown = lexicon::wikiLinksToMarkdown("A [[Monoid [algebra]|monoid]] and [[C++ *templates*]].",
                                                     "lexicon-item:");
  check(markdown == "A [monoid](lexicon-item:Monoid%20%5Balgebra%5D) and "
                    "[C++ \\*templates\\*](lexicon-item:C%2B%2B%20%2Atemplates%2A).",
        "links become Markdown links, got " + markdown);
  check(lexicon::wikiLinksToMarkdown("`[[x]]` stays", "lexicon-item:") == "`[[x]]` stays", "code stays as it was");

  const auto target = lexicon::parseWikiTarget("Monoid [algebra]");
  check(target.title == "Monoid" && target.disambiguation == "algebra", "a target splits into its parts");
  check(lexicon::parseWikiTarget("C++").title == "C++", "a target without a disambiguation stays whole");

  if (failures == 0) std::cout << "wiki_links: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
