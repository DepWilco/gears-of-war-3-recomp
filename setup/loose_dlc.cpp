#include "setup/loose_dlc.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <fstream>
#include <span>
#include <string_view>
#include <system_error>
#include <vector>

#include "setup/be.h"
#include "setup/path_util.h"

namespace gow3::setup {
namespace fs = std::filesystem;

namespace {

constexpr uint32_t kGow3Title = 0x4D5308AB;  // finder.h's kGow3TitleId (finder.h needs windows.h)
constexpr size_t kHeaderSize = 0x148;        // sizeof(XCONTENT_AGGREGATE_DATA)
constexpr size_t kDisplayNameOffset = 0x08, kDisplayNameChars = 128, kTitleIdOffset = 0x140;
constexpr size_t kFileNameBytes = 42;  // XCONTENT_DATA::file_name_raw

bool NameIs(const fs::path& p, std::wstring_view want) {
  const std::wstring name = p.filename().wstring();
  return name.size() == want.size() &&
         std::equal(name.begin(), name.end(), want.begin(),
                    [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
}

bool EndsWithPartDir(const fs::path& p) {
  constexpr std::wstring_view kPart = L".part-dir";
  std::wstring name = p.filename().wstring();
  std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
  return name.size() >= kPart.size() && name.compare(name.size() - kPart.size(), kPart.size(), kPart) == 0;
}

bool HoldsAFile(const fs::path& dir) {
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (it->is_regular_file(e2)) return true;
  }
  return false;
}

}  // namespace

bool IsLooseDlcFolder(const fs::path& dir) {
  std::error_code ec;
  if (dir.empty() || fs::is_symlink(dir, ec) || !fs::is_directory(dir, ec)) return false;
  // "C:\x\" has an empty file name: work on the path without a trailing separator.
  const fs::path p = dir.has_filename() ? dir : dir.parent_path();
  const fs::path type_dir = p.parent_path(), title_dir = type_dir.parent_path();
  if (p.filename().empty() || type_dir == p || title_dir == type_dir) return false;
  if (!NameIs(type_dir, L"00000002") || !NameIs(title_dir, L"4D5308AB") || EndsWithPartDir(p)) return false;
  return HoldsAFile(p);
}

fs::path LooseDlcHeaderPath(const fs::path& dir) {
  const fs::path p = dir.has_filename() ? dir : dir.parent_path();
  fs::path name = p.filename();
  name += ".header";
  return p.parent_path().parent_path() / "Headers" / "00000002" / name;
}

LooseDlcInfo ReadLooseDlcInfo(const fs::path& dir) {
  const fs::path p = dir.has_filename() ? dir : dir.parent_path();
  LooseDlcInfo info;
  info.display_name = Utf8(p.filename());
  std::ifstream in(LooseDlcHeaderPath(p), std::ios::binary);
  if (!in) return info;
  std::vector<uint8_t> h(kHeaderSize + 4);
  in.read(reinterpret_cast<char*>(h.data()), static_cast<std::streamsize>(h.size()));
  const size_t got = static_cast<size_t>(in.gcount());
  if (got < kHeaderSize) return info;
  const std::span<const uint8_t> s(h.data(), got);
  // Another title's header (or not a header at all): keep the folder name.
  if (const uint32_t title = Be32(s, kTitleIdOffset); title != kGow3Title) return info;
  std::string name;
  for (size_t i = 0; i < kDisplayNameChars; ++i) {
    uint32_t c = Be16(s, kDisplayNameOffset + i * 2);
    if (c == 0) break;
    if (c >= 0xD800 && c < 0xDC00 && i + 1 < kDisplayNameChars) {
      const uint32_t lo = Be16(s, kDisplayNameOffset + (i + 1) * 2);
      if (lo >= 0xDC00 && lo < 0xE000) {
        c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
        ++i;
      }
    }
    AppendUtf8(name, c);
  }
  if (!name.empty()) info.display_name = std::move(name);
  // The runtime and Xenia write the mask in the host's byte order (little-endian) after the data.
  if (got >= kHeaderSize + 4) info.license_mask = Le32(s, kHeaderSize);
  return info;
}

std::string LooseDlcContentName(const fs::path& dir) {
  const fs::path p = dir.has_filename() ? dir : dir.parent_path();
  std::string name = Utf8(p.filename());
  if (name.size() <= kFileNameBytes) return name;
  size_t cut = kFileNameBytes;
  while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;  // not inside a character
  name.resize(cut);
  return name;
}

uint64_t FolderSize(const fs::path& dir) {
  uint64_t total = 0;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
       !ec && it != end; it.increment(ec)) {
    std::error_code e2;
    if (!it->is_regular_file(e2)) continue;
    const uint64_t size = it->file_size(e2);
    if (!e2) total += size;
  }
  return total;
}

bool CopyFolder(const fs::path& src, const fs::path& dst, std::string* error) {
  auto fail = [&](const std::string& what, const std::error_code& ec) {
    if (error) *error = what + " (" + ec.message() + ")";
    return false;
  };
  std::error_code ec;
  fs::create_directories(dst, ec);
  if (ec) return fail("Could not create " + Utf8(dst), ec);
  fs::recursive_directory_iterator it(src, fs::directory_options::none, ec), end;
  if (ec) return fail("Could not read " + Utf8(src), ec);
  for (; it != end; it.increment(ec)) {
    if (ec) return fail("Could not read " + Utf8(src), ec);
    const fs::path rel = it->path().lexically_relative(src);
    const fs::path out = dst / rel;
    std::error_code e2;
    if (it->is_symlink(e2)) {
      it.disable_recursion_pending();
      continue;
    }
    if (it->is_directory(e2)) {
      fs::create_directories(out, e2);
      if (e2) return fail("Could not create " + Utf8(out), e2);
    } else if (it->is_regular_file(e2)) {
      fs::copy_file(it->path(), out, fs::copy_options::overwrite_existing, e2);
      if (e2) return fail("Could not copy " + Utf8(it->path()), e2);
    }
  }
  if (ec) return fail("Could not read " + Utf8(src), ec);
  return true;
}

}  // namespace gow3::setup
