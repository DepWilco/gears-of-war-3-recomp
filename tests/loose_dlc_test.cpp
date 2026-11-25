#include <cstdint>
#include <string>
#include <vector>

#include "setup/loose_dlc.h"
#include "tests/test_util.h"

namespace fs = std::filesystem;

namespace {

// A Xenia / ReXGlue .header: XCONTENT_AGGREGATE_DATA (0x148 bytes, big-endian fields), then the
// license mask in host (little-endian) order when `license` is non-zero.
std::vector<uint8_t> MakeContentHeader(std::u16string_view display_name, uint32_t title_id, uint32_t license) {
  std::vector<uint8_t> h(0x148, 0);
  h[7] = 0x02;  // content type 00000002
  for (size_t i = 0; i < display_name.size() && i < 128; ++i) {
    h[0x08 + i * 2] = static_cast<uint8_t>(display_name[i] >> 8);
    h[0x08 + i * 2 + 1] = static_cast<uint8_t>(display_name[i]);
  }
  for (int i = 0; i < 4; ++i) h[0x140 + i] = static_cast<uint8_t>(title_id >> (24 - 8 * i));
  if (license) {
    for (int i = 0; i < 4; ++i) h.push_back(static_cast<uint8_t>(license >> (8 * i)));
  }
  return h;
}

}  // namespace

TEST(loose_dlc_folder_needs_the_layout_and_a_file) {
  auto root = TempDir("loose1");
  const fs::path type_dir = root / "RAAM's Shadow" / "4D5308AB" / "00000002";
  WriteFile(type_dir / "354B60" / "GearGame" / "CookedXenon" / "a.xxx", "A");
  fs::create_directories(type_dir / "empty" / "sub");
  WriteFile(type_dir / "354B60.part-dir" / "a.xxx", "A");
  WriteFile(root / "lower" / "4d5308ab" / "00000002" / "pack" / "f", "F");
  WriteFile(root / "other" / "12345678" / "00000002" / "pack" / "f", "F");
  WriteFile(root / "tu" / "4D5308AB" / "000B0000" / "pack" / "f", "F");
  CHECK(gow3::setup::IsLooseDlcFolder(type_dir / "354B60"));
  CHECK(gow3::setup::IsLooseDlcFolder(root / "lower" / "4d5308ab" / "00000002" / "pack"));
  CHECK(!gow3::setup::IsLooseDlcFolder(type_dir / "empty"));            // no file in it
  CHECK(!gow3::setup::IsLooseDlcFolder(type_dir / "354B60.part-dir"));  // an interrupted copy
  CHECK(!gow3::setup::IsLooseDlcFolder(root / "other" / "12345678" / "00000002" / "pack"));  // another title
  CHECK(!gow3::setup::IsLooseDlcFolder(root / "tu" / "4D5308AB" / "000B0000" / "pack"));     // not DLC
  CHECK(!gow3::setup::IsLooseDlcFolder(type_dir));
  CHECK(!gow3::setup::IsLooseDlcFolder(type_dir / "354B60" / "GearGame" / "CookedXenon" / "a.xxx"));  // a file
  CHECK(!gow3::setup::IsLooseDlcFolder(type_dir / "missing"));
}

TEST(loose_dlc_info_from_xenia_header_or_folder_name) {
  auto root = TempDir("loose2");
  const fs::path title_dir = root / "content" / "0000000000000000" / "4D5308AB";
  const fs::path plain = title_dir / "00000002" / "Horde Command Pack", named = title_dir / "00000002" / "58410A5D";
  const fs::path foreign = title_dir / "00000002" / "foreign";
  for (const fs::path& p : {plain, named, foreign}) WriteFile(p / "f", "F");
  WriteFile(title_dir / "Headers" / "00000002" / "58410A5D.header", MakeContentHeader(u"RAAM's Shadow", 0x4D5308AB, 0xFFFFFFFF));
  WriteFile(title_dir / "Headers" / "00000002" / "foreign.header", MakeContentHeader(u"Not Gears", 0x12345678, 1));

  auto info = gow3::setup::ReadLooseDlcInfo(plain);
  CHECK(info.display_name == "Horde Command Pack" && info.license_mask == 0);
  info = gow3::setup::ReadLooseDlcInfo(named);
  CHECK(info.display_name == "RAAM's Shadow" && info.license_mask == 0xFFFFFFFF);
  info = gow3::setup::ReadLooseDlcInfo(foreign);
  CHECK(info.display_name == "foreign" && info.license_mask == 0);
  CHECK(gow3::setup::LooseDlcHeaderPath(named) == title_dir / "Headers" / "00000002" / "58410A5D.header");
}

TEST(loose_dlc_content_name_fits_xcontent_data) {
  CHECK(gow3::setup::LooseDlcContentName(fs::path("x") / "RAAM's Shadow") == "RAAM's Shadow");
  const std::string id42(42, 'A');
  CHECK(gow3::setup::LooseDlcContentName(fs::path("x") / (id42 + "BC")) == id42);
  // 41 ASCII bytes, then a two-byte character: cut before it, not through it.
  const std::string name = std::string(41, 'a') + "\xC3\xA9";
  CHECK(gow3::setup::LooseDlcContentName(fs::path("x") / fs::path(std::u8string(name.begin(), name.end()))) ==
        std::string(41, 'a'));
}

TEST(loose_dlc_copy_folder_and_size) {
  auto root = TempDir("loose3");
  WriteFile(root / "src" / "a.bin", "AAAA");
  WriteFile(root / "src" / "sub" / "deeper" / "b.bin", "BB");
  fs::create_directories(root / "src" / "empty");
  WriteFile(root / "dst" / "a.bin", "OLD");
  std::string err;
  CHECK(gow3::setup::CopyFolder(root / "src", root / "dst", &err));
  CHECK(SameTree(root / "src", root / "dst"));
  CHECK(fs::is_directory(root / "dst" / "empty"));
  CHECK(gow3::setup::FolderSize(root / "src") == 6);
  CHECK(!gow3::setup::CopyFolder(root / "missing", root / "dst2", &err) && !err.empty());
}
