#pragma once

#include "core/schema.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace monidash {

enum class RecoveryStatus { Active, Corrupt, Missing };
struct RecoveryResult { std::filesystem::path path; RecoveryStatus status; };

class SessionStore {
 public:
  using FailureInjector = std::function<bool(const std::string&)>;
  explicit SessionStore(std::filesystem::path root, FailureInjector failure = {});
  void start(const std::string& id, std::int64_t timestamp_ms);
  void append(Event event);
  void finalize(std::int64_t timestamp_ms);
  const std::filesystem::path& session_path() const { return session_path_; }
  static std::vector<RecoveryResult> scan(const std::filesystem::path& root);
 private:
  std::filesystem::path root_; std::filesystem::path session_path_; FailureInjector failure_;
  std::uint64_t next_sequence_{1}; std::uint64_t last_event_id_{0}; std::int64_t last_timestamp_{0};
  std::int64_t started_at_{0}; bool started_{false}; bool finalized_{false}; bool end_appended_{false};
  void write_manifest(const Manifest& manifest, bool atomic);
};
}  // namespace monidash
