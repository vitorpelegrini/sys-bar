#pragma once

#include "common.h"

#include <string>

// Runs a stand-in for a secondary taskbar ("classic" or "modern") for `seconds`, pumping messages.
int RunFakeTaskbar(const std::wstring& kind, int seconds);
