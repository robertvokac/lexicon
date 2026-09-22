// REST parity: every service the Qt client uses must be reachable over HTTP
// with the same semantics.
#include "support/HttpTestClient.h"
#include "support/ServerHarness.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

using lexicontest::Checks;
using lexicontest::HttpResponse;
using lexicontest::HttpTestClient;
using lexicontest::ServerHarness;
using Json = nlohmann::json;

namespace {
Json parse(const HttpResponse &response) {
  auto json = Json::parse(response.body, nullptr, false);
  return json.is_discarded() ? Json::object() : json;
}

class Session {
public:
  Session(ServerHarness &harness, Checks &checks)
      : checks_(checks), client_("127.0.0.1", harness.port()) {
    const auto login = client_.post(
        "/api/v1/auth/login",
        Json{{"username", harness.options().username},
             {"password", harness.options().password}}
            .dump());
    checks_.expectEqual(login.status, 200, "test session login");
    client_.setBearerToken(parse(login).value("token", std::string{}));
  }
  HttpTestClient &client() { return client_; }

private:
  Checks &checks_;
  HttpTestClient client_;
};

void checkGroups(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();

  const auto initial = client.get("/api/v1/groups");
  checks.expectEqual(initial.status, 200, "groups can be listed");
  const auto defaults = client.get("/api/v1/groups/default");
  checks.expectEqual(defaults.status, 200, "the default group is reachable");
  const int defaultId = parse(defaults).value("groupId", 0);
  checks.expect(defaultId > 0, "the default group has an ID");

  const auto groups = parse(client.get("/api/v1/groups")).at("groups");
  std::string defaultName;
  for (const auto &group : groups)
    if (group.value("id", 0) == defaultId)
      defaultName = group.value("name", std::string{});
  // The default group is Default, not Inbox.
  checks.expectEqual(defaultName, "Default", "the default group is Default");

  const auto created = client.post(
      "/api/v1/groups",
      Json{{"name", "Mathematics"}, {"description", "Maths"}, {"position", 5}}
          .dump());
  checks.expectEqual(created.status, 201, "a group can be created");
  const auto createdGroup = parse(created).at("group");
  const int groupId = createdGroup.value("id", 0);
  checks.expect(groupId > 0, "a created group reports its ID");
  checks.expectEqual(createdGroup.value("position", 0), 5,
                     "the position round-trips");

  checks.expectEqual(
      client
          .post("/api/v1/groups", Json{{"name", "WithId"}, {"id", 77}}.dump())
          .status,
      400, "a new group may not carry an ID");
  checks.expectEqual(client.post("/api/v1/groups", R"({"name":"  "})").status,
                     400, "an empty group name is rejected by validation");

  const auto updated = client.put(
      "/api/v1/groups/" + std::to_string(groupId),
      Json{{"name", "Mathematics and logic"}, {"position", 6}}.dump());
  checks.expectEqual(updated.status, 200, "a group can be updated");
  checks.expectEqual(parse(updated).at("group").value("name", std::string{}),
                     "Mathematics and logic", "the new name is returned");
  checks.expectEqual(
      client.put("/api/v1/groups/999999", Json{{"name", "Ghost"}}.dump()).status,
      404, "updating a missing group is a not-found error");
  checks.expectEqual(
      client.remove("/api/v1/groups/" + std::to_string(groupId)).status, 204,
      "a group can be deleted");
  checks.expectEqual(client.remove("/api/v1/groups/" + std::to_string(groupId))
                         .status,
                     404, "deleting it twice is a not-found error");
}

void checkTypesAndFields(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId = parse(client.get("/api/v1/groups/default")).value("groupId", 0);

  const auto createdType = client.post(
      "/api/v1/types", Json{{"name", "Theorem"},
                            {"description", "A proven statement"},
                            {"groupId", groupId}}
                           .dump());
  checks.expectEqual(createdType.status, 201, "a type can be created");
  const int typeId = parse(createdType).at("type").value("id", 0);
  checks.expect(typeId > 0, "a created type reports its ID");

  const auto sharedType = client.post(
      "/api/v1/types", Json{{"name", "Note"}, {"groupId", nullptr}}.dump());
  checks.expectEqual(sharedType.status, 201, "a type can span all groups");
  checks.expect(parse(sharedType).at("type").at("groupId").is_null(),
                "an all-groups type reports a null group");

  const auto scoped =
      client.get("/api/v1/types?groupId=" + std::to_string(groupId));
  checks.expectEqual(scoped.status, 200, "types can be filtered by group");
  checks.expectEqual(static_cast<long long>(parse(scoped).at("types").size()), 2,
                     "the group sees its own and the shared type");

  // Every field data type the desktop supports must survive the round trip.
  const std::vector<std::string> dataTypes = {
      "Integer", "Float",   "Text", "Date", "Time",
      "Timestamp", "Boolean", "Enum", "Blob", "Other"};
  std::vector<int> fieldIds;
  int position = 0;
  for (const auto &dataType : dataTypes) {
    Json field{{"name", dataType + " field"},
               {"dataType", dataType},
               {"position", position++}};
    if (dataType == "Enum")
      field["enumOptions"] = Json::array({"alpha", "beta"});
    const auto created =
        client.post("/api/v1/types/" + std::to_string(typeId) + "/fields",
                    field.dump());
    checks.expectEqual(created.status, 201, "field " + dataType + " is created");
    const auto stored = parse(created).at("field");
    checks.expectEqual(stored.value("dataType", std::string{}), dataType,
                       "field " + dataType + " keeps its data type");
    fieldIds.push_back(stored.value("id", 0));
  }
  checks.expectEqual(
      client
          .post("/api/v1/types/" + std::to_string(typeId) + "/fields",
                Json{{"name", "Bad"}, {"dataType", "Quaternion"}}.dump())
          .status,
      400, "an unknown data type is rejected");
  checks.expectEqual(
      client
          .post("/api/v1/types/" + std::to_string(typeId) + "/fields",
                Json{{"name", "NoOptions"}, {"dataType", "Enum"}}.dump())
          .status,
      400, "an enum field without options is rejected");

  const auto fields =
      client.get("/api/v1/types/" + std::to_string(typeId) + "/fields");
  checks.expectEqual(static_cast<long long>(parse(fields).at("fields").size()),
                     static_cast<long long>(dataTypes.size()),
                     "all fields are listed");

  const auto counts =
      client.get("/api/v1/types/" + std::to_string(typeId) + "/item-count");
  checks.expectEqual(counts.status, 200, "the affected item count is available");
  checks.expectEqual(parse(counts).value("count", -1), 0,
                     "an unused type affects no items");
  const auto valueCount = client.get("/api/v1/fields/" +
                                     std::to_string(fieldIds.front()) +
                                     "/value-count");
  checks.expectEqual(valueCount.status, 200,
                     "the affected value count is available");

  const auto renamed =
      client.put("/api/v1/fields/" + std::to_string(fieldIds.front()),
                 Json{{"name", "Renamed integer"},
                      {"dataType", "Integer"},
                      {"itemTypeId", typeId},
                      {"position", 0}}
                     .dump());
  checks.expectEqual(renamed.status, 200, "a field can be updated");
  checks.expectEqual(parse(renamed).at("field").value("name", std::string{}),
                     "Renamed integer", "the field keeps its new name");
  checks.expectEqual(
      client
          .put("/api/v1/fields/" + std::to_string(fieldIds.front()),
               Json{{"name", "Wrong type"},
                    {"dataType", "Integer"},
                    {"itemTypeId", 999999}}
                   .dump())
          .status,
      404, "a field cannot be moved to another type");
  checks.expectEqual(
      client.remove("/api/v1/fields/" + std::to_string(fieldIds.back())).status,
      204, "a field can be deleted");
  checks.expectEqual(client.remove("/api/v1/types/" + std::to_string(typeId))
                         .status,
                     204, "a type can be deleted");
}

void checkItems(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId =
      parse(client.get("/api/v1/groups/default")).value("groupId", 0);

  const auto typeResponse = client.post(
      "/api/v1/types", Json{{"name", "Concept"}, {"groupId", groupId}}.dump());
  const int typeId = parse(typeResponse).at("type").value("id", 0);
  const auto enumField = client.post(
      "/api/v1/types/" + std::to_string(typeId) + "/fields",
      Json{{"name", "Difficulty"},
           {"dataType", "Enum"},
           {"position", 0},
           {"enumOptions", Json::array({"easy", "hard"})}}
          .dump());
  const int enumFieldId = parse(enumField).at("field").value("id", 0);
  const auto dateField =
      client.post("/api/v1/types/" + std::to_string(typeId) + "/fields",
                  Json{{"name", "Introduced"},
                       {"dataType", "Date"},
                       {"position", 1}}
                      .dump());
  const int dateFieldId = parse(dateField).at("field").value("id", 0);

  Json item{{"groupId", groupId},
            {"itemTypeId", typeId},
            {"title", "Příliš žluťoučký kůň"},
            {"disambiguation", "pangram"},
            {"aliases", Json::array({"Žluťoučký kůň"})},
            {"tags", Json::array({"czech", "utf8"})},
            {"flags", Json::array({"review"})},
            {"status", "Draft"},
            {"understanding", "Practiced"},
            {"pinned", true},
            {"content", "# Kůň\n\nA **unicode** pangram."},
            {"properties", Json::array({Json{{"key", "source"},
                                             {"value", "folklore"}}})},
            {"fieldValues",
             Json{{std::to_string(enumFieldId), "hard"},
                  {std::to_string(dateFieldId), "2026-09-20"}}}};

  const auto created = client.post("/api/v1/items", Json{{"item", item}}.dump());
  checks.expectEqual(created.status, 201, "an item can be created");
  const int itemId = parse(created).value("id", 0);
  checks.expect(itemId > 0, "a created item reports its ID");
  const auto stored = parse(created).at("item");
  checks.expectEqual(stored.value("title", std::string{}),
                     "Příliš žluťoučký kůň", "UTF-8 titles round-trip");
  checks.expectEqual(stored.value("status", std::string{}), "Draft",
                     "the status is symbolic");
  checks.expectEqual(stored.value("understanding", std::string{}), "Practiced",
                     "the understanding level is symbolic");
  checks.expect(stored.value("pinned", false), "the pinned flag round-trips");
  checks.expectEqual(static_cast<long long>(stored.at("tags").size()), 2,
                     "tags round-trip");
  checks.expectEqual(stored.at("properties").at(0).value("key", std::string{}),
                     "source", "properties round-trip");
  checks.expectEqual(
      stored.at("fieldValues").value(std::to_string(enumFieldId), std::string{}),
      "hard", "enum values round-trip");
  checks.expectEqual(
      stored.at("fieldValues").value(std::to_string(dateFieldId), std::string{}),
      "2026-09-20", "date values keep the stored representation");

  checks.expectEqual(
      client
          .post("/api/v1/items",
                Json{{"item", Json{{"groupId", groupId},
                                   {"itemTypeId", typeId},
                                   {"title", "Invalid enum value"},
                                   {"fieldValues",
                                    Json{{std::to_string(enumFieldId),
                                          "impossible"}}}}}}
                    .dump())
          .status,
      400, "a value outside the enum options is rejected");
  checks.expectEqual(
      client
          .post("/api/v1/items",
                Json{{"item", Json{{"groupId", groupId}, {"title", "  "}}}}
                    .dump())
          .status,
      400, "an empty title is rejected");
  checks.expectEqual(
      client
          .post("/api/v1/items",
                Json{{"item", Json{{"id", 5},
                                   {"groupId", groupId},
                                   {"title", "Has an ID"}}}}
                    .dump())
          .status,
      400, "a new item may not carry an ID");

  const auto loaded = client.get("/api/v1/items/" + std::to_string(itemId));
  checks.expectEqual(loaded.status, 200, "an item can be loaded");
  checks.expectEqual(parse(loaded).at("item").value("content", std::string{}),
                     "# Kůň\n\nA **unicode** pangram.",
                     "Markdown content round-trips");

  item["title"] = "Kůň";
  item["status"] = "Completed";
  const auto updated = client.put("/api/v1/items/" + std::to_string(itemId),
                                  Json{{"item", item}}.dump());
  checks.expectEqual(updated.status, 200, "an item can be updated");
  checks.expectEqual(parse(updated).at("item").value("title", std::string{}),
                     "Kůň", "the update is stored");
  checks.expectEqual(parse(updated).value("id", 0), itemId,
                     "the path ID wins over the body");

  checks.expectEqual(
      client.post("/api/v1/items/" + std::to_string(itemId) + "/read", "{}")
          .status,
      204, "a read can be logged");

  // Pagination, sorting and filtering happen on the server.
  for (int index = 0; index < 5; ++index)
    client.post("/api/v1/items",
                Json{{"item", Json{{"groupId", groupId},
                                   {"title", "Item " + std::to_string(index)},
                                   {"tags", Json::array({"bulk"})}}}}
                    .dump());
  const auto page = client.post("/api/v1/items/query",
                                Json{{"limit", 2},
                                     {"offset", 0},
                                     {"sortColumn", 3},
                                     {"sortOrder", "Ascending"}}
                                    .dump());
  checks.expectEqual(page.status, 200, "items can be queried");
  checks.expectEqual(parse(page).value("totalCount", 0), 6,
                     "the total count covers every match");
  checks.expectEqual(static_cast<long long>(parse(page).at("items").size()), 2,
                     "the page size is honoured");
  const auto secondPage = client.post("/api/v1/items/query",
                                      Json{{"limit", 2},
                                           {"offset", 2},
                                           {"sortColumn", 3},
                                           {"sortOrder", "Ascending"}}
                                          .dump());
  checks.expect(parse(secondPage).at("items").at(0).value("title",
                                                          std::string{}) !=
                    parse(page).at("items").at(0).value("title", std::string{}),
                "offsets move through the result set");
  const auto descending = client.post("/api/v1/items/query",
                                      Json{{"limit", 6},
                                           {"sortColumn", 3},
                                           {"sortOrder", "Descending"}}
                                          .dump());
  const auto ascending = client.post("/api/v1/items/query",
                                     Json{{"limit", 6},
                                          {"sortColumn", 3},
                                          {"sortOrder", "Ascending"}}
                                         .dump());
  checks.expectEqual(
      parse(descending).at("items").at(0).value("title", std::string{}),
      parse(ascending).at("items").at(5).value("title", std::string{}),
      "sorting is reversible");

  const auto tagged = client.post("/api/v1/items/query",
                                  Json{{"tagFilter", "bulk"}}.dump());
  checks.expectEqual(parse(tagged).value("totalCount", 0), 5,
                     "a tag filter narrows the result");
  const auto pinned =
      client.post("/api/v1/items/query", Json{{"pinnedFilter", true}}.dump());
  checks.expectEqual(parse(pinned).value("totalCount", 0), 1,
                     "the pinned filter works");
  const auto statusFiltered = client.post(
      "/api/v1/items/query", Json{{"statusFilter", "Completed"}}.dump());
  checks.expectEqual(parse(statusFiltered).value("totalCount", 0), 1,
                     "the status filter works");
  const auto understanding = client.post(
      "/api/v1/items/query", Json{{"understandingFilter", "Practiced"}}.dump());
  checks.expectEqual(parse(understanding).value("totalCount", 0), 1,
                     "the understanding filter works");
  const auto searched = client.post("/api/v1/items/query",
                                    Json{{"searchText", "kůň"}}.dump());
  checks.expectEqual(parse(searched).value("totalCount", 0), 1,
                     "search covers UTF-8 titles");
  const auto byColumn = client.post(
      "/api/v1/items/query",
      Json{{"columnFilters", Json{{"title", "Item 3"}}}}.dump());
  checks.expectEqual(parse(byColumn).value("totalCount", 0), 1,
                     "column filters work");
  const auto byId = client.post(
      "/api/v1/items/query",
      Json{{"columnFilters", Json{{"id", std::to_string(itemId)}}}}.dump());
  checks.expectEqual(parse(byId).value("totalCount", 0), 1,
                     "the ID column filter works");
  const auto byProperty = client.post(
      "/api/v1/items/query",
      Json{{"propertyFilters",
            Json::array({Json{{"key", "source"}, {"value", "folk"}}})}}
          .dump());
  checks.expectEqual(parse(byProperty).value("totalCount", 0), 1,
                     "property filters match a key and a contained value");
  const auto byPropertyKeyOnly = client.post(
      "/api/v1/items/query",
      Json{{"propertyFilters", Json::array({Json{{"key", "source"}}})}}.dump());
  checks.expectEqual(parse(byPropertyKeyOnly).value("totalCount", 0), 1,
                     "an empty property value matches any value");
  const auto byValueFilter = client.post(
      "/api/v1/items/query",
      Json{{"typeId", typeId},
           {"valueFilters", Json::array({Json{{"fieldId", enumFieldId},
                                              {"value", "hard"},
                                              {"exact", true}}})}}
          .dump());
  checks.expectEqual(parse(byValueFilter).value("totalCount", 0), 1,
                     "type field value filters work");
  const auto sortedByField = client.post("/api/v1/items/query",
                                         Json{{"typeId", typeId},
                                              {"sortColumn", 11},
                                              {"limit", 5}}
                                             .dump());
  checks.expectEqual(sortedByField.status, 200,
                     "type field columns can be sorted");

  checks.expectEqual(client.remove("/api/v1/items/" + std::to_string(itemId))
                         .status,
                     204, "an item can be deleted");
  checks.expectEqual(client.get("/api/v1/items/" + std::to_string(itemId))
                         .status,
                     404, "a deleted item is gone");
}

void checkLinks(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId =
      parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  const auto create = [&](const std::string &title) {
    return parse(client.post("/api/v1/items",
                             Json{{"item", Json{{"groupId", groupId},
                                                {"title", title}}}}
                                 .dump()))
        .value("id", 0);
  };
  const int algebra = create("Algebra");
  const int group = create("Group");
  const int ring = create("Ring");

  // The atomic item plus links save is the primary path, exactly as in the Qt
  // item dialog.
  const auto saved = client.put(
      "/api/v1/items/" + std::to_string(group),
      Json{{"item", Json{{"groupId", groupId}, {"title", "Group"}}},
           {"links", Json::array({Json{{"toItemId", algebra},
                                       {"linkType", "PartOf"},
                                       {"position", 1}},
                                  Json{{"toItemId", ring},
                                       {"linkType", "Custom"},
                                       {"customValue", "generalizes"},
                                       {"position", 2}}})},
           {"backlinks", Json::array({Json{{"fromItemId", ring},
                                           {"linkType", "Uses"},
                                           {"position", 0}}})}}
          .dump());
  checks.expectEqual(saved.status, 200, "an item saves with links");

  const auto links =
      client.get("/api/v1/items/" + std::to_string(group) + "/links");
  checks.expectEqual(links.status, 200, "links can be listed");
  const auto linkArray = parse(links).at("links");
  checks.expectEqual(static_cast<long long>(linkArray.size()), 2,
                     "both links were stored");
  checks.expectEqual(linkArray.at(0).value("linkType", std::string{}), "PartOf",
                     "link types are symbolic");
  checks.expectEqual(linkArray.at(1).value("customValue", std::string{}),
                     "generalizes", "a custom link keeps its value");
  checks.expect(!linkArray.at(0).value("toItemTitle", std::string{}).empty(),
                "links carry the target title");
  const auto backlinks =
      client.get("/api/v1/items/" + std::to_string(group) + "/backlinks");
  checks.expectEqual(static_cast<long long>(parse(backlinks).at("backlinks").size()), 1,
                     "backlinks were stored");

  const auto bundled = client.get("/api/v1/items/" + std::to_string(group) +
                                  "?include=links,backlinks");
  checks.expectEqual(bundled.status, 200, "an item can include its links");
  checks.expect(parse(bundled).contains("links") &&
                    parse(bundled).contains("backlinks"),
                "the bundle carries both directions");
  checks.expectEqual(
      client.get("/api/v1/items/" + std::to_string(group) + "?include=nonsense")
          .status,
      400, "an unknown include value is rejected");

  // Custom links need a value; every other type stores none.
  checks.expectEqual(
      client
          .put("/api/v1/items/" + std::to_string(group),
               Json{{"item", Json{{"groupId", groupId}, {"title", "Group"}}},
                    {"links", Json::array({Json{{"toItemId", algebra},
                                                {"linkType", "Custom"},
                                                {"customValue", "  "}}})}}
                   .dump())
          .status,
      400, "a custom link without a value is rejected");
  checks.expectEqual(
      client
          .put("/api/v1/items/" + std::to_string(group),
               Json{{"item", Json{{"groupId", groupId}, {"title", "Group"}}},
                    {"links", Json::array({Json{{"toItemId", algebra},
                                                {"linkType", "None"}}})}}
                   .dump())
          .status,
      400, "None is not a persistable link type");
  checks.expectEqual(
      client
          .put("/api/v1/items/" + std::to_string(group),
               Json{{"item", Json{{"groupId", groupId}, {"title", "Group"}}},
                    {"links", Json::array({Json{{"toItemId", algebra},
                                                {"linkType", "Sideways"}}})}}
                   .dump())
          .status,
      400, "an unknown link type is rejected");

  // A failed save must leave the previous links untouched.
  checks.expectEqual(
      static_cast<long long>(
          parse(client.get("/api/v1/items/" + std::to_string(group) + "/links"))
              .at("links")
              .size()),
      2, "a rejected save does not change stored links");

  const auto standalone = client.post(
      "/api/v1/links", Json{{"fromItemId", algebra},
                            {"toItemId", ring},
                            {"linkType", "Related"},
                            {"position", 0}}
                           .dump());
  checks.expectEqual(standalone.status, 201, "a link can be created directly");
  const int linkId = parse(standalone).at("links").at(0).value("id", 0);
  checks.expectEqual(
      client
          .put("/api/v1/links/" + std::to_string(linkId),
               Json{{"fromItemId", algebra},
                    {"toItemId", ring},
                    {"linkType", "Contrasts"}}
                   .dump())
          .status,
      200, "a link can be updated directly");
  checks.expectEqual(
      client.remove("/api/v1/links/" + std::to_string(linkId)).status, 204,
      "a link can be deleted directly");

  // Deleting an item removes its links through the schema.
  checks.expectEqual(client.remove("/api/v1/items/" + std::to_string(ring))
                         .status,
                     204, "the linked item can be deleted");
  checks.expectEqual(
      static_cast<long long>(
          parse(client.get("/api/v1/items/" + std::to_string(group) + "/links"))
              .at("links")
              .size()),
      1, "links to a deleted item disappear");
}

void checkSearchAndUsage(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId =
      parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  client.post("/api/v1/items",
              Json{{"item", Json{{"groupId", groupId},
                                 {"title", "Monoid"},
                                 {"disambiguation", "algebra"},
                                 {"aliases", Json::array({"Semigroup unit"})},
                                 {"tags", Json::array({"algebra", "structure"})},
                                 {"flags", Json::array({"todo"})}}}}
                  .dump());
  client.post("/api/v1/items",
              Json{{"item", Json{{"groupId", groupId},
                                 {"title", "Monad"},
                                 {"tags", Json::array({"algebra"})}}}}
                  .dump());

  const auto suggestions = client.get("/api/v1/search/suggestions");
  checks.expectEqual(suggestions.status, 200, "suggestions are available");
  checks.expect(parse(suggestions).at("values").size() >= 3,
                "suggestions include titles and aliases");
  const auto titles = client.get("/api/v1/search/item-titles");
  checks.expect(parse(titles).at("values").size() == 2,
                "item titles are available");
  checks.expect(parse(titles).at("values").at(1).get<std::string>().find(
                    "[algebra]") != std::string::npos,
                "titles carry their disambiguation");

  const auto tags = client.get("/api/v1/usage/tags");
  checks.expectEqual(tags.status, 200, "tag usage is available");
  checks.expectEqual(parse(tags).at("values").at(0).value("usageCount", 0), 2,
                     "tag usage counts are correct");
  checks.expectEqual(client.get("/api/v1/usage/flags").status, 200,
                     "flag usage is available");
  checks.expectEqual(client.get("/api/v1/usage/aliases").status, 200,
                     "alias usage is available");

  const auto resolved = client.get("/api/v1/items/resolve?title=Monoid&disambiguation=algebra");
  checks.expectEqual(resolved.status, 200, "an item resolves by title");
  checks.expect(parse(resolved).value("itemId", 0) > 0,
                "the resolved item has an ID");
  checks.expectEqual(parse(client.get("/api/v1/items/resolve?title=monoid")).value("itemId", 0),
                     parse(resolved).value("itemId", 0), "a title resolves ignoring case");
  checks.expectEqual(parse(client.get("/api/v1/items/resolve?title=semigroup%20UNIT")).value("itemId", 0),
                     parse(resolved).value("itemId", 0), "and so does an alias");
  checks.expectEqual(client.get("/api/v1/items/resolve?title=MONOID&disambiguation=ALGEBRA").status, 200,
                     "a disambiguation also ignores case");
  checks.expectEqual(client.get("/api/v1/items/resolve?title=Nothing").status,
                     404, "an unknown title is a not-found error");
  checks.expectEqual(client.get("/api/v1/items/resolve?title=").status, 400,
                     "an empty title is rejected");

  // The search text also finds item content, and the exact title leads.
  client.post("/api/v1/items",
              Json{{"item", Json{{"groupId", groupId},
                                 {"title", "Group"},
                                 {"content", "A monoid in which every element has an inverse."}}}}
                  .dump());
  const auto found = parse(client.post("/api/v1/items/query",
                                       Json{{"searchText", "monoid"}, {"sortColumn", 0}}.dump()));
  checks.expectEqual(found.value("totalCount", 0), 2, "the search text finds content");
  checks.expectEqual(found.at("items").at(0).value("title", std::string{}), "Monoid",
                     "the exact title comes first");
  checks.expectEqual(found.at("items").at(1).value("title", std::string{}), "Group",
                     "an item mentioning the text in its content follows");
}

void checkBlobs(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId =
      parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  const auto type = client.post(
      "/api/v1/types", Json{{"name", "Attachment"}, {"groupId", groupId}}.dump());
  const int typeId = parse(type).at("type").value("id", 0);
  const auto field =
      client.post("/api/v1/types/" + std::to_string(typeId) + "/fields",
                  Json{{"name", "File"}, {"dataType", "Blob"}}.dump());
  const int fieldId = parse(field).at("field").value("id", 0);

  std::string payload;
  payload.reserve(40000);
  for (int index = 0; index < 5000; ++index)
    payload += "Přílišný obsah ";
  const auto uploaded =
      client.post("/api/v1/blobs", payload, "application/octet-stream");
  checks.expectEqual(uploaded.status, 201, "a blob can be uploaded");
  const auto hash = parse(uploaded).value("hash", std::string{});
  checks.expectEqual(static_cast<long long>(hash.size()), 64,
                     "the blob is addressed by its SHA-256");

  const auto downloaded = client.get("/api/v1/blobs/" + hash);
  checks.expectEqual(downloaded.status, 200, "a blob can be downloaded");
  checks.expect(downloaded.body == payload, "the blob content round-trips");
  checks.expectEqual(downloaded.header("Content-Type"),
                     "application/octet-stream", "blobs are opaque bytes");
  checks.expectEqual(downloaded.header("X-Content-Type-Options"), "nosniff",
                     "blob downloads forbid sniffing");
  checks.expect(downloaded.header("Content-Disposition").find("attachment") !=
                    std::string::npos,
                "blob downloads are attachments");

  const auto item = client.post(
      "/api/v1/items",
      Json{{"item", Json{{"groupId", groupId},
                         {"itemTypeId", typeId},
                         {"title", "Report"},
                         {"fieldValues",
                          Json{{std::to_string(fieldId), hash}}}}}}
          .dump());
  checks.expectEqual(item.status, 201, "a blob hash can be stored in an item");
  checks.expectEqual(
      client
          .post("/api/v1/items",
                Json{{"item", Json{{"groupId", groupId},
                                   {"itemTypeId", typeId},
                                   {"title", "Bad reference"},
                                   {"fieldValues",
                                    Json{{std::to_string(fieldId),
                                          "not-a-hash"}}}}}}
                    .dump())
          .status,
      400, "an invalid blob reference is rejected");
  checks.expectEqual(
      client.get("/api/v1/blobs/" + std::string(64, 'a')).status, 404,
      "an unknown blob is a not-found error");
  checks.expectEqual(client.get("/api/v1/blobs/NOTAHASH").status, 400,
                     "a malformed blob hash is rejected");
  checks.expectEqual(
      client.post("/api/v1/blobs", "", "application/octet-stream").status, 400,
      "an empty upload is rejected");
  const auto oversized = client.post(
      "/api/v1/blobs", std::string(harness.options().maxBlobBytes + 1, 'x'),
      "application/octet-stream");
  checks.expect(!oversized.transported || oversized.status == 413,
                "an oversized blob is rejected");
  // No temporary staging file may be left behind.
  int leftovers = 0;
  for (const auto &entry : std::filesystem::directory_iterator(
           std::filesystem::path(harness.databasePath()).parent_path()))
    if (entry.path().filename().string().rfind(".lexicon-http-", 0) == 0)
      ++leftovers;
  checks.expectEqual(leftovers, 0, "blob staging files are cleaned up");
}

void checkReview(Checks &checks) {
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId = parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  const int itemId = parse(client.post("/api/v1/items",
                                       Json{{"item", Json{{"groupId", groupId}, {"title", "Functor"}}}}.dump()))
                         .value("id", 0);
  client.post("/api/v1/items", Json{{"item", Json{{"groupId", groupId}, {"title", "Monad"}}}}.dump());

  const auto queue = client.get("/api/v1/review?limit=1");
  checks.expectEqual(queue.status, 200, "the review queue is available");
  checks.expectEqual(parse(queue).value("dueCount", 0), 2, "it counts every due item");
  checks.expectEqual(static_cast<long long>(parse(queue).at("items").size()), 1, "and returns up to the limit");
  checks.expect(parse(queue).at("items").at(0).at("reviewedAt").is_null(), "a new item was never reviewed");

  const auto path = "/api/v1/items/" + std::to_string(itemId) + "/review";
  checks.expectEqual(client.post(path, R"({"rating":"Perfect"})").status, 400, "an unknown rating is refused");
  const auto reviewed = client.post(path, R"({"rating":"Good"})");
  checks.expectEqual(reviewed.status, 200, "a review is recorded");
  const auto item = parse(reviewed).at("item");
  checks.expectEqual(item.value("understanding", std::string{}), "Recognized", "Good climbs a level");
  checks.expect(item.at("reviewDueAt").is_string(), "the next review date is reported");
  checks.expectEqual(parse(client.get("/api/v1/review")).value("dueCount", 0), 1, "one item is left to review");
  checks.expectEqual(client.post("/api/v1/items/999999/review", R"({"rating":"Good"})").status, 404,
                     "reviewing a missing item is a not-found error");
  checks.expectEqual(client.get("/api/v1/review?limit=0").status, 400, "the limit must be positive");
}

void checkExportImport(Checks &checks) {
  // The export of one server imports into another.
  ServerHarness source;
  Session session(source, checks);
  auto &client = session.client();
  const int groupId = parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  const int target = parse(client.post("/api/v1/items",
                                       Json{{"item", Json{{"groupId", groupId}, {"title", "Target"}}}}.dump()))
                         .value("id", 0);
  client.post("/api/v1/items",
              Json{{"item", Json{{"groupId", groupId}, {"title", "Source"}, {"content", "Links on."}}},
                   {"links", Json::array({Json{{"toItemId", target}, {"linkType", "Uses"}}})}}
                  .dump());

  const auto exported = client.get("/api/v1/export?blobs=true");
  checks.expectEqual(exported.status, 200, "the dictionary can be exported");
  checks.expect(exported.header("Content-Disposition").find("attachment; filename=\"lexicon-") == 0,
                "the export downloads as a dated file");
  const auto document = parse(exported);
  checks.expectEqual(document.value("format", std::string{}), "lexicon-export", "it is a Lexicon export");
  checks.expectEqual(static_cast<long long>(document.at("items").size()), 2, "both items are exported");
  checks.expectEqual(client.get("/api/v1/export?blobs=perhaps").status, 400,
                     "blobs accepts true or false only");

  const auto again = client.post("/api/v1/import", exported.body);
  checks.expectEqual(again.status, 200, "the export imports into the same server");
  checks.expectEqual(parse(again).at("report").value("itemsSkipped", 0), 2,
                     "and finds both items already there");

  ServerHarness destination;
  Session other(destination, checks);
  const auto imported = other.client().post("/api/v1/import", exported.body);
  checks.expectEqual(imported.status, 200, "the export imports into another server");
  const auto report = parse(imported).at("report");
  checks.expectEqual(report.value("itemsCreated", 0), 2, "both items are created there");
  checks.expectEqual(report.value("linksCreated", 0), 1, "with their link");
  const auto found = parse(other.client().post("/api/v1/items/query", Json{{"searchText", "Source"}}.dump()));
  checks.expectEqual(found.value("totalCount", 0), 1, "the imported item can be found");

  checks.expectEqual(other.client().post("/api/v1/import", exported.body, "text/plain").status, 415,
                     "an import must be JSON");
  const auto damaged = other.client().post("/api/v1/import", R"({"format":"lexicon-export","version":9})");
  checks.expectEqual(damaged.status, 400, "a newer export version is refused");
  checks.expect(parse(damaged).at("error").value("message", std::string{}).find("version 9") != std::string::npos,
                "and the refusal names the version");
}

void checkConflicts(Checks &checks) {
  // Two clients edit the same item: the second save is based on a revision
  // that no longer exists and must be refused instead of silently winning.
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId =
      parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  const auto create = [&](const std::string &title) {
    return parse(client.post("/api/v1/items",
                             Json{{"item", Json{{"groupId", groupId},
                                                {"title", title}}}}
                                 .dump()));
  };
  const auto createdA = create("Monoid");
  const int a = createdA.value("id", 0);
  const int first = createdA.at("item").value("revision", 0);
  checks.expect(first > 0, "a new item reports a revision");
  const auto path = "/api/v1/items/" + std::to_string(a);
  const auto save = [&](const std::string &content, int revision) {
    Json item{{"groupId", groupId}, {"title", "Monoid"}, {"content", content}};
    if (revision > 0)
      item["revision"] = revision;
    return client.put(path, Json{{"item", item}}.dump());
  };

  const auto phone = save("from the phone", first);
  checks.expectEqual(phone.status, 200, "a save based on the current revision works");
  const int second = parse(phone).at("item").value("revision", 0);
  checks.expect(second > first, "a save moves the revision on");

  const auto desktop = save("from the desktop", first);
  checks.expectEqual(desktop.status, 409, "a save based on an old revision is refused");
  checks.expectEqual(parse(desktop).at("error").value("code", std::string{}),
                     "conflict", "the refusal is a conflict");
  checks.expectEqual(
      parse(client.get(path)).at("item").value("content", std::string{}),
      "from the phone", "the refused save changed nothing");
  checks.expectEqual(save("overwritten", second).status, 200,
                     "saving over the newer revision works once it is known");
  checks.expectEqual(save("no revision", 0).status, 200,
                     "a save without a revision is not checked");

  // A link added from the other end changes this item too.
  const int current = parse(client.get(path)).at("item").value("revision", 0);
  const int b = create("Semigroup").value("id", 0);
  const auto linkB = [&](const Json &links) {
    return client.put("/api/v1/items/" + std::to_string(b),
                      Json{{"item", Json{{"groupId", groupId}, {"title", "Semigroup"}}},
                           {"links", links}}
                          .dump());
  };
  const auto linked = linkB(Json::array({Json{{"toItemId", a},
                                              {"linkType", "Related"},
                                              {"position", 0}}}));
  checks.expectEqual(linked.status, 200, "the other item saves a link");
  const int afterLink = parse(client.get(path)).at("item").value("revision", 0);
  checks.expect(afterLink > current, "a new backlink moves the revision on");
  checks.expectEqual(save("stale", current).status, 409,
                     "a save that does not know the new backlink is refused");

  // Resending an unchanged link leaves the other end alone.
  const auto links = parse(client.get("/api/v1/items/" + std::to_string(b) + "/links"))
                         .at("links");
  const int linkId = links.at(0).value("id", 0);
  checks.expectEqual(linkB(Json::array({Json{{"id", linkId},
                                             {"toItemId", a},
                                             {"linkType", "Related"},
                                             {"position", 0}}}))
                         .status,
                     200, "the other item saves again");
  checks.expectEqual(parse(client.get(path)).at("item").value("revision", 0),
                     afterLink, "an unchanged link does not move the revision");
  checks.expectEqual(client.remove("/api/v1/links/" + std::to_string(linkId)).status,
                     204, "the link can be deleted");
  checks.expect(parse(client.get(path)).at("item").value("revision", 0) > afterLink,
                "deleting a backlink moves the revision on");

  // Clearing a field's values changes every item that had one.
  const int typeId = parse(client.post("/api/v1/types",
                                       Json{{"name", "Structure"}, {"groupId", groupId}}.dump()))
                         .at("type").value("id", 0);
  const int fieldId =
      parse(client.post("/api/v1/types/" + std::to_string(typeId) + "/fields",
                        Json{{"name", "Order"}, {"dataType", "Integer"}}.dump()))
          .at("field").value("id", 0);
  const auto typed = client.put(
      path, Json{{"item", Json{{"groupId", groupId},
                               {"title", "Monoid"},
                               {"itemTypeId", typeId},
                               {"fieldValues", Json{{std::to_string(fieldId), "3"}}}}}}
                .dump());
  checks.expectEqual(typed.status, 200, "the item gets a typed value");
  const int typedRevision = parse(typed).at("item").value("revision", 0);
  checks.expectEqual(client.remove("/api/v1/fields/" + std::to_string(fieldId)).status,
                     204, "the field can be deleted");
  checks.expect(parse(client.get(path)).at("item").value("revision", 0) > typedRevision,
                "deleting a field with values moves the revision on");
}

void checkQuickAdd(Checks &checks) {
  // The web Quick Add flow: resolve the default group, then create a titled
  // item with nothing else set.
  ServerHarness harness;
  Session session(harness, checks);
  auto &client = session.client();
  const int groupId =
      parse(client.get("/api/v1/groups/default")).value("groupId", 0);
  const auto created = client.post(
      "/api/v1/items",
      Json{{"item", Json{{"groupId", groupId}, {"title", "Quickly added"}}}}
          .dump());
  checks.expectEqual(created.status, 201, "quick add creates an item");
  const auto stored = parse(created).at("item");
  checks.expectEqual(stored.value("groupName", std::string{}), "Default",
                     "quick add uses the Default group");
  checks.expectEqual(stored.value("status", std::string{}), "None",
                     "quick add leaves the status unset");
  checks.expect(stored.at("itemTypeId").is_null(),
                "quick add leaves the type unset");

  // Adding the same title again must say why it failed, not answer 500.
  const auto twin = client.post(
      "/api/v1/items",
      Json{{"item", Json{{"groupId", groupId}, {"title", "Quickly added"}}}}
          .dump());
  checks.expectEqual(twin.status, 400, "a twin title in the group is refused");
  checks.expect(twin.body.find("already exists in this group") != std::string::npos,
                "and the answer says the item already exists");
}
} // namespace

int main() {
  Checks checks;
  checkGroups(checks);
  checkTypesAndFields(checks);
  checkItems(checks);
  checkLinks(checks);
  checkSearchAndUsage(checks);
  checkBlobs(checks);
  checkQuickAdd(checks);
  checkConflicts(checks);
  checkExportImport(checks);
  checkReview(checks);
  return checks.summarize("server_rest");
}
