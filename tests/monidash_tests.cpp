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
Event event(const std::string& session, std::uint64_t id, std::int64_t time) { Event e; e.session_id = session; e.event_id = id; e.timestamp_ms = time; e.kind = EventKind::LevelStarted; e.level_id = "level"; return e; }

void schema_tests() {
  Event e = event("s", 1, 10); e.sequence = 1; e.kind = EventKind::Death; e.level_id.reset(); DeathObservation d; d.level_id = "level"; d.raw_x = 0; d.mini = false; d.speed = 1.0; d.percentage_bin = 50; e.death = d;
  auto exact_event_id = Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":9007199254740993,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\",\"level_id\":\"level\"}");
  check(exact_event_id.event_id == 9007199254740993ULL, "event_id precision was lost");
  const auto max_event = Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":18446744073709551615,\"sequence\":18446744073709551615,\"timestamp_ms\":9223372036854775807,\"kind\":\"level_started\",\"level_id\":\"level\"}");
  check(max_event.event_id == std::numeric_limits<std::uint64_t>::max() && max_event.sequence == std::numeric_limits<std::uint64_t>::max() && max_event.timestamp_ms == std::numeric_limits<std::int64_t>::max(), "integer boundary was not preserved");
  const auto max_manifest = Manifest::parse("{\"schema_version\":1,\"session_id\":\"s\",\"started_at_ms\":9223372036854775807,\"ended_at_ms\":9223372036854775807,\"status\":\"complete\"}");
  check(max_manifest.started_at_ms == std::numeric_limits<std::int64_t>::max() && max_manifest.ended_at_ms == std::numeric_limits<std::int64_t>::max(), "manifest timestamp boundary was not preserved");
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":18446744073709551616,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\",\"level_id\":\"level\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":9223372036854775808,\"kind\":\"level_started\",\"level_id\":\"level\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1e3,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\",\"level_id\":\"level\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1.0,\"kind\":\"level_started\",\"level_id\":\"level\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":-0,\"kind\":\"level_started\",\"level_id\":\"level\"}"); });
  const auto floating = Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"death\",\"level_id\":\"level\",\"raw_x\":1.25,\"speed\":1e2}");
  check(floating.death->raw_x == 1.25 && floating.death->speed == 100.0, "floating telemetry notation was rejected");
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"death\",\"level_id\":\"level\",\"raw_x\":1e-9999}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"death\",\"level_id\":\"level\",\"speed\":1e-9999}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"death\",\"level_id\":\"level\",\"attempt\":-0}"); });
  rejects([&] { e.validate(); });
  e.death->attempt = -1; rejects([&] { e.validate(); }); e.death->attempt.reset();
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"death\",\"level_id\":\"level\",\"percentage_bin\":50}"); });
  e.death->attempt = 0; e.death->percentage_bin_convention = "verified-level-length-v1"; e.death->reverse_gravity = false; e.death->practice = false; e.death->dual = false; auto round = Event::parse(e.serialize()); check(round.death->attempt == 0 && round.death->raw_x == 0 && round.death->mini == false && round.death->reverse_gravity == false && round.death->practice == false && round.death->dual == false && round.death->speed == 1.0, "zero/false telemetry was not preserved"); check(round.death->percentage_bin == 50 && round.death->percentage_bin_convention == "verified-level-length-v1", "percentage convention lost");
  auto escaped = event("line\n\"\\\b\f\r\t\001", 1, 10); escaped.sequence = 1; escaped.kind = EventKind::LevelStarted; auto escaped_round = Event::parse(escaped.serialize()); check(escaped_round.session_id == escaped.session_id, "control characters did not round-trip");
  auto unicode = Event::parse("{\"schema_version\":1,\"session_id\":\"\\u006c\\u00e9\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\",\"level_id\":\"level\"}"); check(unicode.session_id == "l\xc3\xa9", "unicode escape did not parse");
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":+1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":01,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1.,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":-1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\"}"); });
  rejects([&] { Event::parse("{\"schema_version\":1,\"session_id\":\"s\",\"event_id\":1,\"sequence\":1,\"timestamp_ms\":1,\"kind\":\"level_started\",\"x\":NaN}"); });
  rejects([&] { auto x = e; x.death->speed = std::numeric_limits<double>::infinity(); x.serialize(); });
  Manifest active{1, "s", 10, std::int64_t{11}, "active"}; rejects([&] { active.validate(); });
  Manifest complete{1, "s", 10, std::int64_t{9}, "complete"}; rejects([&] { complete.validate(); });
}

void storage_tests() {
  auto root = temp("storage"); SessionStore store(root); store.start("one", 1); rejects_any([&] { SessionStore duplicate(root); duplicate.start("one", 2); });
  auto unsafe = temp("unsafe"); const auto escaped = unsafe.parent_path() / "monidash-unsafe-escape"; std::filesystem::remove_all(escaped); rejects_any([&] { SessionStore s(unsafe); s.start("../monidash-unsafe-escape", 1); }); rejects_any([&] { SessionStore s(unsafe); s.start((unsafe.parent_path() / "absolute").string(), 1); }); rejects_any([&] { SessionStore s(unsafe); s.start("embedded/separator", 1); }); check(!std::filesystem::exists(escaped), "unsafe session escaped root");
  store.append(event("one", 7, 2)); rejects([&] { store.append(event("other", 8, 3)); }); rejects([&] { store.append(event("one", 6, 3)); }); rejects([&] { store.append(event("one", 8, 1)); });
  const auto content = read(root / "one" / "events.jsonl"); check(content.find("\"event_id\":7") != std::string::npos, "event id not preserved");
  store.append(event("one", std::numeric_limits<std::uint64_t>::max(), 3)); const auto before_max_finalize = read(root / "one" / "events.jsonl"); rejects([&] { store.finalize(4); }); check(read(root / "one" / "events.jsonl") == before_max_finalize, "max event id finalization changed event file");
  int failures = 0; auto fail_root = temp("retry"); SessionStore failing(fail_root, [&](const std::string& stage) { return stage == "manifest_rename" && failures++ == 0; }); failing.start("retry", 1); failing.append(event("retry", 1, 2)); rejects_any([&] { failing.finalize(3); }); failing.finalize(3); const auto log = read(fail_root / "retry" / "events.jsonl"); check(log.find("session_ended") != std::string::npos && log.find("session_ended", log.find("session_ended") + 1) == std::string::npos, "duplicate session end on retry");
  auto recovery = temp("recovery"); std::filesystem::create_directories(recovery / "active"); write(recovery / "active" / "manifest.json", Manifest{1, "active", 1, std::nullopt, "active"}.serialize()); write(recovery / "active" / "events.jsonl", "");
  std::filesystem::create_directories(recovery / "truncated"); write(recovery / "truncated" / "manifest.json", Manifest{1, "truncated", 1, std::nullopt, "active"}.serialize()); write(recovery / "truncated" / "events.jsonl", "{partial");
  std::filesystem::create_directories(recovery / "missing"); std::filesystem::create_directories(recovery / "badmanifest"); write(recovery / "badmanifest" / "manifest.json", "nope");
  const auto before = read(recovery / "truncated" / "events.jsonl"); auto found = SessionStore::scan(recovery); bool active = false, corrupt = false, missing = false; for (const auto& r : found) { active |= r.status == RecoveryStatus::Active; corrupt |= r.status == RecoveryStatus::Corrupt; missing |= r.status == RecoveryStatus::Missing; } check(active && corrupt && missing, "recovery statuses"); check(read(recovery / "truncated" / "events.jsonl") == before, "recovery altered events");
}

void recovery_tests() {
  auto root = temp("recovery-operation");
  std::filesystem::create_directories(root / "active");
  write(root / "active" / "manifest.json", Manifest{1, "active", 10, std::nullopt, "active"}.serialize());
  write(root / "active" / "events.jsonl", "");
  std::filesystem::create_directories(root / "complete");
  write(root / "complete" / "manifest.json", Manifest{1, "complete", 10, 20, "complete"}.serialize());
  write(root / "complete" / "events.jsonl", "complete bytes\n");
  std::filesystem::create_directories(root / "corrupt");
  write(root / "corrupt" / "manifest.json", "not json");
  write(root / "corrupt" / "events.jsonl", "corrupt bytes\n");
  std::filesystem::create_directories(root / "missing");
  write(root / "missing" / "manifest.json", Manifest{1, "missing", 10, std::nullopt, "active"}.serialize());

  const auto active_events = read(root / "active" / "events.jsonl");
  const auto complete_manifest = read(root / "complete" / "manifest.json");
  const auto corrupt_manifest = read(root / "corrupt" / "manifest.json");
  const auto missing_manifest = read(root / "missing" / "manifest.json");
  check(SessionStore::recover(root) == 1, "recovery did not change exactly one active session");
  const auto recovered = Manifest::parse(read(root / "active" / "manifest.json"));
  check(recovered.status == "interrupted" && !recovered.ended_at_ms, "active session was not interrupted without an end time");
  check(read(root / "active" / "events.jsonl") == active_events, "recovery altered active events");
  check(read(root / "complete" / "manifest.json") == complete_manifest, "recovery altered complete session");
  check(read(root / "corrupt" / "manifest.json") == corrupt_manifest, "recovery altered corrupt session");
  check(read(root / "missing" / "manifest.json") == missing_manifest, "recovery altered incomplete session");
  check(SessionStore::recover(root) == 0, "recovery was not idempotent");

  auto lifecycle = temp("recovery-lifecycle");
  auto make_event = [](const std::string& session, std::uint64_t id, std::uint64_t sequence, std::int64_t timestamp, EventKind kind) {
    Event e; e.session_id = session; e.event_id = id; e.sequence = sequence; e.timestamp_ms = timestamp; e.kind = kind; return e;
  };
  const auto started = make_event("terminal", 1, 1, 10, EventKind::SessionStarted).serialize() + "\n";
  const auto level = event("terminal", 2, 20); const auto level_line = [&] { auto e = level; e.sequence = 2; return e.serialize() + "\n"; }();
  const auto ended = make_event("terminal", 3, 3, 30, EventKind::SessionEnded).serialize() + "\n";
  const auto valid_events = started + level_line + ended;
  std::filesystem::create_directories(lifecycle / "terminal");
  write(lifecycle / "terminal" / "manifest.json", Manifest{1, "terminal", 10, std::nullopt, "active"}.serialize());
  write(lifecycle / "terminal" / "events.jsonl", valid_events);
  check(SessionStore::recover(lifecycle) == 1, "terminal active session was not recovered");
  const auto terminal_manifest = Manifest::parse(read(lifecycle / "terminal" / "manifest.json"));
  check(terminal_manifest.status == "complete" && terminal_manifest.ended_at_ms == 30, "terminal event did not publish complete manifest");
  check(read(lifecycle / "terminal" / "events.jsonl") == valid_events, "terminal recovery altered events");

  auto stream_for = [&](const std::string& id, const std::string& shape) {
    const auto start_line = make_event(id, 1, 1, 10, EventKind::SessionStarted).serialize() + "\n";
    auto level_event = event(id, shape == "no-start" ? 1 : 3, 20); level_event.sequence = shape == "no-start" ? 1 : 3;
    const auto level_for_id = level_event.serialize() + "\n";
    const auto end_line = make_event(id, 2, 2, 30, EventKind::SessionEnded).serialize() + "\n";
    const auto repeated_end_line = make_event(id, 4, 4, 40, EventKind::SessionEnded).serialize() + "\n";
    if (shape == "no-start") return level_for_id;
    if (shape == "ended-early" || shape == "after-end") return start_line + end_line + level_for_id;
    return start_line + end_line + repeated_end_line;
  };
  const std::vector<std::pair<std::string, std::string>> invalid_streams = {
    {"no-start", stream_for("no-start", "no-start")},
    {"ended-early", stream_for("ended-early", "ended-early")},
    {"repeated-end", stream_for("repeated-end", "repeated-end")},
    {"after-end", stream_for("after-end", "after-end")},
  };
  for (const auto& [id, events] : invalid_streams) {
    std::filesystem::create_directories(lifecycle / id);
    write(lifecycle / id / "manifest.json", Manifest{1, id, 10, std::nullopt, "active"}.serialize());
    write(lifecycle / id / "events.jsonl", events);
    const auto manifest_before = read(lifecycle / id / "manifest.json");
    const auto events_before = read(lifecycle / id / "events.jsonl");
    bool corrupt = false;
    for (const auto& result : SessionStore::scan(lifecycle)) {
      if (result.path.filename() == id) corrupt = result.status == RecoveryStatus::Corrupt;
    }
    check(corrupt, "invalid lifecycle stream was accepted");
    check(SessionStore::recover(lifecycle) == 0, "corrupt lifecycle stream was recovered");
    check(read(lifecycle / id / "manifest.json") == manifest_before && read(lifecycle / id / "events.jsonl") == events_before, "corrupt lifecycle stream was mutated");
  }
}

void core_tests() {
  auto root = temp("core"); SessionStore store(root); std::int64_t clock = 100; MoniDashCore core(store, [&] { return clock++; }, [] { return std::string("core-session"); }); core.start(); core.record_level_started("level-123"); rejects([&] { core.record_level_started(""); }); Death death; death.level_id = "level"; core.record_death(death); core.shutdown(); auto events = read(root / "core-session" / "events.jsonl"); check(events.find("level-123") != std::string::npos, "level start was not persisted"); check(events.find("\"kind\":\"death\"") != std::string::npos && events.find("\"level_id\":\"level\"") != std::string::npos, "minimal death was not persisted"); check(events.find("\"attempt\"") == std::string::npos && events.find("\"raw_x\"") == std::string::npos && events.find("\"percentage_bin\"") == std::string::npos && events.find("\"gamemode\"") == std::string::npos && events.find("\"mini\"") == std::string::npos && events.find("\"reverse_gravity\"") == std::string::npos && events.find("\"speed\"") == std::string::npos && events.find("\"practice\"") == std::string::npos && events.find("\"dual\"") == std::string::npos && events.find("\"recent_input\"") == std::string::npos, "minimal death included optional telemetry"); check(events.find("session_ended") != std::string::npos && events.find("\"sequence\":4") != std::string::npos, "unexpected lifecycle ordering"); check(read(root / "core-session" / "manifest.json").find("complete") != std::string::npos, "core did not finalize");
}
}
int main() { try { schema_tests(); storage_tests(); recovery_tests(); core_tests(); std::cout << "passed " << checks << " checks\n"; return 0; } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; } }
