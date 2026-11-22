#pragma once
#include <ostream>

inline void print_help(std::ostream& out)
{
    out << R"(cabinet - game library and home window

Usage:
  cabinet [OPTIONS]

Options:
  --import PATH         Open a game-file import review. Requires --system ID.
  --system ID           Select a system and its default core for importing.
  --data-dir DIR        Library and game-data root. Default: ~/.local/share/cabinet/. Passed to cabinet-runner.
  --runner PATH         Runner executable. Default: cabinet-runner beside cabinet.
  --window-size WxH     Initial window size. Default: 1920x1080. Minimum: 560x600.
  --log-level LEVEL     stdout logging: off, error, warn (default), info, debug.
                        info prints the full runner command and exit status.
                        debug also prints library paths and runner readiness.
                        Only controls home logging, not the core's log level.
  --frames COUNT        Exit after COUNT positive display-loop iterations.
  --screenshot FILE.bmp Save the final window as a BMP. Requires --frames.
                        Overwrites an existing screenshot at that path.
  --help, -h            Print help and exit without opening a window.

Examples:
  cabinet
  cabinet --log-level info
  cabinet --system gb --import "/games/My Game.gbc"
  cabinet --data-dir ./profile --runner /opt/cabinet/cabinet-runner
  cabinet --frames 60 --screenshot library.bmp

Games run in the separate cabinet-runner executable. For standalone play:
  cabinet-runner "/games/My Game.gbc" --core "/cores/gambatte_libretro.so"
  cabinet-runner --help

Quote paths containing spaces. Relative paths use the working directory.
)";
}
