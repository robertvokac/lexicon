#pragma once
// The export file: a whole dictionary as one JSON document, written and read
// by the desktop client, the REST server and `LexiconServer export|import`.
// docs/export-format.md describes it.
#include "LexiconApplication.h"
#include "Transport.h"

#include <string>
#include <string_view>

namespace lexicon::exchange {
using Json = http::Json;

inline constexpr const char *kFormat = "lexicon-export";
inline constexpr int kVersion = 1;

// With `includeFiles`, the contents of every file a value refers to travel in
// the document, so it restores a dictionary on its own.
Result<std::string> exportDocument(LexiconApplication &application, bool includeFiles);
// Merges the document into the application's database; see
// ExchangeService::importDictionary. A document that is not an export, or
// comes from a newer format version, is refused before anything is written.
Result<ImportReport> importDocument(LexiconApplication &application, std::string_view text);

Json toJson(const ImportReport &report);
// A short summary for a person: what was created, skipped and why.
std::string describe(const ImportReport &report);
} // namespace lexicon::exchange
