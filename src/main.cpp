#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

#include "core/monidash_core.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace {
std::unique_ptr<monidash::SessionStore> store;
std::unique_ptr<monidash::MoniDashCore> core;
std::optional<std::string> active_level_id;
std::uint64_t sequence = 0;

std::int64_t unix_milliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
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

  void destroyPlayer(PlayerObject* player, GameObject* object) {
    const auto player_was_alive = player ? !player->m_isDead : false;
    PlayLayer::destroyPlayer(player, object);
    if (!core || !active_level_id || !player || !player_was_alive) {
      return;
    }
    try {
      monidash::Death death;
      death.level_id = *active_level_id;
      core->record_death(death);
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
