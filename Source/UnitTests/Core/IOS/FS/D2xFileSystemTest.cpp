// Copyright 2026 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Common/ChunkFile.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/SDCardUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/IOS/FS/D2xFileSystem.h"
#include "Core/IOS/FS/HostBackend/FS.h"

using namespace IOS::HLE::FS;

namespace
{
constexpr Modes modes{Mode::ReadWrite, Mode::ReadWrite, Mode::ReadWrite};

class D2xFileSystemTest : public testing::Test
{
protected:
  void SetUp() override
  {
    m_directory = File::CreateTempDir();
    ASSERT_FALSE(m_directory.empty());
    m_original_image = File::GetUserPath(F_WIISDCARDIMAGE_IDX);
    m_image = m_directory + "/card.raw";
    File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_image);
    Config::Init();
    Config::SetCurrent(Config::MAIN_WII_SD_CARD, true);
    Config::SetCurrent(Config::MAIN_ALLOW_SD_WRITES, true);
    ASSERT_TRUE(Common::SDCardCreate(128, m_image));
    m_real = std::make_shared<HostFileSystem>(m_directory + "/nand/");
    m_fs = std::make_unique<D2xFileSystem>(
        m_real, [] { return true; }, [] { return 0x4641; }, [this] { return m_disc_title; });
    ASSERT_EQ(m_fs->Mount(), 0);
    // Use the card root as an empty NAND, containing only synthetic test data.
    ASSERT_EQ(m_fs->SetMode(0x101, ""), 0);
  }

  void TearDown() override
  {
    m_fs.reset();
    m_real.reset();
    Config::ClearCurrentRunLayer();
    File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_original_image);
    if (!m_directory.empty())
      File::DeleteDirRecursively(m_directory);
  }

  std::vector<u8> SaveState()
  {
    u8* cursor = nullptr;
    PointerWrap measure(&cursor, 0, PointerWrap::Mode::Measure);
    m_fs->DoState(measure);
    std::vector<u8> state(reinterpret_cast<size_t>(cursor));
    cursor = state.data();
    PointerWrap writer(&cursor, state.size(), PointerWrap::Mode::Write);
    m_fs->DoState(writer);
    EXPECT_TRUE(writer.IsWriteMode());
    EXPECT_EQ(cursor, state.data() + state.size());
    return state;
  }

  bool LoadState(std::vector<u8>& state)
  {
    u8* cursor = state.data();
    PointerWrap reader(&cursor, state.size(), PointerWrap::Mode::Read);
    m_fs->DoState(reader);
    return reader.IsReadMode();
  }

  void ChangeImageSerial(const std::string& path, u32 boot_sector = 0)
  {
    File::IOFile image(path, "r+b");
    ASSERT_TRUE(image);
    // SDCardCreate makes FAT32 images, whose volume serial starts at 0x43.
    const u64 serial_offset = static_cast<u64>(boot_sector) * 512 + 0x43;
    u8 serial_byte = 0;
    ASSERT_TRUE(image.Seek(serial_offset, File::SeekOrigin::Begin));
    ASSERT_TRUE(image.ReadBytes(&serial_byte, 1));
    serial_byte ^= 0xff;
    ASSERT_TRUE(image.Seek(serial_offset, File::SeekOrigin::Begin));
    ASSERT_TRUE(image.WriteBytes(&serial_byte, 1));
    ASSERT_TRUE(image.Close());
  }

  std::string m_directory;
  std::string m_image;
  std::string m_original_image;
  std::shared_ptr<HostFileSystem> m_real;
  std::unique_ptr<D2xFileSystem> m_fs;
  bool m_disc_title = false;
};

TEST_F(D2xFileSystemTest, ExactRoutingRules)
{
  EXPECT_TRUE(m_fs->IsEmulated("/sys/uid.sys"));
  EXPECT_TRUE(m_fs->IsEmulated("/tmp/uid.sys"));
  EXPECT_FALSE(m_fs->IsEmulated("/tmp/launch.sys"));
  EXPECT_FALSE(m_fs->IsEmulated("/dev/fs"));
  EXPECT_FALSE(m_fs->IsEmulated("/device"));
  ASSERT_EQ(m_fs->SetMode(1, ""), 0);
  for (const auto* path : {"/ticket/00010001/x", "/ticket/00010005", "/title/00010000/x",
                           "/title/00010001", "/title/00010004/x", "/title/00010005/x", "/tmp",
                           "/tmpfile", "/importing", "/sys/disc.sys"})
    EXPECT_TRUE(m_fs->IsEmulated(path)) << path;
  for (const auto* path : {"/tmp/uid.sys", "/tmp/launch.sys", "/sys/uid.sys", "/shared1",
                           "/title/00000001/x", "/ticket/00010004", "/sys/disc.sys2"})
    EXPECT_FALSE(m_fs->IsEmulated(path)) << path;
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  EXPECT_FALSE(m_fs->IsEmulated("/title/00010001/x"));
}

TEST_F(D2xFileSystemTest, ImagePersistenceAndIndependentHandles)
{
  const std::string path = "/title/00010001/test.bin";
  ASSERT_EQ(m_fs->CreateFile(0, 0, path, 0, modes), ResultCode::Success);
  EXPECT_FALSE(m_real->GetMetadata(0, 0, path));
  const auto writer = m_fs->OpenFile(0, 0, path, Mode::ReadWrite);
  const auto reader = m_fs->OpenFile(0, 0, path, Mode::Read);
  ASSERT_TRUE(writer);
  ASSERT_TRUE(reader);
  const std::array<u8, 4> bytes{1, 2, 3, 4};
  ASSERT_TRUE(writer->Write(bytes.data(), bytes.size()));
  std::array<u8, 4> read{};
  ASSERT_TRUE(reader->Read(read.data(), read.size()));
  EXPECT_EQ(read, bytes);
  EXPECT_EQ(reader->Write(bytes.data(), bytes.size()).Error(), ResultCode::AccessDenied);
  EXPECT_EQ(writer->GetStatus()->offset, 4u);
  EXPECT_EQ(writer->GetStatus()->size, 4u);
  EXPECT_EQ(*writer->Seek(static_cast<u32>(-2), SeekMode::End), 2u);
  EXPECT_EQ(writer->Seek(5, SeekMode::Set).Error(), ResultCode::Invalid);
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  EXPECT_FALSE(m_fs->GetMetadata(0, 0, path));
  // Remount the physical image: no sync-folder or live FatFs cache can supply it.
  m_fs->Unmount();
  ASSERT_EQ(m_fs->Mount(), 0);
  ASSERT_EQ(m_fs->SetMode(0x101, ""), 0);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, path)->size, 4u);
}

TEST_F(D2xFileSystemTest, EscapingMetadataAndDirectoryOperations)
{
  const std::string directory = "/title/00010001/12345678";
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, directory, 0, modes), ResultCode::Success);
  const std::string name = "\"*:<>?|";
  const std::string path = directory + '/' + name;
  ASSERT_EQ(m_fs->CreateFile(0, 0, path, 0, modes), ResultCode::Success);
  const auto entries = m_fs->ReadDirectory(0, 0, directory);
  ASSERT_TRUE(entries);
  EXPECT_EQ(*entries, (std::vector<std::string>{name}));
  const auto metadata = m_fs->GetMetadata(0, 0, path);
  ASSERT_TRUE(metadata);
  EXPECT_EQ(metadata->uid, 0x1001u);
  EXPECT_EQ(metadata->gid, 0x4641u);
  EXPECT_EQ(metadata->modes, modes);
  EXPECT_EQ(m_fs->SetMetadata(0, path, 123, 45, 7, {}), ResultCode::Success);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, path)->uid, 0x1001u);
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/tmp/title.tmd", 0, modes), ResultCode::Success);
  const auto es_metadata = m_fs->GetMetadata(0, 0, "/tmp/title.tmd");
  ASSERT_TRUE(es_metadata);
  EXPECT_EQ(es_metadata->uid, 0u);
  EXPECT_EQ(es_metadata->gid, 0u);
  EXPECT_EQ(es_metadata->modes.other, Mode::None);
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, directory + "/data", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, directory + "/data/nocopy", 0, modes), ResultCode::Success);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, directory + "/data/nocopy")->modes.other, Mode::None);
  ASSERT_EQ(m_fs->Rename(0, 0, path, directory + "/renamed"), ResultCode::Success);
  EXPECT_EQ(m_fs->Delete(0, 0, directory), ResultCode::AccessDenied);
  EXPECT_EQ(m_fs->Delete(0, 0, directory + "/renamed"), ResultCode::Success);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, path).Error(), ResultCode::NotFound);
  ASSERT_EQ(m_fs->SetMode(1, ""), 0);
  EXPECT_EQ(m_fs->Rename(0, 0, "/tmp/title.tmd", "/sys/title.tmd"), ResultCode::Invalid);
  EXPECT_EQ(m_fs->CreateFile(0, 0, "/tmp/../outside", 0, modes), ResultCode::NotFound);
}

TEST_F(D2xFileSystemTest, FakeStatsAndDiscUsage)
{
  const auto stats = m_fs->GetNandStats();
  ASSERT_TRUE(stats);
  EXPECT_EQ(stats->free_clusters, 0x5decu);
  EXPECT_EQ(stats->used_inodes, 0x394u);
  EXPECT_EQ(m_fs->GetDirectoryStats("/does-not-exist")->used_clusters, 1u);
  m_disc_title = true;
  EXPECT_EQ(m_fs->GetDirectoryStats("/title/00010001")->used_clusters, 1505u);
  EXPECT_EQ(m_fs->GetDirectoryStats("/title/00010001")->used_inodes, 74u);
  EXPECT_EQ(m_fs->GetDirectoryStats("/does-not-exist").Error(), ResultCode::NotFound);
}

TEST_F(D2xFileSystemTest, SelectedNandFolderAndRealNandStaySeparate)
{
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, "/nands", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, "/nands/test", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_real->CreateDirectory(0, 0, "/sys", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_real->CreateFile(0, 0, "/sys/marker", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_fs->SetMode(0x101, "/nands/test"), 0);
  EXPECT_FALSE(m_fs->GetMetadata(0, 0, "/sys/marker"));
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/sys/on-card", 0, modes), ResultCode::Success);
  EXPECT_FALSE(m_real->GetMetadata(0, 0, "/sys/on-card"));
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  EXPECT_TRUE(m_fs->GetMetadata(0, 0, "/sys/marker"));
  EXPECT_FALSE(m_fs->GetMetadata(0, 0, "/sys/on-card"));
  ASSERT_EQ(m_fs->SetMode(0x101, ""), 0);
  EXPECT_TRUE(m_fs->GetMetadata(0, 0, "/nands/test/sys/on-card"));
}

TEST_F(D2xFileSystemTest, MissingRemovedAndInvalidCardsFail)
{
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  m_fs->Unmount();
  Config::SetCurrent(Config::MAIN_WII_SD_CARD, false);
  EXPECT_EQ(m_fs->Mount(), -4);
  EXPECT_EQ(m_fs->SetMode(0x101, ""), -4);
  Config::SetCurrent(Config::MAIN_WII_SD_CARD, true);
  File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_directory + "/missing.raw");
  EXPECT_EQ(m_fs->Mount(), -101);
  EXPECT_EQ(m_fs->SetMode(0x101, ""), -101);
  File::IOFile invalid(m_directory + "/invalid.raw", "wb");
  const std::array<u8, 512> zeros{};
  ASSERT_TRUE(invalid.WriteArray(zeros));
  ASSERT_TRUE(invalid.Close());
  File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_directory + "/invalid.raw");
  EXPECT_EQ(m_fs->Mount(), -101);
  EXPECT_EQ(m_fs->SetMode(2, ""), -4);
  const std::string usb_image = m_directory + "/usb.raw";
  ASSERT_TRUE(Common::SDCardCreate(128, usb_image));
  Config::SetCurrent(Config::MAIN_WII_USB_IMAGE_PATH, usb_image);
  EXPECT_EQ(m_fs->Mount(-1, true), 0);
  EXPECT_EQ(m_fs->SetMode(2, ""), 0);
  m_fs->Unmount(true);
  File::SetUserPath(F_WIISDCARDIMAGE_IDX, m_image);
  ASSERT_EQ(m_fs->Mount(), 0);
  ASSERT_EQ(m_fs->SetMode(0x101, ""), 0);
  Config::SetCurrent(Config::MAIN_WII_SD_CARD, false);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, "/title").Error(), ResultCode::Invalid);
}

TEST_F(D2xFileSystemTest, SavestateRestoresRoutingAndHandleOffsets)
{
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/test", 0, modes), ResultCode::Success);
  auto file = m_fs->OpenFile(0, 0, "/title/test", Mode::ReadWrite);
  ASSERT_TRUE(file);
  const std::array<u8, 3> bytes{7, 8, 9};
  ASSERT_TRUE(file->Write(bytes.data(), bytes.size()));
  const Fd fd = file->Release();
  std::vector<u8> state(1024 * 1024);
  u8* cursor = state.data();
  PointerWrap writer(&cursor, state.size(), PointerWrap::Mode::Write);
  m_fs->DoState(writer);
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  ASSERT_EQ(m_fs->Close(fd), ResultCode::Success);
  cursor = state.data();
  PointerWrap reader(&cursor, state.size(), PointerWrap::Mode::Read);
  m_fs->DoState(reader);
  ASSERT_TRUE(reader.IsReadMode());
  EXPECT_TRUE(m_fs->IsEmulated("/title/test"));
  ASSERT_TRUE(m_fs->GetFileStatus(fd));
  EXPECT_EQ(m_fs->GetFileStatus(fd)->offset, 3u);
  ASSERT_TRUE(m_fs->SeekFile(fd, 0, SeekMode::Set));
  std::array<u8, 3> read{};
  ASSERT_TRUE(m_fs->ReadBytesFromFile(fd, read.data(), static_cast<u32>(read.size())));
  EXPECT_EQ(read, bytes);
  EXPECT_EQ(m_fs->Close(fd), ResultCode::Success);
}

TEST_F(D2xFileSystemTest, SavestateRejectsChangedImagePath)
{
  auto state = SaveState();
  const std::string replacement = m_directory + "/replacement.raw";
  ASSERT_TRUE(File::CopyRegularFile(m_image, replacement));
  File::SetUserPath(F_WIISDCARDIMAGE_IDX, replacement);
  EXPECT_FALSE(LoadState(state));
}

TEST_F(D2xFileSystemTest, SavestateRejectsMissingImage)
{
  auto state = SaveState();
  ASSERT_TRUE(File::Delete(m_image));
  EXPECT_FALSE(LoadState(state));
}

TEST_F(D2xFileSystemTest, SavestateRejectsResizedImage)
{
  auto state = SaveState();
  File::IOFile image(m_image, "r+b");
  ASSERT_TRUE(image);
  ASSERT_TRUE(image.Resize(image.GetSize() + 512));
  ASSERT_TRUE(image.Close());
  EXPECT_FALSE(LoadState(state));
}

TEST_F(D2xFileSystemTest, SavestateRejectionPreservesLiveFilesystemState)
{
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/test", 0, modes), ResultCode::Success);
  auto file = m_fs->OpenFile(0, 0, "/title/test", Mode::ReadWrite);
  ASSERT_TRUE(file);
  const Fd fd = file->Release();
  auto state = SaveState();

  ASSERT_EQ(m_fs->Close(fd), ResultCode::Success);
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  m_fs->Unmount();
  const auto config = m_fs->GetNandConfig();
  ASSERT_EQ(m_real->CreateDirectory(0, 0, "/tmp", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_real->CreateFile(0, 0, "/tmp/live", 0, modes), ResultCode::Success);
  ChangeImageSerial(m_image);

  EXPECT_FALSE(LoadState(state));
  const auto after = m_fs->GetNandConfig();
  EXPECT_EQ(after.mode, config.mode);
  EXPECT_EQ(after.nand_path, config.nand_path);
  EXPECT_EQ(after.volume_mounted, config.volume_mounted);
  EXPECT_EQ(after.volume_offsets, config.volume_offsets);
  EXPECT_FALSE(m_fs->GetFileStatus(fd));
  // Restoring the real NAND backend would have replaced /tmp with the saved copy.
  EXPECT_TRUE(m_real->GetMetadata(0, 0, "/tmp/live"));
}

TEST_F(D2xFileSystemTest, SavestateChecksBothMountedImages)
{
  const std::string usb_image = m_directory + "/usb.raw";
  ASSERT_TRUE(Common::SDCardCreate(128, usb_image));
  Config::SetCurrent(Config::MAIN_WII_USB_IMAGE_PATH, usb_image);
  ASSERT_EQ(m_fs->Mount(-1, true), 0);
  auto state = SaveState();
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  ChangeImageSerial(usb_image);

  EXPECT_FALSE(LoadState(state));
  EXPECT_EQ(m_fs->GetNandConfig().mode, 0u);
}

TEST_F(D2xFileSystemTest, SavestateChecksAutomaticallySelectedPartitionBootSector)
{
  // Only the boot sectors are needed to exercise identity checking; no FAT
  // request is made against this synthetic partition layout.
  File::IOFile image(m_image, "r+b");
  ASSERT_TRUE(image);
  std::array<u8, 512> boot{};
  ASSERT_TRUE(image.ReadArray(&boot));
  ASSERT_TRUE(image.Seek(64 * 512, File::SeekOrigin::Begin));
  ASSERT_TRUE(image.WriteArray(boot));
  std::array<u8, 512> mbr{};
  mbr[446 + 4] = 0x0c;
  mbr[446 + 8] = 64;
  mbr[510] = 0x55;
  mbr[511] = 0xaa;
  ASSERT_TRUE(image.Seek(0, File::SeekOrigin::Begin));
  ASSERT_TRUE(image.WriteArray(mbr));
  ASSERT_TRUE(image.Close());
  auto state = SaveState();
  ChangeImageSerial(m_image, 64);
  EXPECT_FALSE(LoadState(state));
}

TEST_F(D2xFileSystemTest, SavestateChecksExplicitlySelectedPartitionBootSector)
{
  File::IOFile image(m_image, "r+b");
  ASSERT_TRUE(image);
  std::array<u8, 512> boot{};
  ASSERT_TRUE(image.ReadArray(&boot));
  ASSERT_TRUE(image.Seek(64 * 512, File::SeekOrigin::Begin));
  ASSERT_TRUE(image.WriteArray(boot));
  ASSERT_TRUE(image.Close());
  auto config = m_fs->GetNandConfig();
  config.volume_offsets[0] = 64;
  m_fs->ApplyNandConfig(config);
  auto state = SaveState();
  ChangeImageSerial(m_image, 64);
  EXPECT_FALSE(LoadState(state));
}

TEST_F(D2xFileSystemTest, SavestateChecksUnmountedImageWithOpenHandle)
{
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/test", 0, modes), ResultCode::Success);
  const auto file = m_fs->OpenFile(0, 0, "/title/test", Mode::ReadWrite);
  ASSERT_TRUE(file);
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  m_fs->Unmount();
  auto state = SaveState();
  ChangeImageSerial(m_image);
  EXPECT_FALSE(LoadState(state));
}

TEST_F(D2xFileSystemTest, SavestateDoesNotRequireUnusedImages)
{
  ASSERT_EQ(m_fs->SetMode(0, ""), 0);
  m_fs->Unmount();
  auto state = SaveState();
  ASSERT_TRUE(File::Delete(m_image));
  EXPECT_TRUE(LoadState(state));
  EXPECT_EQ(m_fs->GetNandConfig().mode, 0u);
  EXPECT_FALSE(m_fs->GetNandConfig().volume_mounted[0]);
}

TEST_F(D2xFileSystemTest, SavestateAcceptsOrdinaryImageWrites)
{
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/test", 0, modes), ResultCode::Success);
  const auto file = m_fs->OpenFile(0, 0, "/title/test", Mode::ReadWrite);
  ASSERT_TRUE(file);
  auto state = SaveState();
  const std::array<u8, 3> bytes{7, 8, 9};
  ASSERT_TRUE(file->Write(bytes.data(), bytes.size()));
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/another", 0, modes), ResultCode::Success);

  ASSERT_TRUE(LoadState(state));
  EXPECT_EQ(file->GetStatus()->offset, 0u);
  std::array<u8, 3> read{};
  ASSERT_TRUE(file->Read(read.data(), read.size()));
  EXPECT_EQ(read, bytes);
}

// The NAND emulation stays on the drive its mode names: mounting the USB drive,
// or a refused switch to USB, must not move SD saves onto the USB image.
TEST_F(D2xFileSystemTest, RedirectFollowsModeNotLastMount)
{
  const std::string usb_image = m_directory + "/usb.raw";
  ASSERT_TRUE(Common::SDCardCreate(128, usb_image));
  Config::SetCurrent(Config::MAIN_WII_USB_IMAGE_PATH, usb_image);
  ASSERT_EQ(m_fs->Mount(-1, true), 0);
  EXPECT_EQ(m_fs->SetMode(0x102, "/bad:path"), -101);

  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/onsd", 0, modes), ResultCode::Success);

  ASSERT_EQ(m_fs->SetMode(0x102, ""), 0);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, "/title/onsd").Error(), ResultCode::NotFound);
  ASSERT_EQ(m_fs->SetMode(0x101, ""), 0);
  EXPECT_TRUE(m_fs->GetMetadata(0, 0, "/title/onsd"));
}

TEST_F(D2xFileSystemTest, SuperfloppyPartitionSelectionAndEmptyWrite)
{
  ASSERT_EQ(m_fs->Mount(0), 0);
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/empty", 0, modes), ResultCode::Success);
  auto file = m_fs->OpenFile(0, 0, "/title/empty", Mode::Write);
  ASSERT_TRUE(file);
  const Fd fd = file->Release();
  const auto written = m_fs->WriteBytesToFile(fd, nullptr, 0);
  ASSERT_TRUE(written);
  EXPECT_EQ(*written, 0u);
  EXPECT_EQ(m_fs->GetFileStatus(fd)->size, 0u);
  EXPECT_EQ(m_fs->Close(fd), ResultCode::Success);
}

TEST_F(D2xFileSystemTest, CarriedNandConfigAppliesWithoutMount)
{
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, "/nands", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_fs->CreateDirectory(0, 0, "/nands/carry", 0, modes), ResultCode::Success);
  ASSERT_EQ(m_fs->SetMode(0x101, "/nands/carry"), 0);
  EXPECT_TRUE(m_fs->IsEmulated("/sys/uid.sys"));
  EXPECT_FALSE(m_fs->IsEmulated("/tmp/launch.sys"));
  EXPECT_FALSE(m_fs->IsEmulated("/dev/fs"));
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/00010001/keep.bin", 0, modes), ResultCode::Success);
  auto original = m_fs->OpenFile(0, 0, "/title/00010001/keep.bin", Mode::Read);
  ASSERT_TRUE(original);
  const auto config = m_fs->GetNandConfig();
  EXPECT_TRUE(config.mode & 0x80);
  EXPECT_EQ(config.nand_path, "/nands/carry");
  D2xFileSystem carried(
      m_real, [] { return true; }, [] { return 0x4641; }, [this] { return m_disc_title; });
  carried.ApplyNandConfig(config);
  EXPECT_FALSE(carried.GetFileStatus(0x10000));
  EXPECT_EQ(carried.IsEmulated("/sys/uid.sys"), m_fs->IsEmulated("/sys/uid.sys"));
  EXPECT_EQ(carried.IsEmulated("/tmp/launch.sys"), m_fs->IsEmulated("/tmp/launch.sys"));
  EXPECT_EQ(carried.IsEmulated("/dev/fs"), m_fs->IsEmulated("/dev/fs"));
  EXPECT_EQ(carried.IsEmulated("/title/00010001/x"), m_fs->IsEmulated("/title/00010001/x"));
  const std::string path = "/title/00010001/carried.bin";
  ASSERT_EQ(carried.CreateFile(0, 0, path, 0, modes), ResultCode::Success);
  auto writer = carried.OpenFile(0, 0, path, Mode::ReadWrite);
  ASSERT_TRUE(writer);
  const std::array<u8, 4> bytes{9, 8, 7, 6};
  ASSERT_TRUE(writer->Write(bytes.data(), bytes.size()));
  const auto reader = m_fs->OpenFile(0, 0, path, Mode::Read);
  ASSERT_TRUE(reader);
  std::array<u8, 4> read{};
  ASSERT_TRUE(reader->Read(read.data(), read.size()));
  EXPECT_EQ(read, bytes);
  EXPECT_FALSE(m_real->GetMetadata(0, 0, path));
}

TEST(D2xReloadCarryTest, TakenOnce)
{
  D2xReloadCarry carry;
  carry.nand_config.mode = 0x101;
  carry.nand_config.nand_path = "/nands/carry";
  carry.nand_config.volume_mounted = {true, false};
  carry.nand_config.volume_offsets = {123, 456};
  carry.di_fragments = true;
  carry.di_disable_reset = true;
  carry.es_config = true;
  SetReloadCarryForNextIOSBoot(carry);
  const D2xReloadCarry first = TakeReloadCarryForNextIOSBoot();
  EXPECT_TRUE(first.has_value);
  EXPECT_EQ(first.nand_config.mode, carry.nand_config.mode);
  EXPECT_EQ(first.nand_config.nand_path, carry.nand_config.nand_path);
  EXPECT_EQ(first.nand_config.volume_mounted, carry.nand_config.volume_mounted);
  EXPECT_EQ(first.nand_config.volume_offsets, carry.nand_config.volume_offsets);
  EXPECT_TRUE(first.di_fragments);
  EXPECT_TRUE(first.di_disable_reset);
  EXPECT_TRUE(first.es_config);
  const D2xReloadCarry second = TakeReloadCarryForNextIOSBoot();
  EXPECT_FALSE(second.has_value);
  EXPECT_FALSE(second.di_fragments);
  EXPECT_FALSE(second.di_disable_reset);
  EXPECT_FALSE(second.es_config);
}

TEST(D2xReloadCarryTest, DiOnlyWithNandOff)
{
  D2xReloadCarry carry;
  carry.di_disable_reset = true;
  SetReloadCarryForNextIOSBoot(carry);
  const D2xReloadCarry first = TakeReloadCarryForNextIOSBoot();
  EXPECT_TRUE(first.has_value);
  EXPECT_EQ(first.nand_config.mode, 0u);
  EXPECT_FALSE(first.di_fragments);
  EXPECT_TRUE(first.di_disable_reset);
  EXPECT_FALSE(first.es_config);
  EXPECT_FALSE(TakeReloadCarryForNextIOSBoot().has_value);
}

TEST_F(D2xFileSystemTest, RenameDoesNotRetargetHandlesFromAnotherNandFolder)
{
  for (const char* folder : {"/nand-a", "/nand-b"})
    ASSERT_EQ(m_fs->CreateDirectory(0, 0, folder, 0, modes), ResultCode::Success);

  ASSERT_EQ(m_fs->SetMode(0x101, "/nand-a"), 0);
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/a", 0, modes), ResultCode::Success);
  auto original = m_fs->OpenFile(0, 0, "/title/a", Mode::ReadWrite);
  ASSERT_TRUE(original);

  ASSERT_EQ(m_fs->SetMode(0x101, "/nand-b"), 0);
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/a", 0, modes), ResultCode::Success);
  auto renamed = m_fs->OpenFile(0, 0, "/title/a", Mode::ReadWrite);
  ASSERT_TRUE(renamed);
  ASSERT_EQ(m_fs->Rename(0, 0, "/title/a", "/title/b"), ResultCode::Success);
  const u8 first = 0x41;
  const u8 second = 0x42;
  ASSERT_TRUE(original->Write(&first, 1));
  ASSERT_TRUE(renamed->Write(&second, 1));
  ASSERT_TRUE(renamed->Seek(0, SeekMode::Set));
  u8 actual = 0;
  ASSERT_TRUE(renamed->Read(&actual, 1));
  EXPECT_EQ(actual, second);

  ASSERT_EQ(m_fs->SetMode(0x101, "/nand-a"), 0);
  auto reader = m_fs->OpenFile(0, 0, "/title/a", Mode::Read);
  ASSERT_TRUE(reader);
  ASSERT_TRUE(reader->Read(&actual, 1));
  EXPECT_EQ(actual, first);
  EXPECT_FALSE(m_fs->GetMetadata(0, 0, "/title/b"));
}

TEST_F(D2xFileSystemTest, RenameDoesNotRetargetHandlesFromAnotherDrive)
{
  const std::string usb_image = m_directory + "/usb.raw";
  ASSERT_TRUE(Common::SDCardCreate(128, usb_image));
  Config::SetCurrent(Config::MAIN_WII_USB_IMAGE_PATH, usb_image);
  ASSERT_EQ(m_fs->Mount(-1, true), 0);

  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/a", 0, modes), ResultCode::Success);
  auto sd_file = m_fs->OpenFile(0, 0, "/title/a", Mode::ReadWrite);
  ASSERT_TRUE(sd_file);
  ASSERT_EQ(m_fs->SetMode(0x102, ""), 0);
  ASSERT_EQ(m_fs->CreateFile(0, 0, "/title/a", 0, modes), ResultCode::Success);
  auto usb_file = m_fs->OpenFile(0, 0, "/title/a", Mode::ReadWrite);
  ASSERT_TRUE(usb_file);
  ASSERT_EQ(m_fs->Rename(0, 0, "/title/a", "/title/b"), ResultCode::Success);
  const u8 sd_value = 0x53;
  const u8 usb_value = 0x55;
  ASSERT_TRUE(sd_file->Write(&sd_value, 1));
  ASSERT_TRUE(usb_file->Write(&usb_value, 1));

  ASSERT_EQ(m_fs->SetMode(0x101, ""), 0);
  auto reader = m_fs->OpenFile(0, 0, "/title/a", Mode::Read);
  ASSERT_TRUE(reader);
  u8 actual = 0;
  ASSERT_TRUE(reader->Read(&actual, 1));
  EXPECT_EQ(actual, sd_value);
  EXPECT_FALSE(m_fs->GetMetadata(0, 0, "/title/b"));
  ASSERT_TRUE(usb_file->Seek(0, SeekMode::Set));
  ASSERT_TRUE(usb_file->Read(&actual, 1));
  EXPECT_EQ(actual, usb_value);
}

TEST_F(D2xFileSystemTest, DirectoryRenameUpdatesChildrenButNotSharedPrefixes)
{
  for (const char* directory : {"/title/a", "/title/ab"})
    ASSERT_EQ(m_fs->CreateDirectory(0, 0, directory, 0, modes), ResultCode::Success);
  for (const char* path : {"/title/a/child?", "/title/ab/child?"})
    ASSERT_EQ(m_fs->CreateFile(0, 0, path, 0, modes), ResultCode::Success);
  auto child = m_fs->OpenFile(0, 0, "/title/a/child?", Mode::ReadWrite);
  auto sibling = m_fs->OpenFile(0, 0, "/title/ab/child?", Mode::ReadWrite);
  ASSERT_TRUE(child);
  ASSERT_TRUE(sibling);
  ASSERT_EQ(m_fs->Rename(0, 0, "/title/a", "/title/b"), ResultCode::Success);
  const u8 value = 0x61;
  ASSERT_TRUE(child->Write(&value, 1));
  ASSERT_TRUE(sibling->Write(&value, 1));
  EXPECT_EQ(m_fs->GetMetadata(0, 0, "/title/b/child?")->size, 1u);
  EXPECT_EQ(m_fs->GetMetadata(0, 0, "/title/ab/child?")->size, 1u);
}
}  // namespace
