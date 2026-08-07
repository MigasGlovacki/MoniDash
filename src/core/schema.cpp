#include "core/schema.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <variant>
#include <vector>

namespace monidash { namespace {
struct JsonNumber {
  std::string token;
};

struct Json {
  using Object = std::map<std::string, Json>;
  using Array = std::vector<Json>;
  std::variant<std::nullptr_t, bool, JsonNumber, std::string, Array, Object> value;
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
  static void append_utf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7f) {
      out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7ff) {
      out += static_cast<char>(0xc0 | (codepoint >> 6));
      out += static_cast<char>(0x80 | (codepoint & 0x3f));
    } else if (codepoint <= 0xffff) {
      out += static_cast<char>(0xe0 | (codepoint >> 12));
      out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (codepoint & 0x3f));
    } else {
      out += static_cast<char>(0xf0 | (codepoint >> 18));
      out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f));
      out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f));
      out += static_cast<char>(0x80 | (codepoint & 0x3f));
    }
  }
  std::uint32_t unicode_escape() {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      if (p_ >= s_.size()) bad();
      const char c = s_[p_++];
      value <<= 4;
      if (c >= '0' && c <= '9') value += static_cast<std::uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') value += static_cast<std::uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') value += static_cast<std::uint32_t>(c - 'A' + 10);
      else bad();
    }
    return value;
  }
  std::string string() {
    ws();
    if (p_ >= s_.size() || s_[p_++] != '"') bad();
    std::string out;
    while (p_ < s_.size()) {
      const char c = s_[p_++];
      if (c == '"') return out;
      if (c == '\\') {
      if (p_ >= s_.size()) bad();
      char e = s_[p_++];
      if (e == '"' || e == '\\' || e == '/') out += e;
      else if (e == 'b') out += '\b';
      else if (e == 'f') out += '\f';
      else if (e == 'n') out += '\n';
      else if (e == 'r') out += '\r';
      else if (e == 't') out += '\t';
      else if (e == 'u') {
        const auto first = unicode_escape();
        if (first >= 0xd800 && first <= 0xdbff) {
          if (p_ + 5 > s_.size() || s_[p_] != '\\' || s_[p_ + 1] != 'u') bad();
          p_ += 2;
          const auto second = unicode_escape();
          if (second < 0xdc00 || second > 0xdfff) bad();
          append_utf8(out, 0x10000 + ((first - 0xd800) << 10) + (second - 0xdc00));
        } else if (first >= 0xdc00 && first <= 0xdfff) {
          bad();
        } else {
          append_utf8(out, first);
        }
      } else bad();
      } else {
        if (static_cast<unsigned char>(c) < 0x20) bad();
        out += c;
      }
    }
    bad();
  }
  Json number() {
    const auto start = p_;
    if (s_[p_] == '-') ++p_;
    if (p_ >= s_.size()) bad();
    if (s_[p_] == '0') {
      ++p_;
      if (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') bad();
    } else {
      if (s_[p_] < '1' || s_[p_] > '9') bad();
      while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
    }
    if (p_ < s_.size() && s_[p_] == '.') {
      ++p_;
      const auto fraction = p_;
      while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
      if (p_ == fraction) bad();
    }
    if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E')) {
      ++p_;
      if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
      const auto exponent = p_;
      while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
      if (p_ == exponent) bad();
    }
    return {JsonNumber{s_.substr(start, p_ - start)}};
  }
  Json value() {
    ws(); if (p_ >= s_.size()) bad(); char c = s_[p_];
    if (c == '"') return {string()};
    if (c == '{') return object();
    if (c == '[') return array();
    if (s_.compare(p_, 4, "true") == 0) { p_ += 4; return {true}; }
    if (s_.compare(p_, 5, "false") == 0) { p_ += 5; return {false}; }
    if (s_.compare(p_, 4, "null") == 0) { p_ += 4; return {nullptr}; }
    if (c == '-' || (c >= '0' && c <= '9')) return number();
    bad();
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
double num(const Json& j) {
  const auto* p = std::get_if<JsonNumber>(&j.value);
  if (!p) throw std::invalid_argument("finite number expected");
  char* end = nullptr;
  const double value = std::strtod(p->token.c_str(), &end);
  if (end != p->token.c_str() + p->token.size() || !std::isfinite(value)) throw std::invalid_argument("finite number expected");
  return value;
}
bool canonical_integer_token(const std::string& token) {
  std::size_t first = token.front() == '-' ? 1 : 0;
  if (first == token.size()) return false;
  if (token[first] == '0' && token.size() - first > 1) return false;
  for (std::size_t i = first; i < token.size(); ++i) {
    if (token[i] < '0' || token[i] > '9') return false;
  }
  return true;
}
std::int64_t integer(const Json& j) {
  const auto* p = std::get_if<JsonNumber>(&j.value);
  if (!p || !canonical_integer_token(p->token)) throw std::invalid_argument("canonical integer expected");
  std::int64_t value = 0;
  const auto result = std::from_chars(p->token.data(), p->token.data() + p->token.size(), value);
  if (result.ec != std::errc{} || result.ptr != p->token.data() + p->token.size()) throw std::invalid_argument("integer out of range");
  return value;
}
int small_integer(const Json& j) {
  const auto n = integer(j); if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max()) throw std::invalid_argument("small integer expected"); return static_cast<int>(n);
}
std::uint64_t unsigned_integer(const Json& j) {
  const auto* p = std::get_if<JsonNumber>(&j.value);
  if (!p || !canonical_integer_token(p->token) || p->token.front() == '-') throw std::invalid_argument("canonical unsigned integer expected");
  std::uint64_t value = 0;
  const auto result = std::from_chars(p->token.data(), p->token.data() + p->token.size(), value);
  if (result.ec != std::errc{} || result.ptr != p->token.data() + p->token.size()) throw std::invalid_argument("unsigned integer out of range");
  return value;
}
bool boolean(const Json& j) { auto* p = std::get_if<bool>(&j.value); if (!p) throw std::invalid_argument("boolean expected"); return *p; }
std::string quote(const std::string& s) {
  std::ostringstream o;
  o << '"';
  for (const char c : s) {
    switch (c) {
      case '"': o << "\\\""; break;
      case '\\': o << "\\\\"; break;
      case '\b': o << "\\b"; break;
      case '\f': o << "\\f"; break;
      case '\n': o << "\\n"; break;
      case '\r': o << "\\r"; break;
      case '\t': o << "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          o << "\\u00" << "0123456789abcdef"[(static_cast<unsigned char>(c) >> 4) & 0xf]
            << "0123456789abcdef"[static_cast<unsigned char>(c) & 0xf];
        } else {
          o << c;
        }
    }
  }
  return o.str() + '"';
}
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
  if (schema_version != kSchemaVersion || session_id.empty() || event_id == 0 || sequence == 0 || timestamp_ms < 0) {
    throw std::invalid_argument("invalid event");
  }
  if (kind == EventKind::Death) {
    if (!death || death->level_id.empty()) throw std::invalid_argument("death requires level_id");
    if (death->percentage_bin.has_value() != death->percentage_bin_convention.has_value()) {
      throw std::invalid_argument("percentage bin requires convention");
    }
    if (death->percentage_bin && (*death->percentage_bin < 0 || *death->percentage_bin > 100)) {
      throw std::invalid_argument("percentage_bin out of range");
    }
    if (death->percentage_bin_convention && death->percentage_bin_convention->empty()) {
      throw std::invalid_argument("empty percentage convention");
    }
    if (death->raw_x && !std::isfinite(*death->raw_x)) throw std::invalid_argument("invalid raw_x");
    if (death->speed && !std::isfinite(*death->speed)) throw std::invalid_argument("invalid speed");
  } else if (death) {
    throw std::invalid_argument("non-death has death fields");
  }
}
std::string Event::serialize() const { validate(); std::ostringstream o; bool f = true; o << '{'; put(o, "schema_version", schema_version, f); put(o, "session_id", session_id, f); put(o, "event_id", event_id, f); put(o, "sequence", sequence, f); put(o, "timestamp_ms", timestamp_ms, f); const std::string k = kind == EventKind::SessionStarted ? "session_started" : kind == EventKind::SessionEnded ? "session_ended" : kind == EventKind::LevelStarted ? "level_started" : "death"; put(o, "kind", k, f);
  if (death) { put(o, "level_id", death->level_id, f); if (death->attempt) put(o, "attempt", *death->attempt, f); if (death->raw_x) put(o, "raw_x", *death->raw_x, f); if (death->percentage_bin && death->percentage_bin_convention) { put(o, "percentage_bin", *death->percentage_bin, f); put(o, "percentage_bin_convention", *death->percentage_bin_convention, f); } if (death->gamemode) put(o, "gamemode", *death->gamemode, f); if (death->mini) put(o, "mini", *death->mini, f); if (death->reverse_gravity) put(o, "reverse_gravity", *death->reverse_gravity, f); if (death->speed) put(o, "speed", *death->speed, f); if (death->practice) put(o, "practice", *death->practice, f); if (death->dual) put(o, "dual", *death->dual, f); if (death->recent_input) put(o, "recent_input", *death->recent_input, f); } return o.str() + '}'; }
Event Event::parse(const std::string& j) { return parse_event(Parser(j).parse()); }
void Manifest::validate() const { if (schema_version != kSchemaVersion || session_id.empty() || started_at_ms < 0 || (status != "active" && status != "complete")) throw std::invalid_argument("invalid manifest"); if (status == "active" && ended_at_ms) throw std::invalid_argument("active manifest has end time"); if (status == "complete" && (!ended_at_ms || *ended_at_ms < started_at_ms)) throw std::invalid_argument("invalid complete manifest times"); if (ended_at_ms && *ended_at_ms < 0) throw std::invalid_argument("invalid manifest end time"); }
std::string Manifest::serialize() const { validate(); std::ostringstream o; bool f = true; o << '{'; put(o, "schema_version", schema_version, f); put(o, "session_id", session_id, f); put(o, "started_at_ms", started_at_ms, f); if (ended_at_ms) put(o, "ended_at_ms", *ended_at_ms, f); put(o, "status", status, f); return o.str() + '}'; }
Manifest Manifest::parse(const std::string& j) { const auto root = Parser(j).parse(); const auto& o = obj(root); Manifest m; m.schema_version = small_integer(req(o, "schema_version")); m.session_id = str(req(o, "session_id")); m.started_at_ms = integer(req(o, "started_at_ms")); auto i = o.find("ended_at_ms"); if (i != o.end()) m.ended_at_ms = integer(i->second); m.status = str(req(o, "status")); m.validate(); return m; }
}
