#pragma once
#include <string>
#include <vector>

namespace david {

// UI text lookup. Keys are the English strings themselves; lang/en.json is the built-in file (key = value)
// that a translation copies and edits. A missing key shows the English text.
const char* tr(const char* text);

// Loads lang/<code>.json from dir (the English file needs no loading); false keeps English
bool loadLanguage(const std::string& dir, const std::string& code, std::string& error);
// Language codes found in dir, "en" first
std::vector<std::string> languages(const std::string& dir);
// The language's own name, from the "_name" key of its file
std::string languageName(const std::string& dir, const std::string& code);

}
