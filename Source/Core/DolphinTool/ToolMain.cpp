// Copyright 2021 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>
#include <fmt/ostream.h>

#include "Common/StringUtil.h"
#include "Core/Core.h"
#include "Core/WiiUtils.h"
#include "UICommon/UICommon.h"

#include "DolphinTool/ConvertCommand.h"
#include "DolphinTool/ExtractCommand.h"
#include "DolphinTool/HeaderCommand.h"
#include "DolphinTool/VerifyCommand.h"

static void PrintUsage()
{
  fmt::print(std::cerr, "usage: dolphin-tool COMMAND -h\n"
                        "\n"
                        "commands supported: [convert, verify, header, extract, installwad]\n");
}

// installwad <user folder> <wad>...: installs each WAD into that user
// folder's NAND for good, as Tools > Install WAD does (booting a WAD only
// installs it until emulation stops). For RiftWii's channel tests.
static int InstallWads(const std::vector<std::string>& args)
{
  if (args.size() < 2)
  {
    fmt::print(std::cerr, "usage: dolphin-tool installwad <user folder> <wad>...\n");
    return EXIT_FAILURE;
  }
  UICommon::SetUserDirectory(args[0]);
  UICommon::CreateDirectories();
  UICommon::Init();
  int failed = 0;
  for (size_t i = 1; i < args.size(); ++i)
  {
    const bool installed = WiiUtils::InstallWAD(args[i]);
    fmt::print(std::cerr, "{}: {}\n", args[i], installed ? "installed" : "FAILED");
    if (!installed)
      ++failed;
  }
  UICommon::Shutdown();
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

#ifdef _WIN32
#define main app_main
#endif

int main(int argc, char* argv[])
{
  Core::DeclareAsHostThread();

  if (argc < 2)
  {
    PrintUsage();
    return EXIT_FAILURE;
  }

  const std::string_view command_str = argv[1];
  // Take off the program name and command selector before passing arguments down
  const std::vector<std::string> args(argv + 2, argv + argc);

  if (command_str == "convert")
    return DolphinTool::ConvertCommand(args);
  else if (command_str == "verify")
    return DolphinTool::VerifyCommand(args);
  else if (command_str == "header")
    return DolphinTool::HeaderCommand(args);
  else if (command_str == "extract")
    return DolphinTool::Extract(args);
  else if (command_str == "installwad")
    return InstallWads(args);
  PrintUsage();
  return EXIT_FAILURE;
}

#ifdef _WIN32
int wmain(int, wchar_t*[], wchar_t*[])
{
  std::vector<std::string> args = Common::CommandLineToUtf8Argv(GetCommandLineW());
  const int argc = static_cast<int>(args.size());
  std::vector<char*> argv(args.size());
  for (size_t i = 0; i < args.size(); ++i)
    argv[i] = args[i].data();

  return main(argc, argv.data());
}

#undef main
#endif
