// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "Common/ChunkFile.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/Swap.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/DVD/DVDThread.h"
#include "Core/System.h"
#include "DiscIO/DiscUtils.h"
#include "DiscIO/VolumeDisc.h"
#include "UICommon/UICommon.h"

namespace
{
constexpr u32 DISC_SECTORS = 0x50000 / 512;

std::vector<u8> MakeFragmentList()
{
  // Map the whole synthetic disc to the start of the card image.
  const std::array<u32, 6> words{DISC_SECTORS, 1, 1, 0, 0, DISC_SECTORS};
  std::vector<u8> list(sizeof(words));
  for (size_t index = 0; index < words.size(); ++index)
  {
    const u32 word = Common::swap32(words[index]);
    std::memcpy(list.data() + index * sizeof(word), &word, sizeof(word));
  }
  return list;
}

class DVDStateTest : public testing::Test
{
protected:
  void SetUp() override
  {
    m_directory = File::CreateTempDir();
    ASSERT_FALSE(m_directory.empty());
    m_original_user_path = File::GetUserPath(D_USER_IDX);
    m_original_sd_path = File::GetUserPath(F_WIISDCARDIMAGE_IDX);
    UICommon::SetUserDirectory(m_directory);
    Config::Init();
    SConfig::Init();
    Core::DeclareAsCPUThread();

    // Only timing and the DVD drive are needed. Never boot a kernel or execute guest code.
    m_system.GetCoreTiming().Init();
    m_system.GetDVDInterface().Init();
    m_initialized = true;
    ASSERT_EQ(m_system.GetIOS(), nullptr);

    m_card_path = m_directory + "/card.raw";
    m_iso_path = m_directory + "/disc.iso";
    WriteDisc(m_card_path, "RTEST0");
    WriteDisc(m_iso_path, "RTEST0");
    File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_card_path);

    std::string error;
    ASSERT_EQ(m_system.GetDVDInterface().SetD2xDisc(2, MakeFragmentList(), &error), 0) << error;
    m_d2x_state = SaveState();
    m_system.GetDVDInterface().SetDisc(nullptr, {});
  }

  void TearDown() override
  {
    if (m_initialized)
    {
      m_system.GetDVDInterface().Shutdown();
      m_system.GetCoreTiming().Shutdown();
      SConfig::Shutdown();
      Config::Shutdown();
      Core::UndeclareAsCPUThread();
    }
    if (!m_original_user_path.empty())
    {
      File::SetUserPath(D_USER_IDX, m_original_user_path);
      File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_original_sd_path);
    }
    if (!m_directory.empty())
      File::DeleteDirRecursively(m_directory);
  }

  void WriteDisc(const std::string& path, const std::string& game_id)
  {
    // A header and empty partition tables are enough for VolumeWii; no game files are used.
    std::vector<u8> contents(DISC_SECTORS * 512);
    std::copy(game_id.begin(), game_id.end(), contents.begin());
    const u32 magic = Common::swap32(DiscIO::WII_DISC_MAGIC);
    std::memcpy(contents.data() + 0x18, &magic, sizeof(magic));
    File::IOFile image(path, "wb");
    ASSERT_TRUE(image.WriteBytes(contents.data(), contents.size()));
    ASSERT_TRUE(image.Close());
  }

  std::vector<u8> SaveState()
  {
    u8* cursor = nullptr;
    PointerWrap measure(&cursor, 0, PointerWrap::Mode::Measure);
    m_system.GetDVDInterface().DoState(measure);
    std::vector<u8> state(reinterpret_cast<size_t>(cursor));
    cursor = state.data();
    PointerWrap writer(&cursor, state.size(), PointerWrap::Mode::Write);
    m_system.GetDVDInterface().DoState(writer);
    EXPECT_TRUE(writer.IsWriteMode());
    EXPECT_EQ(cursor, state.data() + state.size());
    return state;
  }

  bool LoadState(std::vector<u8>& state)
  {
    u8* cursor = state.data();
    PointerWrap reader(&cursor, state.size(), PointerWrap::Mode::Read);
    m_system.GetDVDInterface().DoState(reader);
    return reader.IsReadMode();
  }

  Core::System& m_system = Core::System::GetInstance();
  std::vector<u8> m_d2x_state;
  std::string m_card_path;
  std::string m_iso_path;

private:
  bool m_initialized = false;
  std::string m_directory;
  std::string m_original_user_path;
  std::string m_original_sd_path;
};
}  // namespace

TEST_F(DVDStateTest, UndoRestoresOriginalDiscAfterRepeatedD2xLoads)
{
  auto disc = DiscIO::CreateDisc(m_iso_path);
  ASSERT_NE(disc, nullptr);
  const auto* original_disc = disc.get();
  m_system.GetDVDInterface().SetDisc(std::move(disc), {});
  auto undo_state = SaveState();

  ASSERT_TRUE(LoadState(m_d2x_state));
  ASSERT_TRUE(LoadState(m_d2x_state));
  ASSERT_TRUE(LoadState(undo_state));

  auto restored_disc = m_system.GetDVDThread().SetDisc(nullptr);
  ASSERT_EQ(restored_disc.get(), original_disc);
  EXPECT_EQ(restored_disc->GetGameID(), "RTEST0");
}

TEST_F(DVDStateTest, FailedReconstructionDoesNotEjectOriginalDiscDuringUndo)
{
  auto disc = DiscIO::CreateDisc(m_iso_path);
  ASSERT_NE(disc, nullptr);
  const auto* original_disc = disc.get();
  m_system.GetDVDInterface().SetDisc(std::move(disc), {});
  auto undo_state = SaveState();
  File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_card_path + ".missing");

  EXPECT_FALSE(LoadState(m_d2x_state));
  ASSERT_TRUE(LoadState(undo_state));

  auto restored_disc = m_system.GetDVDThread().SetDisc(nullptr);
  EXPECT_EQ(restored_disc.get(), original_disc);
}

TEST_F(DVDStateTest, WrongGameDoesNotEjectOriginalDiscDuringUndo)
{
  auto disc = DiscIO::CreateDisc(m_iso_path);
  ASSERT_NE(disc, nullptr);
  const auto* original_disc = disc.get();
  m_system.GetDVDInterface().SetDisc(std::move(disc), {});
  auto undo_state = SaveState();
  WriteDisc(m_card_path, "ROTHER");

  EXPECT_FALSE(LoadState(m_d2x_state));
  ASSERT_TRUE(LoadState(undo_state));

  auto restored_disc = m_system.GetDVDThread().SetDisc(nullptr);
  EXPECT_EQ(restored_disc.get(), original_disc);
}

TEST_F(DVDStateTest, UndoRestoresEmptyDriveWhenNoDiscWasReplaced)
{
  auto undo_state = SaveState();
  ASSERT_TRUE(LoadState(m_d2x_state));
  ASSERT_TRUE(m_system.GetDVDThread().HasDisc());
  ASSERT_TRUE(LoadState(undo_state));
  EXPECT_FALSE(m_system.GetDVDThread().HasDisc());
}

TEST_F(DVDStateTest, NormalDiscChangeKeepsTheNewDiscOnLaterNonD2xLoad)
{
  m_system.GetDVDInterface().SetDisc(DiscIO::CreateDisc(m_iso_path), {});
  auto normal_state = SaveState();
  ASSERT_TRUE(LoadState(m_d2x_state));

  auto new_disc = DiscIO::CreateDisc(m_iso_path);
  ASSERT_NE(new_disc, nullptr);
  const auto* new_disc_pointer = new_disc.get();
  m_system.GetDVDInterface().SetDisc(std::move(new_disc), {});
  ASSERT_TRUE(LoadState(normal_state));

  auto restored_disc = m_system.GetDVDThread().SetDisc(nullptr);
  EXPECT_EQ(restored_disc.get(), new_disc_pointer);
}
