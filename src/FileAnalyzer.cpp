// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

extern "C" {
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
};
#include <iostream>
#include <memory>
#include "Commands/Runner.h"
#include "FileImage.h"
#include "Linux/ELFCore32FileAnalyzerFactory.h"
#include "Linux/ELFCore64FileAnalyzerFactory.h"

namespace chap {
using namespace std;

FileAnalyzer::FileAnalyzer() {}
void FileAnalyzer::AddCommandCallbacks(Commands::Runner & /* r */) {}

void PrintUsageAndExit(int exitCode,
                       const vector<string> supportedFileFormats) {
  cerr << "Usage: chap [-t] <file>\n\n"
          "-t means to just do truncation check then stop\n"
          "   0 exit code means no truncation was found\n\n"
          "Supported file types include the following:\n\n";
  for (vector<string>::const_iterator it = supportedFileFormats.begin();
       it != supportedFileFormats.end(); ++it) {
    cerr << *it << "\n";
  }
  exit(exitCode);
}
}  // namespace chap

using namespace chap;
int main(int argc, char **argv, char ** /* envp */) {
  vector<FileAnalyzerFactory *> factories;

  Linux::ELFCore64FileAnalyzerFactory elf64CoreAnalyzerFactory;
  factories.push_back(&elf64CoreAnalyzerFactory);
  Linux::ELFCore32FileAnalyzerFactory elf32CoreAnalyzerFactory;
  factories.push_back(&elf32CoreAnalyzerFactory);

  vector<string> supportedFileFormats;
  for (vector<FileAnalyzerFactory *>::iterator it = factories.begin();
       it != factories.end(); ++it) {
    supportedFileFormats.push_back((*it)->GetSupportedFileFormat());
  }

  string path(argv[argc - 1]);
  if ((argc < 2) || (argc > 3) || ((argc == 3) && (strcmp(argv[1], "-t"))) ||
      (path[0] == '-')) {
    PrintUsageAndExit(1, supportedFileFormats);
  }

  bool truncationCheckOnly = (argc == 3);

  try {
    FileImage fileImage(path.c_str());
    for (vector<FileAnalyzerFactory *>::iterator it = factories.begin();
         it != factories.end(); ++it) {
      /*
       * Try to create a file analyzer of the given type, telling it to
       * find allocations eagerly unless we are only checking for truncation.
       */
      FileAnalyzer *analyzer =
          (*it)->MakeFileAnalyzer(fileImage, truncationCheckOnly);
      if (analyzer == 0) {
        continue;
      }
      if (analyzer->FileIsKnownTruncated()) {
        cerr << path << " is truncated." << endl;
        uint64_t fileSize = analyzer->GetFileSize();
        uint64_t minimumExpectedFileSize =
            analyzer->GetMinimumExpectedFileSize();
        if (fileSize > 0 && minimumExpectedFileSize > 0) {
          cerr << "It has size " << dec << fileSize
               << " which is smaller than minimum expected size "
               << minimumExpectedFileSize << "." << endl;
          if (!truncationCheckOnly) {
            cerr << "Many commands may be disabled or inaccurate as a "
                 << "result." << endl;
          }
        }
        if (truncationCheckOnly) {
          exit(1);
        }
      }
      if (!truncationCheckOnly) {
        Commands::Runner commandsRunner(path);

        analyzer->AddCommands(commandsRunner);
        // TODO - the call to AddCommandCallbacks will become obsolete
        analyzer->AddCommandCallbacks(commandsRunner);

        commandsRunner.RunCommands();
      }
      delete analyzer;
      exit(0);
    }

    cerr << "File \"" << path << "\" is of some unsupported format." << endl;
    PrintUsageAndExit(1, supportedFileFormats);
  } catch (...) {
    exit(1);
  }
}
