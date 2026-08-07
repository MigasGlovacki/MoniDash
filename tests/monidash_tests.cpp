#include "core/monidash_core.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace monidash;
namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F f) { try { f(); throw std::runtime_error("expected rejection"); } catch (const std::invalid_argument&) {} }
template<class F> void rejects_any(F f) { try { f(); throw std::runtime_error("expected rejection"); } catch (const std::exception&) {} }
std::filesystem::path temp(const char* name) { auto p = std::filesystem::temp_directory_path() / (std::string("monidash-") + name); std::filesystem::remove_all(p); std::filesystem::create_directories(p); return p; }
std::string read(const std::filesystem::path& p) { std::ifstream in(p); return {std::istreambuf_iterator<char>(in), {}}; }
void write(const std::filesystem::path& p, const std::string& content) { std::ofstream out(p); check(static_cast<bool>(out), "fixture file open failed"); out << content; out.flush(); check(static_cast<bool>(out), "fixture file write failed"); }
Event event(const std::string& session, std::uint64_t id, std::int64_t time) { Event e; e.session_id = session; e.event_id = id; e.timestamp_ms = time; e.kind = EventKind::LevelStarted; return e; }

void schema_tests() {
  Event e = event("s", 1, 10); e.sequence = 1; e.kind = EventKind::Death; DeathObservation d; d.level_id = "level"; d.raw_x = 0; d.mini = false; d.speed = 1.0; d.percentage_bin = 50; e.death = d;
  const auto text = e.serialize(); check(text.find("percentage_bin") == std::string::npos, "unmarked percentage bin inferred");
  e.death->percentage_bin_convention = "verified-level-length-v1"; auto round = Event::parse(e.serialize()); check(round.death->percentage_bin == 50 && round.death->percentage_bin_convention == "verified-level-length-v1", "percentage convention lost");
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":-1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\",\"x\":NaN}"); });
  rejects([&] { auto x = e; x.death->speed = std::numeric_limits<double>::infinity(); x.serialize(); });
  Manifest active{1, "s", 10, std::int64_t{11}, "active"}; rejects([&] { active.validate(); });
  Manifest complete{1, "s", 10, std::int64_t{9}, "complete"}; rejects([&] { complete.validate(); });
}

void storage_tests() {
  auto root = temp("storage"); SessionStore store(root); store.start("one", 1); rejects_any([&] { SessionStore duplicate(root); duplicate.start("one", 2); });
  store.append(event("one", 7, 2)); rejects([&] { store.append(event("other", 8, 3)); }); rejects([&] { store.append(event("one", 6, 3)); }); rejects([&] { store.append(event("one", 8, 1)); });
  const auto content = read(root / "one" / "events.jsonl"); check(content.find("\"event_id\":7") != std::string::npos, "event id not preserved");
  int failures = 0; auto fail_root = temp("retry"); SessionStore failing(fail_root, [&](const std::string& stage) { return stage == "manifest_rename" && failures++ == 0; }); failing.start("retry", 1); failing.append(event("retry", 1, 2)); rejects_any([&] { failing.finalize(3); }); failing.finalize(3); const auto log = read(fail_root / "retry" / "events.jsonl"); check(log.find("session_ended") != std::string::npos && log.find("session_ended", log.find("session_ended") + 1) == std::string::npos, "duplicate session end on retry");
  auto recovery = temp("recovery"); std::filesystem::create_directories(recovery / "active"); write(recovery / "active" / "manifest.json", Manifest{1, "active", 1, std::nullopt, "active"}.serialize()); auto active_event = event("active", 1, 1); active_event.sequence = 1; write(recovery / "active" / "events.jsonl", active_event.serialize() + '\n');
  std::filesystem::create_directories(recovery / "truncated"); write(recovery / "truncated" / "manifest.json", Manifest{1, "truncated", 1, std::nullopt, "active"}.serialize()); write(recovery / "truncated" / "events.jsonl", "{partial");
  std::filesystem::create_directories(recovery / "missing"); std::filesystem::create_directories(recovery / "badmanifest"); write(recovery / "badmanifest" / "manifest.json", "nope");
  const auto before = read(recovery / "truncated" / "events.jsonl"); auto found = SessionStore::scan(recovery); bool active = false, corrupt = false, missing = false; for (const auto& r : found) { active |= r.status == RecoveryStatus::Active; corrupt |= r.status == RecoveryStatus::Corrupt; missing |= r.status == RecoveryStatus::Missing; } check(active && corrupt && missing, "recovery statuses"); check(read(recovery / "truncated" / "events.jsonl") == before, "recovery altered events");
}

void core_tests() {
  auto root = temp("core"); SessionStore store(root); std::int64_t clock = 100; MoniDashCore core(store, [&] { return clock++; }, [] { return std::string("core-session"); }); core.start(); Death death; death.level_id = "level"; death.speed = 1.2; core.record_death(death); core.shutdown(); auto events = read(root / "core-session" / "events.jsonl"); check(events.find("session_ended") != std::string::npos && events.find("\"sequence\":4") == std::string::npos, "unexpected lifecycle ordering"); check(read(root / "core-session" / "manifest.json").find("complete") != std::string::npos, "core did not finalize");
}
}
int main() { try { schema_tests(); storage_tests(); core_tests(); std::cout << "passed " << checks << " checks\n"; return 0; } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
