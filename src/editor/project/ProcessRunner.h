#pragma once

#include <functional>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// runProcessCapture — launch a program with an explicit argument vector and
// stream its combined stdout/stderr line by line.
//
// Takes an argv array rather than a command string so arguments never pass
// through a shell: a path containing spaces, quotes or ';' is data, not syntax.
// Returns the child's exit code, or -1 if it could not be started.
// ─────────────────────────────────────────────────────────────────────────────
int runProcessCapture(const std::vector<std::string>& argv,
                      const std::function<void(const std::string&)>& onLine);
