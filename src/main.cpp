#include <Geode/Geode.hpp>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

using namespace geode::prelude;

#include "core/monidash_core.hpp"

#include <chrono>
#include <cstdint>
#include <array>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
std::unique_ptr<monidash::SessionStore> store;
std::unique_ptr<monidash::MoniDashCore> core;
std::optional<std::string> active_level_id;
std::uint64_t sequence = 0;
bool exiting_handled = false;

std::int64_t unix_milliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}

void snapshot_death_tracker(const std::filesystem::path& session_path) {
  try {
    if (!active_level_id) {
      log::info("MoniDash omitted Death Tracker snapshot: no active level");
      return;
    }
    const std::string mod_id = "elohmrow.death_tracker";
    auto* death_tracker = Loader::get()->getLoadedMod(mod_id);
    if (!death_tracker) {
      log::info("MoniDash omitted Death Tracker snapshot: mod is not loaded");
      return;
    }

    const auto root = death_tracker->getSaveDir() / "levels";
    const std::array<std::string, 3> keys{*active_level_id, *active_level_id + "-local", *active_level_id + "-gauntlet"};
    std::vector<std::pair<std::string, std::filesystem::path>> candidates;
    for (const auto& key : keys) {
      const auto candidate = root / key;
      std::error_code error;
      if (std::filesystem::is_directory(candidate, error)) {
        candidates.emplace_back(key, candidate);
      } else if (error) {
        log::error("MoniDash omitted Death Tracker snapshot: cannot inspect {}: {}", candidate.string(), error.message());
        return;
      }
    }
    const auto directory_count = candidates.size();
    if (directory_count != 1) {
      log::info("MoniDash omitted Death Tracker snapshot: expected one candidate directory, found {}", directory_count);
      return;
    }

    const auto& [key, source] = candidates.front();
    const std::array<std::string, 2> names{"metadata", "general.dt"};
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> files;
    for (const auto& name : names) {
      const auto input = source / name;
      std::error_code error;
      if (std::filesystem::is_regular_file(input, error)) {
        files.emplace_back(input, name);
      } else if (error) {
        log::error("MoniDash omitted Death Tracker file {}: {}", input.string(), error.message());
      } else {
        log::info("MoniDash omitted missing Death Tracker file {}", input.string());
      }
    }
    if (files.empty()) {
      return;
    }

    const auto destination = session_path / "death_tracker_snapshot" / key;
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) {
      log::error("MoniDash omitted Death Tracker snapshot: cannot create {}: {}", destination.string(), error.message());
      return;
    }
    for (const auto& [input, name] : files) {
      error.clear();
      if (!std::filesystem::copy_file(input, destination / name, std::filesystem::copy_options::none, error)) {
        log::error("MoniDash omitted Death Tracker file {}: {}", input.string(), error ? error.message() : "copy was not performed");
      }
    }
  } catch (const std::exception& error) {
    log::error("MoniDash omitted Death Tracker snapshot: {}", error.what());
  } catch (...) {
    log::error("MoniDash omitted Death Tracker snapshot: unknown failure");
  }
}
}

class $modify(PlayLayer) {
  bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
    active_level_id.reset();
    if (!PlayLayer::init(level, useReplay, dontCreateObjects)) {
      return false;
    }
    if (!core) {
      log::error("MoniDash omitted level-start: core is inactive");
      return true;
    }
    if (!level) {
      log::error("MoniDash omitted level-start: level is null");
      return true;
    }
    try {
      const auto raw_level_id = level->m_levelID.value();
      if (raw_level_id <= 0) {
        log::error("MoniDash omitted level-start: invalid level ID {}", raw_level_id);
        return true;
      }
      const auto level_id = std::to_string(raw_level_id);
      core->record_level_started(level_id);
      active_level_id = level_id;
      log::info("MoniDash recorded level-start {}", level_id);
    } catch (const std::exception& error) {
      log::error("MoniDash omitted level-start: {}", error.what());
    } catch (...) {
      log::error("MoniDash omitted level-start: level ID conversion failed");
    }
    return true;
  }

};

class $modify(PlayerObject) {
  void playerDestroyed(bool noEffects) {
    PlayerObject::playerDestroyed(noEffects);
    if (!core || !active_level_id) {
      return;
    }
    try {
      core->record_death(monidash::Death{*active_level_id});
    } catch (const std::exception& error) {
      log::error("MoniDash omitted death: {}", error.what());
    } catch (...) {
      log::error("MoniDash omitted death: recording failed");
    }
  }
};

$on_mod(Loaded) {
  try {
    const auto root = Mod::get()->getSaveDir() / "telemetry" / "sessions";
    const auto recovered = monidash::SessionStore::recover(root);
    log::info("MoniDash recovered {} abandoned session(s)", recovered);

    store = std::make_unique<monidash::SessionStore>(root);
    core = std::make_unique<monidash::MoniDashCore>(
        *store, unix_milliseconds, [] {
          return "monidash-" + std::to_string(unix_milliseconds()) + "-" + std::to_string(++sequence);
        });
    core->start();
    log::info("MoniDash started local session {} at {}", core->session_id(), core->session_path().string());
  } catch (const std::exception& error) {
    core.reset();
    store.reset();
    log::error("MoniDash telemetry bootstrap failed; mod is inert: {}", error.what());
  }
}

$on_game(Exiting) {
  if (exiting_handled) {
    return;
  }
  exiting_handled = true;
  if (core) {
    snapshot_death_tracker(core->session_path());
    try {
      core->shutdown();
    } catch (const std::exception& error) {
      log::error("MoniDash session finalization failed: {}", error.what());
    } catch (...) {
      log::error("MoniDash session finalization failed: unknown failure");
    }
  }
  core.reset();
  store.reset();
}
