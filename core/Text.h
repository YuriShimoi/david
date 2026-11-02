#pragma once

// Marks a UI string that is stored in English (command labels, tool names, History step names) and translated
// with tr() when shown; tools/gen-lang.py collects these together with tr("...") calls
#define N_(text) (text)
