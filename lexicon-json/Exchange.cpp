#include "Exchange.h"

#include <chrono>
#include <format>
#include <map>
#include <set>

namespace lexicon::exchange {
namespace {
constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(std::string_view bytes) {
  std::string text;
  text.reserve((bytes.size() + 2) / 3 * 4);
  std::size_t index = 0;
  for (; index + 2 < bytes.size(); index += 3) {
    const auto chunk = (static_cast<unsigned char>(bytes[index]) << 16) |
                       (static_cast<unsigned char>(bytes[index + 1]) << 8) |
                       static_cast<unsigned char>(bytes[index + 2]);
    text += kAlphabet[(chunk >> 18) & 63];
    text += kAlphabet[(chunk >> 12) & 63];
    text += kAlphabet[(chunk >> 6) & 63];
    text += kAlphabet[chunk & 63];
  }
  if (index < bytes.size()) {
    const bool two = index + 1 < bytes.size();
    const auto chunk = (static_cast<unsigned char>(bytes[index]) << 16) |
                       (two ? static_cast<unsigned char>(bytes[index + 1]) << 8 : 0);
    text += kAlphabet[(chunk >> 18) & 63];
    text += kAlphabet[(chunk >> 12) & 63];
    text += two ? kAlphabet[(chunk >> 6) & 63] : '=';
    text += '=';
  }
  return text;
}

std::optional<std::string> base64Decode(std::string_view text) {
  if (text.size() % 4 != 0)
    return std::nullopt;
  const auto value = [](char ch) -> int {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
  };
  std::string bytes;
  bytes.reserve(text.size() / 4 * 3);
  for (std::size_t index = 0; index < text.size(); index += 4) {
    const bool last = index + 4 == text.size();
    const int padding = last ? (text[index + 3] == '=') + (text[index + 2] == '=') : 0;
    int sextets[4];
    for (int offset = 0; offset < 4; ++offset) {
      const bool padded = offset >= 4 - padding;
      sextets[offset] = padded ? 0 : value(text[index + offset]);
      if (sextets[offset] < 0)
        return std::nullopt;
    }
    const auto chunk = (sextets[0] << 18) | (sextets[1] << 12) | (sextets[2] << 6) | sextets[3];
    bytes += static_cast<char>((chunk >> 16) & 255);
    if (padding < 2) bytes += static_cast<char>((chunk >> 8) & 255);
    if (padding < 1) bytes += static_cast<char>(chunk & 255);
  }
  return bytes;
}

std::string utcNow() {
  const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
  return std::format("{:%Y-%m-%dT%H:%M:%SZ}", now);
}

Json linkToJson(const LinkRecord &link) {
  return Json{{"fromItemId", link.fromItemId},
              {"toItemId", link.toItemId},
              {"linkType", http::name(link.linkType)},
              {"position", link.position},
              {"customValue", link.customValue}};
}

const Json &requiredArray(const Json &document, const char *key) {
  const auto found = document.find(key);
  if (found == document.end() || !found->is_array())
    http::badRequest(std::string("'") + key + "' must be an array.");
  return *found;
}
} // namespace

Result<std::string> exportDocument(LexiconApplication &application, bool includeFiles) {
  auto dictionary = application.exchange.exportDictionary();
  if (!dictionary)
    return std::unexpected(dictionary.error());
  Json types = Json::array();
  std::map<int, ItemFieldRecord> fields;
  for (const auto &entry : dictionary->types) {
    Json type = http::toJson(entry.type);
    type["fields"] = http::toJsonArray(entry.fields);
    types.push_back(std::move(type));
    for (const auto &field : entry.fields)
      fields[field.id] = field;
  }
  Json links = Json::array();
  for (const auto &link : dictionary->links)
    links.push_back(linkToJson(link));
  Json document{{"format", kFormat},
                {"version", kVersion},
                {"exportedAt", utcNow()},
                {"groups", http::toJsonArray(dictionary->groups)},
                {"types", std::move(types)},
                {"items", http::toJsonArray(dictionary->items)},
                {"links", std::move(links)},
                {"alarms", http::toJsonArray(dictionary->alarms)}};
  if (includeFiles) {
    std::set<std::string> hashes;
    for (const auto &item : dictionary->items)
      for (const auto &[fieldId, value] : item.fieldValues)
        if (const auto field = fields.find(fieldId); field != fields.end())
          if (auto hash = blobHashOf(field->second, value); !hash.empty())
            hashes.insert(std::move(hash));
    Json blobs = Json::array();
    for (const auto &hash : hashes) {
      // A file missing from this database is left out; importing the value
      // then says so.
      auto data = application.blobs.readData(hash);
      if (data)
        blobs.push_back(Json{{"hash", hash}, {"data", base64Encode(*data)}});
    }
    document["blobs"] = std::move(blobs);
  }
  // A historical database may hold text that is not valid UTF-8; it is
  // replaced rather than failing the whole export.
  return document.dump(1, '\t', false, Json::error_handler_t::replace) + "\n";
}

Result<ImportReport> importDocument(LexiconApplication &application, std::string_view text) {
  const auto invalid = [](std::string message) {
    return std::unexpected(Error{Error::Code::Validation, std::move(message)});
  };
  const Json document = Json::parse(text, nullptr, false);
  if (document.is_discarded() || !document.is_object())
    return invalid("The file is not a Lexicon export: it is not a JSON object.");
  if (document.value("format", std::string{}) != kFormat)
    return invalid("The file is not a Lexicon export.");
  const auto version = document.find("version");
  if (version == document.end() || !version->is_number_integer())
    return invalid("The Lexicon export has no format version.");
  if (version->get<long long>() != kVersion)
    return invalid("The Lexicon export has format version " + std::to_string(version->get<long long>()) +
                   "; this Lexicon reads version " + std::to_string(kVersion) + ".");
  DictionaryExport dictionary;
  std::map<std::string, std::string> blobs;
  try {
    for (const auto &group : requiredArray(document, "groups"))
      dictionary.groups.push_back(http::groupFromJson(group));
    for (const auto &entry : requiredArray(document, "types")) {
      TypeExport type{http::typeFromJson(entry), {}};
      if (entry.contains("fields"))
        for (const auto &field : requiredArray(entry, "fields"))
          type.fields.push_back(http::fieldFromJson(field));
      dictionary.types.push_back(std::move(type));
    }
    for (const auto &item : requiredArray(document, "items"))
      dictionary.items.push_back(http::itemFromJson(item));
    for (const auto &link : requiredArray(document, "links"))
      dictionary.links.push_back(http::linkFromJson(link));
    if (document.contains("alarms"))
      for (const auto &alarm : requiredArray(document, "alarms"))
        dictionary.alarms.push_back(http::alarmFromJson(alarm));
    if (document.contains("blobs")) {
      for (const auto &blob : requiredArray(document, "blobs")) {
        const auto hash = http::requiredString(blob, "hash");
        auto data = base64Decode(http::requiredString(blob, "data"));
        if (!data)
          http::badRequest("The file with SHA-256 " + hash + " is not valid base64.");
        blobs.emplace(hash, std::move(*data));
      }
    }
  } catch (const http::BadRequest &bad) {
    return invalid("The Lexicon export is damaged: " + bad.message);
  }
  return application.exchange.importDictionary(dictionary, blobs);
}

Json toJson(const ImportReport &report) {
  return Json{{"groupsCreated", report.groupsCreated},
              {"typesCreated", report.typesCreated},
              {"fieldsCreated", report.fieldsCreated},
              {"itemsCreated", report.itemsCreated},
              {"itemsSkipped", report.itemsSkipped},
              {"linksCreated", report.linksCreated},
              {"blobsImported", report.blobsImported},
              {"alarmsCreated", report.alarmsCreated},
              {"warnings", report.warnings}};
}

std::string describe(const ImportReport &report) {
  std::string text = std::format(
      "Imported {} item(s), {} link(s), {} file(s) and {} alarm(s); {} item(s) were already here. "
      "Created {} group(s), {} type(s) and {} field(s).",
      report.itemsCreated, report.linksCreated, report.blobsImported, report.alarmsCreated,
      report.itemsSkipped, report.groupsCreated, report.typesCreated, report.fieldsCreated);
  for (const auto &warning : report.warnings)
    text += "\n- " + warning;
  return text;
}
} // namespace lexicon::exchange
