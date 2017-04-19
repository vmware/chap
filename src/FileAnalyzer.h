// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>

#include "Commands/CountCommand.h"
#include "Commands/EnumerateCommand.h"
#include "Commands/ListCommand.h"
#include "Commands/Runner.h"
#include "Commands/ShowCommand.h"
#include "Commands/SummarizeCommand.h"
namespace chap {
class FileAnalyzer {
 public:
  FileAnalyzer();

  virtual ~FileAnalyzer() {}

  /*
   * Return true if the file is known to be truncated.
   */

  virtual bool FileIsKnownTruncated() { return false; }

  /*
   * Return the actual file size of the current file or 0 if none.  This
   * should never throw.
   */

  virtual uint64_t GetFileSize() const = 0;

  /*
   * Return the minimum expected file size, based on information from the
   * start of the file, or 0 if the expected file size is not known.  Note
   * that if the file format is unsupported, the expected file normally will
   * not be known but this is not a requirement, because, for example, an ELF
   * crash dump analyzer would not support an ELF executable but still might
   * reasonably be able to determine the expected size as part of common ELF
   * processing.
   */

  virtual uint64_t GetMinimumExpectedFileSize() const {
    return 0;  // Indicate that the expected file size is not known.
  }

  /*
   * Add command callbacks.  This should include all the ones reasonably
   * supported for this file format, including ones that are disabled because
   * some key piece of information is missing from the file.
   */

  virtual void AddCommandCallbacks(Commands::Runner& r);

  /*
   * Add commands.  This should include all the ones reasonably supported
   * for this file format.
   */

  virtual void AddCommands(Commands::Runner& r) {
    r.AddCommand(_countCommand);
    r.AddCommand(_summarizeCommand);
    r.AddCommand(_enumerateCommand);
    r.AddCommand(_listCommand);
    r.AddCommand(_showCommand);
  }

 protected:
  Commands::CountCommand _countCommand;
  Commands::SummarizeCommand _summarizeCommand;
  Commands::EnumerateCommand _enumerateCommand;
  Commands::ListCommand _listCommand;
  // Commands::DescribeCommand _describeCommand;
  Commands::ShowCommand _showCommand;
};
}  // namespace chap
