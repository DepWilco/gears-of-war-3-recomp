#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace gow3::setup {

// Loose (already extracted) DLC: a folder holding the files of one marketplace package, kept in the
// console / Xenia content layout <...>\4D5308AB\00000002\<package name>\. Xenia's content folder
// (content\0000000000000000\4D5308AB\00000002\<name>) and a USB / HDD dump both look like this.
// Next to it Xenia keeps <...>\4D5308AB\Headers\00000002\<name>.header (XCONTENT_AGGREGATE_DATA, then
// the license mask); when that is there, its display name and license mask are used.

// A folder (not a link) named like a package, directly inside 4D5308AB\00000002 (any case), holding
// at least one file. An interrupted copy (<name>.part-dir) is never one.
bool IsLooseDlcFolder(const std::filesystem::path& dir);

// <dir>\..\..\Headers\00000002\<name>.header
std::filesystem::path LooseDlcHeaderPath(const std::filesystem::path& dir);

struct LooseDlcInfo {
  std::string display_name;   // UTF-8: the Xenia header's, else the folder name
  uint32_t license_mask = 0;  // the Xenia header's, else 0 (what Xenia uses for a folder with no header)
};
LooseDlcInfo ReadLooseDlcInfo(const std::filesystem::path& dir);

// The name the runtime installs the folder under: its UTF-8 name, cut to the 42 bytes an
// XCONTENT_DATA file name holds (at a character boundary), so the installed folder and its .header
// match the name the game is handed.
std::string LooseDlcContentName(const std::filesystem::path& dir);

// Total size of the regular files below `dir`; 0 when unreadable.
uint64_t FolderSize(const std::filesystem::path& dir);

// Copies the files and folders below `src` into `dst` (made first; files already there are
// overwritten). Links are not followed. False (and *error) on the first failure.
bool CopyFolder(const std::filesystem::path& src, const std::filesystem::path& dst, std::string* error);

}  // namespace gow3::setup
