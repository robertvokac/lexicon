#include "LexiconApplication.h"

int main() {
  lexicon::ItemRecord item;
  item.title = "Příliš žluťoučký kůň";
  item.groupId = 7;
  if (!lexicon::validateItem(item, {}))
    return 1;
  item.title = "  ";
  const auto invalid = lexicon::validateItem(item, {});
  if (invalid || invalid.error().code != lexicon::Error::Code::Validation)
    return 2;
  const auto unique =
      lexicon::cleanedUniqueValues({" Alpha ", "alpha", "beta"});
  if (unique.size() != 2)
    return 3;
  if (lexicon::asciiFold("ABCéÉ") != "abcéÉ")
    return 4;
  const auto unicode = lexicon::cleanedUniqueValues({"École", "école", "ALPHA", "alpha"});
  if (unicode.size() != 3 ||
      lexicon::asciiFold("École") == lexicon::asciiFold("école"))
    return 5;
  return 0;
}
