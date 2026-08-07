#include "core/monidash_core.hpp"

#include <stdexcept>
namespace monidash {
void MoniDashCore::start() { if(active_)throw std::logic_error("core already active");session_id_=ids_();if(session_id_.empty())throw std::invalid_argument("empty session id");store_.start(session_id_,clock_());Event e;e.session_id=session_id_;e.event_id=++event_id_;e.timestamp_ms=clock_();e.kind=EventKind::SessionStarted;store_.append(e);active_=true; }
void MoniDashCore::record_level_started(const std::string& level_id) { if(!active_)throw std::logic_error("core inactive");if(level_id.empty())throw std::invalid_argument("empty level id");Event e;e.session_id=session_id_;e.event_id=++event_id_;e.timestamp_ms=clock_();e.kind=EventKind::LevelStarted;e.level_id=level_id;store_.append(e); }
void MoniDashCore::record_death(const Death& death) { if(!active_)throw std::logic_error("core inactive");Event e;e.session_id=session_id_;e.event_id=++event_id_;e.timestamp_ms=clock_();e.kind=EventKind::Death;e.death=death;store_.append(e); }
void MoniDashCore::shutdown() { if(!active_)throw std::logic_error("core inactive");store_.finalize(clock_());active_=false; }
} // namespace monidash
