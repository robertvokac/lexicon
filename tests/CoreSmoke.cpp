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
  return 0;
}
