/** @file script.h
 *  @brief The script player behind frizz-twin (cli.cpp), and what ui.cpp replays a bug
 *  report with: README.md's commands, played into the virtual CHOMPI from power-on.
 */
#pragma once
#include <cstdio>
#include <istream>
#include <string>
#include <vector>

namespace twin
{
/** Plays a script. leds (may be null) gets a line whenever the LEDs change, out (may be
 *  null) the master out, interleaved L R. Returns how many lines failed (an `expect` that
 *  didn't hold, an unknown command), each told on stderr */
int PlayScript(std::istream& src, FILE* leds, std::vector<float>* out);

/** Every LED now, as a line of the LED log: "pth RRGGBB x10 | smt RRGGBB x25" */
std::string LedLine();
} // namespace twin
