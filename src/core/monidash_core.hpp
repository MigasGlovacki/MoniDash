#pragma once

#include "core/observations.hpp"
#include "core/session_store.hpp"

#include <functional>

namespace monidash {
class MoniDashCore {
 public:
  using Clock = std::function<std::int64_t()>;
  using IdGenerator = std::function<std::string()>;
  MoniDashCore(SessionStore& store, Clock clock, IdGenerator ids) : store_(store), clock_(std::move(clock)), ids_(std::move(ids)) {}
  void start();
  void record_level_started(const std::string& level_id);
  void record_death(const Death& death);
  void shutdown();
  const std::string& session_id() const { return session_id_; }
  const std::filesystem::path& session_path() const { return store_.session_path(); }
 private: SessionStore& store_; Clock clock_; IdGenerator ids_; std::string session_id_; std::uint64_t event_id_{0}; bool active_{false};
};
}  // namespace monidash
