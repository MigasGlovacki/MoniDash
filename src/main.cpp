#include <Geode/Geode.hpp>

using namespace geode::prelude;

#include "core/monidash_core.hpp"

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <string>

namespace {
std::unique_ptr<monidash::SessionStore> store;
std::unique_ptr<monidash::MoniDashCore> core;
std::uint64_t sequence = 0;

std::int64_t unix_milliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}
}

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
