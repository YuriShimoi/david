#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace david::zip {

// A zip archive held in memory: stored or deflated entries, no zip64 or encryption
struct Entry {
  std::string name;
  std::vector<uint8_t> data;
};

// Reads every file entry (directories skipped); false with an error on a bad or unsupported archive
bool read(const std::vector<uint8_t>& bytes, std::vector<Entry>& out, std::string& error);
bool readFile(const std::string& path, std::vector<Entry>& out, std::string& error);

// Writes the entries; `stored` lists names kept uncompressed (OpenRaster's "mimetype" must be first and stored)
std::vector<uint8_t> write(const std::vector<Entry>& entries, const std::vector<std::string>& stored = {});
bool writeFile(const std::string& path, const std::vector<Entry>& entries, std::string& error, const std::vector<std::string>& stored = {});

const Entry* find(const std::vector<Entry>& entries, const std::string& name);

}
