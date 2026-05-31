#include "study/StudyPackReader.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <new>

namespace {
constexpr const char* MODULE = "STPK";
constexpr size_t MANIFEST_BUF_SIZE = 4096;
constexpr size_t JSONL_LINE_SIZE = 2300;

std::string joinPath(const std::string& dir, const std::string& file) {
  if (file.empty()) {
    return dir;
  }
  if (!file.empty() && file[0] == '/') {
    return file;
  }
  return dir + "/" + file;
}

std::string fileNameFromEntry(const char* name) {
  const std::string path = name ? name : "";
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool readLine(HalFile& file, char* line, size_t len, bool& overflow) {
  overflow = false;
  if (!file.available()) {
    return false;
  }

  size_t pos = 0;
  bool sawByte = false;
  while (file.available()) {
    const int value = file.read();
    if (value < 0) {
      break;
    }
    sawByte = true;
    const char c = static_cast<char>(value);
    if (c == '\n') {
      break;
    }
    if (c == '\r') {
      continue;
    }
    if (pos + 1 < len) {
      line[pos++] = c;
    } else {
      overflow = true;
    }
  }
  line[pos] = '\0';
  return sawByte;
}

void copyString(std::string& dest, JsonVariantConst value) {
  const char* text = value | "";
  dest = text ? text : "";
}

void copyStringArray(std::vector<std::string>& dest, JsonVariantConst value) {
  dest.clear();
  JsonArrayConst arr = value.as<JsonArrayConst>();
  for (JsonVariantConst item : arr) {
    const char* text = item | "";
    if (text && *text) {
      dest.emplace_back(text);
    }
  }
}

void copyChoices(std::vector<StudyChoice>& dest, JsonVariantConst value) {
  dest.clear();
  JsonArrayConst arr = value.as<JsonArrayConst>();
  for (JsonObjectConst item : arr) {
    StudyChoice choice;
    copyString(choice.id, item["id"]);
    copyString(choice.label, item["label"]);
    if (!choice.id.empty()) {
      if (choice.label.empty()) {
        choice.label = choice.id;
      }
      dest.push_back(choice);
    }
  }
}

bool containsActionId(const std::vector<std::string>& actionIds, const char* id) {
  if (!id || !*id) {
    return false;
  }
  if (actionIds.empty()) {
    return true;
  }
  return std::find(actionIds.begin(), actionIds.end(), id) != actionIds.end();
}

void copyEpisode(JsonObjectConst obj, StudyEpisode& out) {
  out = StudyEpisode{};
  copyString(out.id, obj["id"]);
  copyString(out.type, obj["type"]);
  copyString(out.title, obj["title"]);
  copyString(out.objective, obj["objective"]);
  copyString(out.prompt, obj["prompt"]);
  copyString(out.reveal, obj["reveal"]);
  copyString(out.rubricId, obj["rubric_id"]);
  copyString(out.responseMode, obj["response"]["mode"]);
  copyStringArray(out.conceptIds, obj["concept_ids"]);
  copyStringArray(out.actionIds, obj["response"]["actions"]);
  copyChoices(out.choices, obj["response"]["choices"]);
  copyStringArray(out.nextIds, obj["next"]);
}
}  // namespace

bool StudyPackReader::scanPacks(std::vector<StudyPackInfo>& packs, const int maxPacks) const {
  packs.clear();
  if (!Storage.exists(PACK_ROOT)) {
    Storage.ensureDirectoryExists(PACK_ROOT);
    return true;
  }

  HalFile root = Storage.open(PACK_ROOT);
  if (!root) {
    LOG_ERR(MODULE, "Could not open pack root: %s", PACK_ROOT);
    return false;
  }

  HalFile entry;
  while ((entry = root.openNextFile())) {
    if (!entry.isDirectory()) {
      entry.close();
      continue;
    }

    char nameBuf[96] = {};
    const size_t nameLen = entry.getName(nameBuf, sizeof(nameBuf));
    entry.close();

    if (nameLen == 0 || nameLen >= sizeof(nameBuf) - 1) {
      LOG_ERR(MODULE, "Skipping StudyPack entry with invalid name");
      continue;
    }

    const std::string name = fileNameFromEntry(nameBuf);
    if (name.empty() || name[0] == '.') {
      continue;
    }

    StudyPackInfo info;
    const std::string path = nameBuf[0] == '/' ? std::string(nameBuf) : (std::string(PACK_ROOT) + "/" + name);
    if (loadManifest(path, info)) {
      packs.push_back(info);
      if (static_cast<int>(packs.size()) >= maxPacks) {
        break;
      }
    }
  }
  root.close();

  std::sort(packs.begin(), packs.end(), [](const StudyPackInfo& a, const StudyPackInfo& b) {
    const std::string& aLabel = a.title.empty() ? a.id : a.title;
    const std::string& bLabel = b.title.empty() ? b.id : b.title;
    return aLabel < bLabel;
  });
  return true;
}

bool StudyPackReader::loadManifest(const std::string& packPath, StudyPackInfo& info) const {
  const std::string path = joinPath(packPath, "manifest.json");
  std::unique_ptr<char[]> buf(new (std::nothrow) char[MANIFEST_BUF_SIZE]);
  if (!buf) {
    LOG_ERR(MODULE, "Could not allocate manifest buffer");
    return false;
  }
  const size_t read = Storage.readFileToBuffer(path.c_str(), buf.get(), MANIFEST_BUF_SIZE, MANIFEST_BUF_SIZE - 1);
  if (read == 0) {
    LOG_ERR(MODULE, "Could not read manifest: %s", path.c_str());
    return false;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    LOG_ERR(MODULE, "Manifest parse failed: %s", path.c_str());
    return false;
  }

  const char* schema = doc["schema"] | "";
  if (strcmp(schema, "biscuit.study-pack.v0") != 0) {
    LOG_ERR(MODULE, "Unsupported study pack schema: %s", schema);
    return false;
  }

  info = StudyPackInfo{};
  info.path = packPath;
  copyString(info.id, doc["pack_id"]);
  copyString(info.title, doc["title"]);
  copyString(info.revision, doc["revision"]);
  copyString(info.entryEpisodeId, doc["entry_episode_id"]);
  copyString(info.conceptsFile, doc["files"]["concepts"]);
  copyString(info.episodesFile, doc["files"]["episodes"]);
  copyString(info.rubricsFile, doc["files"]["rubrics"]);
  if (info.conceptsFile.empty()) info.conceptsFile = "concepts.jsonl";
  if (info.episodesFile.empty()) info.episodesFile = "episodes.jsonl";
  if (info.rubricsFile.empty()) info.rubricsFile = "rubrics.jsonl";
  info.episodeCount = doc["counts"]["episodes"] | 0;

  return !info.id.empty();
}

bool StudyPackReader::loadEpisodeById(const StudyPackInfo& pack, const std::string& episodeId,
                                      StudyEpisode& out) const {
  if (episodeId.empty()) {
    return false;
  }
  return readEpisodeLine(pack, true, episodeId, 0, out);
}

bool StudyPackReader::loadEpisodeByIndex(const StudyPackInfo& pack, const int index, StudyEpisode& out) const {
  if (index < 0) {
    return false;
  }
  return readEpisodeLine(pack, false, "", index, out);
}

bool StudyPackReader::readEpisodeLine(const StudyPackInfo& pack, const bool byId, const std::string& episodeId,
                                      const int episodeIndex, StudyEpisode& out) const {
  const std::string path = joinPath(pack.path, pack.episodesFile);
  HalFile file = Storage.open(path.c_str());
  if (!file) {
    LOG_ERR(MODULE, "Could not open episodes file: %s", path.c_str());
    return false;
  }

  std::unique_ptr<char[]> line(new (std::nothrow) char[JSONL_LINE_SIZE]);
  if (!line) {
    LOG_ERR(MODULE, "Could not allocate episode line buffer");
    file.close();
    return false;
  }
  int index = 0;
  bool overflow = false;
  while (readLine(file, line.get(), JSONL_LINE_SIZE, overflow)) {
    if (line[0] == '\0') {
      continue;
    }
    if (overflow) {
      LOG_ERR(MODULE, "Episode JSONL line too long: %s", path.c_str());
      continue;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, line.get());
    if (error) {
      LOG_ERR(MODULE, "Episode parse failed at %d", index);
      continue;
    }

    const char* id = doc["id"] | "";
    const bool match = byId ? episodeId == id : index == episodeIndex;
    if (match) {
      copyEpisode(doc.as<JsonObjectConst>(), out);
      file.close();
      return !out.id.empty();
    }
    index++;
  }

  file.close();
  return false;
}

bool StudyPackReader::loadRubricActions(const StudyPackInfo& pack, const std::string& rubricId,
                                        const std::vector<std::string>& actionIds,
                                        std::vector<StudyAction>& actions) const {
  actions.clear();
  if (rubricId.empty()) {
    return false;
  }

  const std::string path = joinPath(pack.path, pack.rubricsFile);
  HalFile file = Storage.open(path.c_str());
  if (!file) {
    return false;
  }

  std::unique_ptr<char[]> line(new (std::nothrow) char[JSONL_LINE_SIZE]);
  if (!line) {
    file.close();
    return false;
  }
  bool overflow = false;
  while (readLine(file, line.get(), JSONL_LINE_SIZE, overflow)) {
    if (overflow || line[0] == '\0') {
      continue;
    }
    JsonDocument doc;
    if (deserializeJson(doc, line.get())) {
      continue;
    }
    const char* id = doc["id"] | "";
    if (rubricId != id) {
      continue;
    }
    JsonArrayConst arr = doc["actions"].as<JsonArrayConst>();
    for (JsonObjectConst action : arr) {
      const char* actionId = action["id"] | "";
      if (!containsActionId(actionIds, actionId)) {
        continue;
      }
      StudyAction out;
      copyString(out.id, action["id"]);
      copyString(out.label, action["label"]);
      copyString(out.meaning, action["meaning"]);
      if (!out.id.empty()) {
        if (out.label.empty()) {
          out.label = out.id;
        }
        actions.push_back(out);
      }
    }
    file.close();
    return true;
  }

  file.close();
  return false;
}
