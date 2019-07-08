// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <map>
#include "../../Commands/Runner.h"
#include "../../Commands/Subcommand.h"
#include "../SignatureDirectory.h"
namespace chap {
namespace Allocations {
namespace Subcommands {
template <class Offset>
class SummarizeSignatures : public Commands::Subcommand {
 public:
  SummarizeSignatures(const ProcessImage<Offset>& processImage)
      : Commands::Subcommand("summarize", "signatures"),
        _signatureDirectory(processImage.GetSignatureDirectory()) {}

  void ShowHelpMessage(Commands::Context& context) {
    context.GetOutput()
        << "This command summarizes the status of all the signatures found.\n";
  }

  void Run(Commands::Context& context) {
    Commands::Output& output = context.GetOutput();
    Offset numSignatures = 0;
    std::vector<size_t> counts;
    counts.resize(SignatureDirectory<Offset>::VTABLE_WITH_NAME_FROM_BINDEFS + 1,
                  0);
    typename SignatureDirectory<Offset>::SignatureNameAndStatusConstIterator
        itEnd = _signatureDirectory.EndSignatures();
    for (typename SignatureDirectory<
             Offset>::SignatureNameAndStatusConstIterator it =
             _signatureDirectory.BeginSignatures();
         it != itEnd; ++it) {
      typename SignatureDirectory<Offset>::Status status = it->second.second;
      counts[status]++;
      numSignatures++;
    }
    output << std::dec;
    size_t count =
        counts[SignatureDirectory<Offset>::UNWRITABLE_PENDING_SYMDEFS];
    if (count > 0) {
      output << count << " signatures are unwritable addresses "
                         "pending .symdefs file creation.\n";
    }
    count = counts[SignatureDirectory<Offset>::UNWRITABLE_MISSING_FROM_SYMDEFS];
    if (count > 0) {
      output << count << " signatures are unwritable addresses "
                         "missing from the .symdefs file.\n";
    }
    count = counts[SignatureDirectory<Offset>::VTABLE_WITH_NAME_FROM_SYMDEFS];
    if (count > 0) {
      output << count << " signatures are vtable pointers "
                         "defined in the .symdefs file.\n";
    }
    count =
        counts[SignatureDirectory<Offset>::UNWRITABLE_WITH_NAME_FROM_SYMDEFS];
    if (count > 0) {
      output << count << " signatures are unwritable addresses "
                         "defined in the .symdefs file.\n";
    }
    count =
        counts[SignatureDirectory<Offset>::VTABLE_WITH_NAME_FROM_PROCESS_IMAGE];
    if (count > 0) {
      output << count << " signatures are vtable pointers "
                         "with names from the process image.\n";
    }
    count = counts[SignatureDirectory<
        Offset>::WRITABLE_VTABLE_WITH_NAME_FROM_PROCESS_IMAGE];
    if (count > 0) {
      output << count << " signatures point to writable vtables "
                         "with names from the process image.\n";
    }
    count = counts[SignatureDirectory<Offset>::VTABLE_WITH_NAME_FROM_BINARY];
    if (count > 0) {
      output << count << " signatures are vtable pointers "
                         "with names from libraries or executables.\n";
    }
    count = counts[SignatureDirectory<Offset>::WRITABLE_MODULE_REFERENCE];
    if (count > 0) {
      output << count << " signatures point to writable memory for modules.\n";
    }
    count = counts[SignatureDirectory<Offset>::VTABLE_WITH_NAME_FROM_BINDEFS];
    if (count > 0) {
      output << count << " signatures are vtable pointers "
                         "with names from the .bindefs file.\n";
    }

    output << numSignatures << " signatures in total were found.\n";
  }

 private:
  const SignatureDirectory<Offset>& _signatureDirectory;
};
}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
