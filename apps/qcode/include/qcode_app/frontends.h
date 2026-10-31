#pragma once

namespace qcode::app {

// One executable, three frontends. The dispatcher passes the original
// argv[0] through as the synthetic argv[0] for every mode, so server asset
// resolution and usage messages remain stable.
int run_tui(int argc, char* argv[]);
int run_server(int argc, char* argv[]);
int run_cli(int argc, char* argv[]);

}  // namespace qcode::app
