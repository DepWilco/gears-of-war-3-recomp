// gow3 - ReXGlue Recompiled Project
//
// Player-supplied DLC: Xbox 360 marketplace packages (STFS files starting with "LIVE", "PIRS" or
// "CON ") put in a DLC folder are installed with the SDK's ContentManager::InstallContent, which
// extracts them to <content root>/0000000000000000/4D5308AB/00000002/<file name>/ and writes the
// header file the game's content enumeration reads. A package counts as installed once its header
// exists (InstallContent writes it last), so an interrupted install is redone on the next start.
// Searched: gow3_dlc_folder if set, else a DLC folder next to gow3.exe and one inside the game
// folder, each recursively (the packages keep their console layout, 4D5308AB\00000002\<name>).
//
// Loose (already extracted) DLC: a folder in that same layout, 4D5308AB\00000002\<name>\ holding
// the package's files (setup/loose_dlc.h; Xenia's content folder looks like this). It is copied to
// the same place InstallContent extracts to and gets the same .header file, with the display name
// and license mask of the Xenia .header beside it when there is one (else the folder name and no
// license mask, as Xenia treats a content folder without a header).

#include "gow3_dlc.h"

#include <array>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/runtime.h>
#include <rex/system/xam/content_device.h>
#include <rex/system/xam/content_manager.h>

#include "setup/loose_dlc.h"

REXCVAR_DEFINE_STRING(gow3_dlc_folder, "", "Content",
                      "Folder with your own Gears of War 3 DLC packages (searched recursively). "
                      "Empty: a DLC folder next to gow3.exe and one inside the game folder");

namespace rex::glue {
namespace {

bool IsStfsPackage(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::array<char, 4> magic{};
  if (!in.read(magic.data(), magic.size())) return false;
  const std::string m(magic.data(), magic.size());
  return m == "LIVE" || m == "PIRS" || m == "CON ";
}

std::vector<std::filesystem::path> DlcFolders(const std::filesystem::path& game_folder) {
  const std::string configured = REXCVAR_GET(gow3_dlc_folder);
  if (!configured.empty()) return {std::filesystem::u8path(configured)};
  std::vector<std::filesystem::path> folders = {
      rex::filesystem::GetExecutablePath().parent_path() / "DLC"};
  if (!game_folder.empty()) folders.push_back(game_folder / "DLC");
  return folders;
}

void InstallLooseDlc(rex::system::KernelState* ks, rex::system::xam::ContentManager* content,
                     const std::filesystem::path& folder) {
  using rex::system::XContentType;
  const uint32_t title_id = ks->title_id();
  const std::string name = gow3::setup::LooseDlcContentName(folder);
  rex::system::xam::XCONTENT_AGGREGATE_DATA installed{};
  if (content->ReadContentHeaderFile(name, 0, title_id, XContentType::kMarketplaceContent, installed) ==
      X_ERROR_SUCCESS) {
    REXLOG_INFO("DLC already installed: {} ({}, loose folder)", rex::path_to_utf8(installed.display_name()),
                name);
    return;
  }
  std::filesystem::path root = ks->emulator()->user_data_root();
  if (root.empty()) {
    REXLOG_ERROR("DLC install of loose folder {} skipped: no user data folder", rex::path_to_utf8(folder));
    return;
  }
  std::error_code ec;
  root = std::filesystem::absolute(root, ec);
  // ContentManager::ResolvePackagePath for marketplace content (xuid 0).
  const std::filesystem::path dest = root / "0000000000000000" / fmt::format("{:08X}", title_id) /
                                     fmt::format("{:08X}", uint32_t(XContentType::kMarketplaceContent)) /
                                     rex::to_path(name);
  REXLOG_INFO("DLC installing loose folder {} ...", rex::path_to_utf8(folder));
  const auto start = std::chrono::steady_clock::now();
  // Files already there (an interrupted copy) are overwritten; the folder may also be the
  // destination itself (gow3_dlc_folder pointing into the content folder), then only the header is new.
  if (!std::filesystem::equivalent(folder, dest, ec)) {
    std::string error;
    if (!gow3::setup::CopyFolder(folder, dest, &error)) {
      REXLOG_ERROR("DLC install of loose folder {} failed: {}", rex::path_to_utf8(folder), error);
      return;
    }
  }
  const gow3::setup::LooseDlcInfo info = gow3::setup::ReadLooseDlcInfo(folder);
  rex::system::xam::XCONTENT_AGGREGATE_DATA data{};
  data.device_id = static_cast<uint32_t>(rex::system::xam::DummyDeviceId::HDD);
  data.content_type = XContentType::kMarketplaceContent;
  data.title_id = title_id;
  data.xuid = 0;
  data.set_file_name(name);
  data.set_display_name(rex::path_to_utf16(rex::to_path(info.display_name)));
  // Written last: the header is what marks the install as complete.
  const X_RESULT result = content->WriteContentHeaderFile(0, data, info.license_mask);
  const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
  if (result == X_ERROR_SUCCESS) {
    REXLOG_INFO("DLC installed {} ({}, loose folder) in {:.1f} s", info.display_name, name, seconds.count());
  } else {
    REXLOG_ERROR("DLC install of loose folder {}: writing its header failed: 0x{:08X}", name, uint32_t(result));
  }
}

}  // namespace

void InstallPlayerDlc(const std::filesystem::path& game_folder) {
  auto* ks = rex::system::kernel_state();
  auto* content = ks ? ks->content_manager() : nullptr;
  if (!content) return;
  const uint32_t title_id = ks->title_id();

  for (const std::filesystem::path& folder : DlcFolders(game_folder)) {
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) continue;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, ec);
         !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
      if (std::error_code dir_ec; it->is_directory(dir_ec) && gow3::setup::IsLooseDlcFolder(it->path())) {
        it.disable_recursion_pending();  // its files are the DLC's, not packages
        InstallLooseDlc(ks, content, it->path());
        continue;
      }
      if (!it->is_regular_file(ec) || !IsStfsPackage(it->path())) continue;
      const std::string name = rex::path_to_utf8(it->path().filename());
      rex::system::xam::XCONTENT_AGGREGATE_DATA installed{};
      if (content->ReadContentHeaderFile(name, 0, title_id,
                                         rex::system::XContentType::kMarketplaceContent,
                                         installed) == X_ERROR_SUCCESS) {
        REXLOG_INFO("DLC already installed: {} ({})", rex::path_to_utf8(installed.display_name()),
                    name);
        continue;
      }
      REXLOG_INFO("DLC installing {} ...", rex::path_to_utf8(it->path()));
      const auto start = std::chrono::steady_clock::now();
      const X_RESULT result = content->InstallContent(it->path());
      const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start);
      if (result == X_ERROR_SUCCESS) {
        REXLOG_INFO("DLC installed {} in {:.1f} s", name, seconds.count());
      } else {
        REXLOG_ERROR("DLC install of {} failed: 0x{:08X}", name, uint32_t(result));
      }
    }
  }
}

}  // namespace rex::glue
