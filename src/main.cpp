#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

using namespace geode::prelude;

namespace monidash {

constexpr char const* kSchemaVersion = "2.0.0";
constexpr double kContextWindowSeconds = 5.0;

std::string escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(character);
                    escaped += hex.str();
                } else {
                    escaped += static_cast<char>(character);
                }
        }
    }
    return escaped;
}

std::string jsonString(std::string_view value) {
    return "\"" + escape(value) + "\"";
}

std::string unixMillis() {
    auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string playerMode(PlayerObject* player) {
    if (!player) return "unknown";
    if (player->m_isShip) return "ship";
    if (player->m_isBird) return "ufo";
    if (player->m_isDart) return "wave";
    if (player->m_isRobot) return "robot";
    if (player->m_isSpider) return "spider";
    if (player->m_isBall) return "ball";
    return "cube";
}

std::string objectClass(GameObject* object) {
    if (!object) return "unknown";
    // This is intentionally conservative: object IDs are retained as the source of truth.
    switch (object->m_objectID) {
        case 1: case 8: case 39: case 103: return "block";
        case 2: case 3: case 4: case 5: case 6: case 7: return "spike";
        default: return "unknown";
    }
}

struct Attempt {
    int number = 0;
    std::string id;
    double startX = 0.0;
    bool trainingSegment = false;
    bool finished = false;
};

class Recorder {
public:
    static Recorder& get() {
        static Recorder instance;
        return instance;
    }

    bool active() const { return m_playLayer != nullptr && m_output.is_open(); }

    bool owns(PlayerObject* player) const {
        return active() && player && (player == m_playLayer->m_player1 || player == m_playLayer->m_player2);
    }

    void start(PlayLayer* layer, GJGameLevel* level) {
        if (m_playLayer == layer) return;
        endSession();
        if (!Mod::get()->getSettingValue<bool>("capture-enabled")) return;

        m_playLayer = layer;
        m_startedAt = std::chrono::steady_clock::now();
        m_sessionId = "session-" + unixMillis();
        m_attemptCounter = 0;
        m_context.clear();
        m_telemetryDir = Mod::get()->getSaveDir() / "telemetry";
        std::filesystem::create_directories(m_telemetryDir);
        m_output.open(m_telemetryDir / (m_sessionId + ".jsonl"), std::ios::out | std::ios::app);
        if (!m_output.is_open()) {
            log::error("Could not open MoniDash telemetry output");
            m_playLayer = nullptr;
            return;
        }

        auto length = level ? level->m_levelLength : -1;
        auto* settings = layer->m_levelSettings;
        const bool mirrorMode = settings && settings->m_mirrorMode;
        write("session_started", "\"session_id\":" + jsonString(m_sessionId) +
            ",\"level\":{\"id\":" + std::to_string(level ? static_cast<int>(level->m_levelID) : 0) +
            ",\"name\":" + jsonString(level ? level->m_levelName : "") +
            ",\"creator\":" + jsonString(level ? level->m_creatorName : "") +
            ",\"length_category\":" + std::to_string(length) +
            ",\"extent_x\":" + number(layer->m_endXPosition) +
            ",\"local_or_saved\":" + std::string(level && level->m_localOrSaved ? "true" : "false") +
            ",\"platformer\":" + std::string(level && level->isPlatformer() ? "true" : "false") +
            ",\"mirror_mode\":" + std::string(mirrorMode ? "true" : "false") + "}");
    }

    void beginAttempt() {
        if (!active()) return;
        finishAttempt("reset", nullptr);
        ++m_attemptCounter;
        m_attempt = {};
        m_lastFatalPlayer = nullptr;
        m_lastFatalObject = nullptr;
        m_attempt.number = m_attemptCounter;
        m_attempt.id = m_sessionId + "-attempt-" + std::to_string(m_attemptCounter);
        auto* startPos = m_playLayer->m_startPosObject;
        m_attempt.trainingSegment = startPos != nullptr;
        m_attempt.startX = startPos ? startPos->getPositionX() : 0.0;
        write("attempt_started", "\"attempt_id\":" + jsonString(m_attempt.id) +
            ",\"attempt_number\":" + std::to_string(m_attempt.number) +
            ",\"training_segment\":" + std::string(m_attempt.trainingSegment ? "true" : "false") +
            ",\"start_x\":" + number(m_attempt.startX));
        if (m_attempt.trainingSegment) {
            auto levelID = m_playLayer->m_level ? static_cast<int>(m_playLayer->m_level->m_levelID) : 0;
            write("copy_level_link", "\"attempt_id\":" + jsonString(m_attempt.id) +
                ",\"copy_level_id\":" + std::to_string(levelID) +
                ",\"official_level_id\":null,\"link_status\":\"needs_confirmation\",\"start_x\":" + number(m_attempt.startX));
        }
    }

    void input(int button, bool player2, bool pressed) {
        if (!active() || m_attempt.id.empty() || m_attempt.finished) return;
        event("input", "\"button\":" + std::to_string(button) +
            ",\"player\":" + std::to_string(player2 ? 2 : 1) +
            ",\"pressed\":" + std::string(pressed ? "true" : "false"));
    }

    void interaction(PlayerObject* player, GameObject* object) {
        if (!owns(player) || m_attempt.id.empty() || m_attempt.finished) return;
        event("interaction", "\"player\":" + std::to_string(player == m_playLayer->m_player2 ? 2 : 1) +
            ",\"object\":" + objectJson(object));
    }

    void sample() {
        if (!active() || m_attempt.id.empty() || m_attempt.finished || !m_playLayer->m_player1) return;
        auto* p1 = m_playLayer->m_player1;
        auto signature = playerMode(p1) + ":" + (p1->m_isUpsideDown ? "up" : "down") + ":" +
            std::to_string(static_cast<int>(p1->m_yVelocity)) + ":" + std::to_string(p1->m_playerSpeed);
        if (signature != m_lastState) {
            m_lastState = signature;
            event("player_state", "\"player_state\":" + playerJson(p1, 1));
        }
    }

    void death(PlayerObject* player) {
        if (!owns(player) || m_attempt.id.empty() || m_attempt.finished) return;
        auto* object = (m_lastFatalPlayer == player) ? m_lastFatalObject : nullptr;
        std::ostringstream context;
        context << "[";
        bool first = true;
        auto now = elapsed();
        for (auto const& item : m_context) {
            if (now - item.time > kContextWindowSeconds) continue;
            if (!first) context << ",";
            context << item.json;
            first = false;
        }
        context << "]";
        write("death_context", "\"attempt_id\":" + jsonString(m_attempt.id) +
            ",\"player_snapshot\":" + playerJson(player, player == m_playLayer->m_player2 ? 2 : 1) +
            ",\"fatal_object\":" + objectJson(object) +
            ",\"cause\":{\"classification\":" + jsonString(objectClass(object)) +
            ",\"confidence\":" + jsonString(object ? (objectClass(object) == "unknown" ? "low" : "medium") : "none") + "}" +
            ",\"context_seconds\":5,\"preceding_events\":" + context.str());
        finishAttempt("death", player);
    }

    // O GD 2.2081 chama PlayLayer::destroyPlayer repetidamente durante o ciclo
    // de spawn/respawn (com o chão como objeto), então destroyPlayer não é um
    // gatilho confiável de morte. Aqui ele apenas registra o candidato a objeto
    // fatal; a morte real é sinalizada uma única vez por PlayerObject::playerDestroyed.
    void noteDestroyPlayer(PlayerObject* player, GameObject* object) {
        if (!owns(player)) return;
        m_lastFatalPlayer = player;
        m_lastFatalObject = object;
    }

    void complete() {
        if (!active() || m_attempt.id.empty()) return;
        auto endX = m_playLayer->m_player1 ? m_playLayer->m_player1->getPositionX() : 0.0;
        if (m_attempt.trainingSegment) {
            auto referenceId = m_sessionId + "-reference-" + std::to_string(m_attempt.number);
            auto active = activateReference(referenceId, endX);
            write("reference_run_saved", "\"reference_id\":" + jsonString(referenceId) +
                ",\"attempt_id\":" + jsonString(m_attempt.id) +
                ",\"link_status\":\"needs_confirmation\",\"segment\":{\"start_x\":" + number(m_attempt.startX) +
                ",\"end_x\":" + number(endX) + "},\"active_key\":" + jsonString(segmentKey()) +
                ",\"active\":" + std::string(active ? "true" : "false"));
        }
        finishAttempt("completed", m_playLayer->m_player1);
    }

    void endSession() {
        if (!m_output.is_open()) {
            m_playLayer = nullptr;
            return;
        }
        finishAttempt("abandoned", nullptr);
        write("session_ended", "\"session_id\":" + jsonString(m_sessionId));
        m_output.flush();
        m_output.close();
        m_playLayer = nullptr;
        m_attempt = {};
        m_context.clear();
    }

private:
    struct ContextEvent { double time; std::string json; };
    PlayLayer* m_playLayer = nullptr;
    std::ofstream m_output;
    std::deque<ContextEvent> m_context;
    Attempt m_attempt;
    PlayerObject* m_lastFatalPlayer = nullptr;
    GameObject* m_lastFatalObject = nullptr;
    std::string m_sessionId;
    std::string m_lastState;
    std::filesystem::path m_telemetryDir;
    int m_attemptCounter = 0;
    std::chrono::steady_clock::time_point m_startedAt = std::chrono::steady_clock::now();

    double elapsed() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_startedAt).count();
    }
    static std::string number(double value) {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(3) << value;
        return stream.str();
    }
    std::string playerJson(PlayerObject* player, int playerNumber) const {
        // Os bindings Geode 5.8.2 / GD 2.2081 não expõem m_isMini nem m_isMirror
        // no PlayerObject (verificado no build Windows); a escala (mini ≈ 0.6 vs
        // 1.0) e a direção observável (is_going_left, correlata de espelhamento)
        // são os fatos disponíveis. A classificação mini/espelhado fica para a
        // análise, separada dos dados observados.
        return "{\"player\":" + std::to_string(playerNumber) +
            ",\"x\":" + number(player->getPositionX()) + ",\"y\":" + number(player->getPositionY()) +
            ",\"y_velocity\":" + number(player->m_yVelocity) +
            ",\"speed\":" + number(player->m_playerSpeed) +
            ",\"mode\":" + jsonString(playerMode(player)) +
            ",\"gravity\":" + jsonString(player->m_isUpsideDown ? "up" : "down") +
            ",\"on_ground\":" + std::string(player->m_isOnGround ? "true" : "false") +
            ",\"scale\":" + number(player->getScale()) +
            ",\"is_going_left\":" + std::string(player->m_isGoingLeft ? "true" : "false") + "}";
    }
    static std::string objectJson(GameObject* object) {
        if (!object) return "null";
        return "{\"object_id\":" + std::to_string(object->m_objectID) +
            ",\"unique_id\":" + std::to_string(object->m_uniqueID) +
            ",\"object_type\":" + std::to_string(static_cast<int>(object->m_objectType)) +
            ",\"x\":" + number(object->getPositionX()) + ",\"y\":" + number(object->getPositionY()) + "}";
    }
    void write(std::string_view type, std::string body) {
        if (!m_output.is_open()) return;
        m_output << "{\"schema_version\":" << jsonString(kSchemaVersion)
                 << ",\"event_type\":" << jsonString(type)
                 << ",\"timestamp_ms\":" << jsonString(unixMillis())
                 << ",\"monotonic_seconds\":" << number(elapsed()) << "," << body << "}\n";
        m_output.flush();
    }
    void event(std::string_view type, std::string body) {
        std::string line = "{\"event_type\":" + jsonString(type) + ",\"at\":" + number(elapsed()) + "," + body + "}";
        m_context.push_back({ elapsed(), line });
        while (!m_context.empty() && elapsed() - m_context.front().time > kContextWindowSeconds) m_context.pop_front();
        write("gameplay_event", "\"attempt_id\":" + jsonString(m_attempt.id) + ",\"event\":" + line);
    }
    void finishAttempt(std::string_view outcome, PlayerObject* player) {
        if (!active() || m_attempt.id.empty() || m_attempt.finished) return;
        m_attempt.finished = true;
        auto endX = player ? player->getPositionX() : 0.0;
        write("attempt_ended", "\"attempt_id\":" + jsonString(m_attempt.id) +
            ",\"outcome\":" + jsonString(outcome) + ",\"start_x\":" + number(m_attempt.startX) +
            ",\"end_x\":" + number(endX) + ",\"training_segment\":" + std::string(m_attempt.trainingSegment ? "true" : "false"));
    }
    std::string segmentKey() const {
        auto levelID = m_playLayer && m_playLayer->m_level ? static_cast<int>(m_playLayer->m_level->m_levelID) : 0;
        return "level:" + std::to_string(levelID);
    }
    static std::optional<double> referenceStartX(std::filesystem::path const& path) {
        std::ifstream index(path);
        std::string json((std::istreambuf_iterator<char>(index)), std::istreambuf_iterator<char>());
        auto field = json.find("\"start_x\":");
        if (!index || field == std::string::npos) return std::nullopt;
        try {
            return std::stod(json.substr(field + 10));
        } catch (std::exception const&) {
            return std::nullopt;
        }
    }
    bool activateReference(std::string const& referenceId, double endX) const {
        auto referencesDir = m_telemetryDir / "active-references";
        std::filesystem::create_directories(referencesDir);
        auto levelID = m_playLayer && m_playLayer->m_level ? static_cast<int>(m_playLayer->m_level->m_levelID) : 0;
        auto filename = "level-" + std::to_string(levelID) + ".json";
        auto finalPath = referencesDir / filename;
        if (std::filesystem::exists(finalPath)) {
            auto previousStartX = referenceStartX(finalPath);
            if (!previousStartX) {
                log::warn("Could not read active reference index: {}", finalPath.string());
                return false;
            }
            if (m_attempt.startX <= *previousStartX) return false;
        }
        auto tempPath = finalPath;
        tempPath += ".tmp";
        std::ofstream index(tempPath, std::ios::out | std::ios::trunc);
        if (!index.is_open()) {
            log::warn("Could not update active reference index");
            return false;
        }
        index << "{\"schema_version\":" << jsonString(kSchemaVersion)
              << ",\"reference_id\":" << jsonString(referenceId)
              << ",\"session_id\":" << jsonString(m_sessionId)
              << ",\"attempt_id\":" << jsonString(m_attempt.id)
              << ",\"active_key\":" << jsonString(segmentKey())
              << ",\"start_x\":" << number(m_attempt.startX)
              << ",\"end_x\":" << number(endX) << "}";
        index.close();
        std::error_code error;
        std::filesystem::rename(tempPath, finalPath, error);
        if (error) {
            std::filesystem::remove(finalPath, error);
            std::filesystem::rename(tempPath, finalPath, error);
        }
        if (error) log::warn("Could not activate reference index: {}", error.message());
        return !error;
    }
};
} // namespace monidash

class $modify(MoniDashPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        monidash::Recorder::get().start(this, level);
        monidash::Recorder::get().beginAttempt();
        return true;
    }
    void onExit() {
        monidash::Recorder::get().endSession();
        PlayLayer::onExit();
    }
    void resetLevel() {
        PlayLayer::resetLevel();
        monidash::Recorder::get().beginAttempt();
    }
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        monidash::Recorder::get().sample();
    }
    void destroyPlayer(PlayerObject* player, GameObject* object) {
        monidash::Recorder::get().noteDestroyPlayer(player, object);
        PlayLayer::destroyPlayer(player, object);
    }
    void levelComplete() {
        monidash::Recorder::get().complete();
        PlayLayer::levelComplete();
    }
};

class $modify(MoniDashPlayerObject, PlayerObject) {
    void playerDestroyed(bool noEffects) {
        monidash::Recorder::get().death(this);
        PlayerObject::playerDestroyed(noEffects);
    }
};

class $modify(MoniDashBaseGameLayer, GJBaseGameLayer) {
    void handleButton(bool down, int button, bool isPlayer1) {
        monidash::Recorder::get().input(button, !isPlayer1, down);
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }
    void bumpPlayer(PlayerObject* player, EffectGameObject* object) {
        monidash::Recorder::get().interaction(player, object);
        GJBaseGameLayer::bumpPlayer(player, object);
    }
};
