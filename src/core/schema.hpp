#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace monidash {

constexpr int kSchemaVersion = 1;

enum class EventKind { SessionStarted, SessionEnded, LevelStarted, Death };

struct DeathObservation {
  std::string level_id;
  std::optional<int> attempt;
  std::optional<double> raw_x;
  std::optional<int> percentage_bin;
  std::optional<std::string> percentage_bin_convention;
  std::optional<std::string> gamemode;
  std::optional<bool> mini;
  std::optional<bool> reverse_gravity;
  std::optional<double> speed;
  std::optional<bool> practice;
  std::optional<bool> dual;
  std::optional<std::string> recent_input;
};

struct Event {
  int schema_version{kSchemaVersion};
  std::string session_id;
  std::uint64_t event_id{0};
  std::uint64_t sequence{0};
  std::int64_t timestamp_ms{0};
  EventKind kind{EventKind::SessionStarted};
  std::optional<std::string> level_id;
  std::optional<DeathObservation> death;

  std::string serialize() const;
  static Event parse(const std::string& json);
  void validate() const;
};

struct Manifest {
  int schema_version{kSchemaVersion};
  std::string session_id;
  std::int64_t started_at_ms{0};
  std::optional<std::int64_t> ended_at_ms;
  std::string status;

  std::string serialize() const;
  static Manifest parse(const std::string& json);
  void validate() const;
};

}  // namespace monidash
