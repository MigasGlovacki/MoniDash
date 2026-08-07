#include "core/monidash_core.hpp"

#include <iostream>
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "usage: monidash_smoke <temporary-root>\n"; return 2; }
  monidash::SessionStore store(argv[1]); std::int64_t now=1000;
  monidash::MoniDashCore core(store,[&]{ return now++; },[]{ return std::string("smoke-session"); });
  core.start(); monidash::Death death; death.level_id="sample-level"; death.raw_x=42.0; death.practice=false; core.record_death(death); core.shutdown();
  std::cout << store.session_path() << "\n"; return 0;
}
