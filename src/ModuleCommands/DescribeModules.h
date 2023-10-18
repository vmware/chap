// Copyright (c) 2023 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Commands/Runner.h"
#include "../Commands/Subcommand.h"
#include "../ModuleDirectory.h"
#include "../SizedTally.h"
namespace chap {
namespace ModuleCommands {
template <class Offset>
class DescribeModules : public Commands::Subcommand {
 public:
  DescribeModules(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("describe", "modules"),
        _moduleDirectory(processImage.GetModuleDirectory()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command describes the modules and their address ranges.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    SizedTally<Offset> tally(context, "modules");
    for (const auto& nameAndModuleInfo : _moduleDirectory) {
      const auto& moduleInfo = nameAndModuleInfo.second;
      const std::string& path = nameAndModuleInfo.first;
      if (!path.empty() && path[0] == '/') {
        output << "The module at runtime path " << path;
        ModuleImage<Offset>* moduleImage = moduleInfo._moduleImage.get();
        if (moduleImage == nullptr) {
          output << "\n has no copy available to chap.\n";
          if (!moduleInfo._incompatiblePaths.empty()) {
            std::cerr
                << " The following candidates had incompatible contents:\n";
            for (const auto path : moduleInfo._incompatiblePaths) {
              output << "  " << path << "\n";
            }
          }
        } else {
          const std::string& moduleImagePath = moduleImage->GetPath();
          if (moduleImagePath == path) {
            output << "\n has a current copy at that location.\n";
          } else {
            output << "\n has a current copy available to chap at\n   "
                   << moduleImagePath << "\n";
          }
        }
      } else {
        output << "Module name " << path
               << " has no absolute path known to chap.\n";
      }

      output << " It uses the following ranges:\n";
      Offset totalBytesForModule = 0;
      for (const auto& range : moduleInfo._ranges) {
        totalBytesForModule += range._size;
        output << "  [0x" << std::hex << range._base << ", 0x" << range._limit
               << ") ";
        int flags = range._value._flags;
        if (flags & RangeAttributes::HAS_KNOWN_PERMISSIONS) {
          if (flags & RangeAttributes::IS_READABLE) {
            if (flags & RangeAttributes::IS_WRITABLE) {
              if (flags & RangeAttributes::IS_EXECUTABLE) {
                // This is mildly unexpected.
                output << " is readable, writable and executable";
              } else {
                output << " is readable and writable";
              }
            } else {
              if (flags & RangeAttributes::IS_EXECUTABLE) {
                output << " is readable and executable";
              } else {
                output << " is readable but not writable or executable";
              }
            }
          } else {
            if (flags & RangeAttributes::IS_WRITABLE) {
              if (flags & RangeAttributes::IS_EXECUTABLE) {
                // This is unexpected.
                output << " is (unexpectedly) writable and executable but not "
                          "readable";
              } else {
                // This is unexpected.
                output << " is (unexpectedly) writable but not readable";
              }
            } else {
              if (flags & RangeAttributes::IS_EXECUTABLE) {
                // This is unexpected.
                output << " is (unexpectedly) executable but not readable";
              } else {
                /*
                 * This happens if the process has reserved a virtual address
                 * range but is not currently using it.
                 */
                output << " is not readable, writable or executable";
              }
            }
          }
        } else {
          output << " has unknown permissions";
        }
        if ((flags & RangeAttributes::IS_MAPPED) != 0) {
          if ((flags & RangeAttributes::IS_TRUNCATED) != 0) {
            output << "\n   and is missing due to truncation of "
                      "the process image";
          } else {
            output << "\n   and is mapped into the process image";
          }
        } else {
          output << "\n   and is not mapped into the process image";
        }
        output << ".\n";
      }
      tally.AdjustTally(totalBytesForModule);
    }
  }

 private:
  typedef typename VirtualAddressMap<Offset>::RangeAttributes RangeAttributes;
  const ModuleDirectory<Offset>& _moduleDirectory;
};
}  // namespace ModuleCommands
}  // namespace chap
