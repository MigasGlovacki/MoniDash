#include "core/schema.hpp"

#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

namespace monidash { namespace {
struct Json {
  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;
  std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;
};

class Parser {
 public:
  explicit Parser(const std::string& s) : s_(s) {}
  Json parse() { Json v = value(); ws(); if (p_ != s_.size()) bad(); return v; }
 private:
  const std::string& s_; std::size_t p_ = 0;
  [[noreturn]] void bad() const { throw std::invalid_argument("invalid JSON"); }
  void ws() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\n' || s_[p_] == '\r' || s_[p_] == '\t')) ++p_; }
  void expect(char c) { ws(); if (p_ >= s_.size() || s_[p_++] != c) bad(); }
  std::string string() {
    ws(); if (p_ >= s_.size() || s_[p_++] != '"') bad(); std::string out;
    while (p_ < s_.size()) { char c = s_[p_++]; if (c == '"') return out; if (c == '\\') {
      if (p_ >= s_.size()) bad();
      char e = s_[p_++];
      if (e == '"' || e == '\\' || e == '/') out += e; else if (e == 'n') out += '\n';
      else if (e == 'r') out += '\r'; else if (e == 't') out += '\t'; else bad();
    } else out += c; }
    bad();
  }
  Json value() {
    ws(); if (p_ >= s_.size()) bad(); char c = s_[p_];
    if (c == '"') return {string()};
    if (c == '{') return object();
    if (c == '[') return array();
    if (s_.compare(p_, 4, "true") == 0) { p_ += 4; return {true}; }
    if (s_.compare(p_, 5, "false") == 0) { p_ += 5; return {false}; }
    if (s_.compare(p_, 4, "null") == 0) { p_ += 4; return {nullptr}; }
    char* end = nullptr; double n = std::strtod(s_.c_str() + p_, &end);
    if (end == s_.c_str() + p_ || !std::isfinite(n)) bad();
    p_ = static_cast<std::size_t>(end - s_.c_str()); return {n};
  }
  Json object() {
    Json::Object out; expect('{'); ws(); if (p_ < s_.size() && s_[p_] == '}') { ++p_; return {out}; }
    while (true) { auto k = string(); expect(':'); auto [it, inserted] = out.emplace(std::move(k), value()); if (!inserted) bad();
      ws(); if (p_ < s_.size() && s_[p_] == '}') { ++p_; return {out}; } expect(','); }
  }
  Json array() {
    Json::Array out; expect('['); ws(); if (p_ < s_.size() && s_[p_] == ']') { ++p_; return {out}; }
    while (true) { out.push_back(value()); ws(); if (p_ < s_.size() && s_[p_] == ']') { ++p_; return {out}; } expect(','); }
  }
};
const Json::Object& obj(const Json& j) { auto* p = std::get_if<Json::Object>(&j.value); if (!p) throw std::invalid_argument("JSON object expected"); return *p; }
const Json& req(const Json::Object& o, const char* k) { auto i = o.find(k); if (i == o.end()) throw std::invalid_argument(std::string("missing field: ") + k); return i->second; }
std::string str(const Json& j) { auto* p = std::get_if<std::string>(&j.value); if (!p) throw std::invalid_argument("string expected"); return *p; }
double num(const Json& j) { auto* p = std::get_if<double>(&j.value); if (!p || !std::isfinite(*p)) throw std::invalid_argument("finite number expected"); return *p; }
std::int64_t integer(const Json& j) {
  const double n = num(j); const double max = static_cast<double>(std::numeric_limits<std::int64_t>::max());
  if (n < static_cast<double>(std::numeric_limits<std::int64_t>::min()) || n > max || std::trunc(n) != n) throw std::invalid_argument("integer expected");
  return static_cast<std::int64_t>(n);
}
int small_integer(const Json& j) {
  const auto n = integer(j); if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) throw std::invalid_argument("small integer expected"); return static_cast<int>(n);
}
std::uint64_t unsigned_integer(const Json& j) {
  const double n = num(j); constexpr long double limit = 18446744073709551616.0L;
  if (n < 0 || static_cast<long double>(n) >= limit || std::trunc(n) != n) throw std::invalid_argument("nonnegative unsigned integer expected");
  return static_cast<std::uint64_t>(n);
}
bool boolean(const Json& j) { auto* p = std::get_if<bool>(&j.value); if (!p) throw std::invalid_argument("boolean expected"); return *p; }
std::string quote(const std::string& s) { std::ostringstream o; o << '"'; for (char c : s) { if (c == '"' || c == '\\') o << '\\'; if (c == '\n') o << "\\n"; else if (c == '\r') o << "\\r"; else if (c == '\t') o << "\\t"; else o << c; } return o.str() + '"'; }
void put(std::ostringstream& o, const char* k, const std::string& v, bool& f) { if (!f) o << ','; f = false; o << quote(k) << ':' << quote(v); }
void put(std::ostringstream& o, const char* k, bool v, bool& f) { if (!f) o << ','; f = false; o << quote(k) << ':' << (v ? "true" : "false"); }
template<class T> void put(std::ostringstream& o, const char* k, T v, bool& f) { if (!f) o << ','; f = false; o << quote(k) << ':' << v; }

Event parse_event(const Json& j) {
  const auto& o = obj(j); Event e; e.schema_version = small_integer(req(o, "schema_version")); e.session_id = str(req(o, "session_id"));
  e.event_id = unsigned_integer(req(o, "event_id")); e.sequence = unsigned_integer(req(o, "sequence")); e.timestamp_ms = integer(req(o, "timestamp_ms"));
  const auto kind = str(req(o, "kind")); if (kind == "session_started") e.kind = EventKind::SessionStarted; else if (kind == "session_ended") e.kind = EventKind::SessionEnded; else if (kind == "level_started") e.kind = EventKind::LevelStarted; else if (kind == "death") e.kind = EventKind::Death; else throw std::invalid_argument("unknown event kind");
  if (e.kind == EventKind::Death) { DeathObservation d; d.level_id = str(req(o, "level_id"));
    auto get = [&](const char* key, auto& target, auto fn) { auto i = o.find(key); if (i != o.end()) target = fn(i->second); };
    get("attempt", d.attempt, small_integer); get("raw_x", d.raw_x, num); get("percentage_bin", d.percentage_bin, small_integer); get("percentage_bin_convention", d.percentage_bin_convention, str); get("gamemode", d.gamemode, str); get("mini", d.mini, boolean); get("reverse_gravity", d.reverse_gravity, boolean); get("speed", d.speed, num); get("practice", d.practice, boolean); get("dual", d.dual, boolean); get("recent_input", d.recent_input, str); e.death = d;
  } e.validate(); return e;
}
} }

namespace monidash {
void Event::validate() const {
  if (schema_version != kSchemaVersion || session_id.empty() || event_id == 0 || sequence == 0 || timestamp_ms < 0) throw std::invalid_argument("invalid event");
  if (kind == EventKind::Death) { if (!death || death->level_id.empty()) throw std::invalid_argument("death requires level_id"); if (death->percentage_bin && (*death->percentage_bin < 0 || *death->percentage_bin > 100)) throw std::invalid_argument("percentage_bin out of range"); if (death->percentage_bin_convention && death->percentage_bin_convention->empty()) throw std::invalid_argument("empty percentage convention"); if (death->raw_x && !std::isfinite(*death->raw_x)) throw std::invalid_argument("invalid raw_x"); if (death->speed && !std::isfinite(*death->speed)) throw std::invalid_argument("invalid speed"); }
  else if (death) throw std::invalid_argument("non-death has death fields");
}
std::string Event::serialize() const { validate(); std::ostringstream o; bool f = true; o << '{'; put(o, "schema_version", schema_version, f); put(o, "session_id", session_id, f); put(o, "event_id", event_id, f); put(o, "sequence", sequence, f); put(o, "timestamp_ms", timestamp_ms, f); const std::string k = kind == EventKind::SessionStarted ? "session_started" : kind == EventKind::SessionEnded ? "session_ended" : kind == EventKind::LevelStarted ? "level_started" : "death"; put(o, "kind", k, f);
  if (death) { put(o, "level_id", death->level_id, f); if (death->attempt) put(o, "attempt", *death->attempt, f); if (death->raw_x) put(o, "raw_x", *death->raw_x, f); if (death->percentage_bin && death->percentage_bin_convention) { put(o, "percentage_bin", *death->percentage_bin, f); put(o, "percentage_bin_convention", *death->percentage_bin_convention, f); } if (death->gamemode) put(o, "gamemode", *death->gamemode, f); if (death->mini) put(o, "mini", *death->mini, f); if (death->reverse_gravity) put(o, "reverse_gravity", *death->reverse_gravity, f); if (death->speed) put(o, "speed", *death->speed, f); if (death->practice) put(o, "practice", *death->practice, f); if (death->dual) put(o, "dual", *death->dual, f); if (death->recent_input) put(o, "recent_input", *death->recent_input, f); } return o.str() + '}'; }
Event Event::parse(const std::string& j) { return parse_event(Parser(j).parse()); }
void Manifest::validate() const { if (schema_version != kSchemaVersion || session_id.empty() || started_at_ms < 0 || (status != "active" && status != "complete")) throw std::invalid_argument("invalid manifest"); if (status == "active" && ended_at_ms) throw std::invalid_argument("active manifest has end time"); if (status == "complete" && (!ended_at_ms || *ended_at_ms < started_at_ms)) throw std::invalid_argument("invalid complete manifest times"); if (ended_at_ms && *ended_at_ms < 0) throw std::invalid_argument("invalid manifest end time"); }
std::string Manifest::serialize() const { validate(); std::ostringstream o; bool f = true; o << '{'; put(o, "schema_version", schema_version, f); put(o, "session_id", session_id, f); put(o, "started_at_ms", started_at_ms, f); if (ended_at_ms) put(o, "ended_at_ms", *ended_at_ms, f); put(o, "status", status, f); return o.str() + '}'; }
Manifest Manifest::parse(const std::string& j) { const auto root = Parser(j).parse(); const auto& o = obj(root); Manifest m; m.schema_version = small_integer(req(o, "schema_version")); m.session_id = str(req(o, "session_id")); m.started_at_ms = integer(req(o, "started_at_ms")); auto i = o.find("ended_at_ms"); if (i != o.end()) m.ended_at_ms = integer(i->second); m.status = str(req(o, "status")); m.validate(); return m; }
}
