#include "core/session_store.hpp"

#include <fstream>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace monidash { namespace {
void fail_if(const SessionStore::FailureInjector& f, const std::string& stage) {
  if (f && f(stage)) {
    throw std::runtime_error("injected failure: " + stage);
  }
}
std::string read_all(const std::filesystem::path& p) {
  std::ifstream in(p);
  if (!in) {
    throw std::runtime_error("cannot read " + p.string());
  }
  return {std::istreambuf_iterator<char>(in), {}};
}
void replace_file_atomically(const std::filesystem::path& temporary, const std::filesystem::path& target) {
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::runtime_error("manifest replacement failed");
  }
#else
  std::error_code ec;
  std::filesystem::rename(temporary, target, ec);
  if (ec) {
    std::filesystem::remove(temporary);
    throw std::runtime_error("manifest replacement failed: " + ec.message());
  }
#endif
}
}
SessionStore::SessionStore(std::filesystem::path root, FailureInjector failure) : root_(std::move(root)), failure_(std::move(failure)) {}
void SessionStore::start(const std::string& id, std::int64_t timestamp_ms) {
  if (started_) {
    throw std::logic_error("already started");
  }
  const std::filesystem::path id_path(id);
  const bool has_separator = id.find('/') != std::string::npos || id.find('\\') != std::string::npos;
  const bool is_single_filename = id_path.lexically_normal().filename() == id_path;
  if (id.empty() || id == "." || id == ".." || has_separator || id_path.is_absolute() || !is_single_filename || timestamp_ms < 0) {
    throw std::invalid_argument("invalid session start");
  }
  std::error_code ec;
  std::filesystem::create_directories(root_, ec);
  if (ec) {
    throw std::runtime_error("cannot create root: " + ec.message());
  }
  session_path_ = root_ / id;
  if (std::filesystem::exists(session_path_, ec)) {
    throw std::runtime_error("session directory already exists");
  }
  if (!std::filesystem::create_directory(session_path_, ec) || ec) {
    throw std::runtime_error("cannot create session directory");
  }
  std::ofstream events(session_path_ / "events.jsonl");
  if (!events) {
    throw std::runtime_error("cannot open events");
  }
  Manifest m;
  m.session_id = id;
  m.started_at_ms = timestamp_ms;
  m.status = "active";
  write_manifest(m, false);
  started_at_ = timestamp_ms;
  last_timestamp_ = timestamp_ms;
  started_ = true;
}
void SessionStore::append(Event event) {
  if (!started_ || finalized_) {
    throw std::logic_error("session is not active");
  }
  if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::invalid_argument("event sequence exhausted");
  }
  if (event.session_id != session_path_.filename().string()) {
    throw std::invalid_argument("event session mismatch");
  }
  event.sequence = next_sequence_;
  event.validate();
  if (event.event_id <= last_event_id_ || event.timestamp_ms < last_timestamp_) {
    throw std::invalid_argument("event order violation");
  }
  fail_if(failure_, "event_write");
  std::ofstream out(session_path_ / "events.jsonl", std::ios::app);
  if (!out || !(out << event.serialize() << '\n')) {
    throw std::runtime_error("event append failed");
  }
  out.flush();
  if (!out) {
    throw std::runtime_error("event flush failed");
  }
  ++next_sequence_;
  last_event_id_ = event.event_id;
  last_timestamp_ = event.timestamp_ms;
}
void SessionStore::write_manifest(const Manifest& m, bool atomic) {
  const auto target = session_path_ / "manifest.json";
  if (!atomic) {
    fail_if(failure_, "manifest_write");
    std::ofstream out(target, std::ios::trunc);
    if (!out || !(out << m.serialize() << '\n')) {
      throw std::runtime_error("manifest write failed");
    }
    out.flush();
    if (!out) {
      throw std::runtime_error("manifest flush failed");
    }
    return;
  }
  const auto temp = session_path_ / "manifest.json.tmp";
  fail_if(failure_, "manifest_write");
  {
    std::ofstream out(temp, std::ios::trunc);
    if (!out || !(out << m.serialize() << '\n')) {
      throw std::runtime_error("manifest temp write failed");
    }
    out.flush();
    if (!out) {
      throw std::runtime_error("manifest temp flush failed");
    }
  }
  // filesystem::rename maps to replace-on-Linux rename(2); other platforms need validation before relying on this.
  fail_if(failure_, "manifest_rename");
  replace_file_atomically(temp, target);
}
void SessionStore::finalize(std::int64_t timestamp_ms) {
  if (!started_ || finalized_) {
    throw std::logic_error("session cannot finalize");
  }
  if (timestamp_ms < last_timestamp_) {
    throw std::invalid_argument("non-monotonic finalization time");
  }
  if (!end_appended_ && last_event_id_ == std::numeric_limits<std::uint64_t>::max()) {
    throw std::invalid_argument("event id exhausted");
  }
  if (!end_appended_) {
    Event end;
    end.session_id = session_path_.filename().string();
    end.event_id = last_event_id_ + 1;
    end.timestamp_ms = timestamp_ms;
    end.kind = EventKind::SessionEnded;
    append(end);
    end_appended_ = true;
  }
  Manifest m;
  m.session_id = session_path_.filename().string();
  m.started_at_ms = started_at_;
  m.ended_at_ms = last_timestamp_;
  m.status = "complete";
  write_manifest(m, true);
  finalized_ = true;
}
std::vector<RecoveryResult> SessionStore::scan(const std::filesystem::path& root) {
  std::vector<RecoveryResult> result;
  std::error_code ec;
  if (!std::filesystem::is_directory(root, ec)) {
    return result;
  }
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (!entry.is_directory()) {
      continue;
    }
    const auto dir = entry.path();
    const auto manifest_path = dir / "manifest.json";
    const auto events_path = dir / "events.jsonl";
    if (!std::filesystem::is_regular_file(manifest_path, ec) || !std::filesystem::is_regular_file(events_path, ec)) {
      result.push_back({dir, RecoveryStatus::Missing});
      continue;
    }
    try {
      const Manifest manifest = Manifest::parse(read_all(manifest_path));
      if (manifest.session_id != dir.filename().string()) {
        throw std::invalid_argument("manifest session mismatch");
      }
      if (manifest.status != "active") {
        continue;
      }
      std::ifstream in(events_path); std::string line; std::uint64_t sequence = 0, event_id = 0; std::int64_t timestamp = manifest.started_at_ms; bool saw_started = false, saw_ended = false;
      while (std::getline(in, line)) {
        if (line.empty()) {
          throw std::invalid_argument("empty JSONL line");
        }
        const Event event = Event::parse(line);
        if (event.session_id != manifest.session_id || event.sequence != ++sequence || event.event_id <= event_id || event.timestamp_ms < timestamp || (!saw_started && event.kind != EventKind::SessionStarted) || (saw_ended && event.kind != EventKind::SessionEnded) || (event.kind == EventKind::SessionStarted && saw_started) || (event.kind == EventKind::SessionEnded && saw_ended)) {
          throw std::invalid_argument("event order/session violation");
        }
        saw_started = saw_started || event.kind == EventKind::SessionStarted;
        saw_ended = saw_ended || event.kind == EventKind::SessionEnded;
        event_id = event.event_id;
        timestamp = event.timestamp_ms;
      }
      if (!in.eof()) {
        throw std::invalid_argument("invalid event log");
      }
      if (!saw_started) {
        throw std::invalid_argument("missing session start");
      }
      result.push_back({dir, RecoveryStatus::Active});
    } catch (...) {
      result.push_back({dir, RecoveryStatus::Corrupt});
    }
  }
  return result;
}
std::size_t SessionStore::recover(const std::filesystem::path& root) {
  std::size_t recovered = 0;
  for (const auto& result : scan(root)) {
    if (result.status != RecoveryStatus::Active) {
      continue;
    }
    const auto manifest_path = result.path / "manifest.json";
    Manifest manifest = Manifest::parse(read_all(manifest_path));
    if (manifest.status != "active" || manifest.session_id != result.path.filename().string()) {
      continue;
    }
    std::ifstream events(result.path / "events.jsonl");
    std::string line;
    std::optional<std::int64_t> ended_at_ms;
    while (std::getline(events, line)) {
      const Event event = Event::parse(line);
      if (event.kind == EventKind::SessionEnded) ended_at_ms = event.timestamp_ms;
    }
    if (!events.eof()) {
      throw std::runtime_error("recovery event read failed");
    }
    manifest.status = ended_at_ms ? "complete" : "interrupted";
    manifest.ended_at_ms = ended_at_ms;
    const auto temporary = result.path / "manifest.json.tmp";
    {
      std::ofstream out(temporary, std::ios::trunc);
      if (!out || !(out << manifest.serialize() << '\n')) {
        throw std::runtime_error("recovery manifest write failed");
      }
      out.flush();
      if (!out) {
        throw std::runtime_error("recovery manifest flush failed");
      }
    }
    replace_file_atomically(temporary, manifest_path);
    ++recovered;
  }
  return recovered;
}
} // namespace monidash
