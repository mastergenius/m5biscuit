#pragma once

#include <string>
#include <vector>

struct StudyPackInfo {
  std::string path;
  std::string id;
  std::string title;
  std::string revision;
  std::string entryEpisodeId;
  std::string conceptsFile = "concepts.jsonl";
  std::string episodesFile = "episodes.jsonl";
  std::string rubricsFile = "rubrics.jsonl";
  int episodeCount = 0;
};

struct StudyEpisode {
  std::string id;
  std::string type;
  std::string title;
  std::string objective;
  std::string prompt;
  std::string reveal;
  std::string rubricId;
  std::vector<std::string> conceptIds;
  std::vector<std::string> actions;
  std::vector<std::string> nextIds;
};

class StudyPackReader {
 public:
  static constexpr const char* PACK_ROOT = "/biscuit/study/packs";

  bool scanPacks(std::vector<StudyPackInfo>& packs, int maxPacks = 32) const;
  bool loadManifest(const std::string& packPath, StudyPackInfo& info) const;
  bool loadEpisodeById(const StudyPackInfo& pack, const std::string& episodeId, StudyEpisode& out) const;
  bool loadEpisodeByIndex(const StudyPackInfo& pack, int index, StudyEpisode& out) const;
  bool loadRubricActions(const StudyPackInfo& pack, const std::string& rubricId,
                         std::vector<std::string>& actions) const;

 private:
  bool readEpisodeLine(const StudyPackInfo& pack, bool byId, const std::string& episodeId, int episodeIndex,
                       StudyEpisode& out) const;
};
