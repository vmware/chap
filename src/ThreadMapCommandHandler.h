// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Commands/Runner.h"
#include "SizedTally.h"
#include "ThreadMap.h"
namespace chap {
template <typename Offset>
class ThreadMapCommandHandler {
 public:
  ThreadMapCommandHandler(const ThreadMap<Offset>* threadMap)
      : _threadMap(threadMap) {}

  void SetThreadMap(const ThreadMap<Offset>* threadMap) {
    _threadMap = threadMap;
  }

  size_t ListStacks(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    Offset startAddr;
    Offset numBytes;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "list") {
      numTokensAccepted++;
      if (context.TokenAt(1) == "stacks") {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      Commands::Output& output = context.GetOutput();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: list stacks\n";
      } else if (CheckThreadMap(context)) {
        Offset totalBytes = 0;
        typename ThreadMap<Offset>::const_iterator itEnd = _threadMap->end();
        for (typename ThreadMap<Offset>::const_iterator it =
                 _threadMap->begin();
             it != itEnd; ++it) {
          output << "Thread " << std::dec << it->_threadNum
                 << " uses stack block [0x" << std::hex << it->_stackBase
                 << ", " << it->_stackLimit << ") current sp: 0x"
                 << it->_stackPointer << "\n";
          totalBytes += (it->_stackLimit - it->_stackBase);
        }
        output << std::dec << _threadMap->NumThreads() << " threads use 0x"
               << std::hex << totalBytes << " bytes.\n";
      }
    }
    return numTokensAccepted;
  }

  size_t CountStacks(Commands::Context& context, bool checkOnly) {
    Commands::Output& output = context.GetOutput();
    Offset startAddr;
    Offset numBytes;
    size_t numTokensAccepted = 0;
    if (context.TokenAt(0) == "count") {
      numTokensAccepted++;
      if (context.TokenAt(1) == "stacks") {
        numTokensAccepted++;
      }
    }
    if (!checkOnly) {
      Commands::Error& error = context.GetError();
      Commands::Output& output = context.GetOutput();
      if (context.GetNumTokens() != numTokensAccepted ||
          numTokensAccepted != 2) {
        error << "Usage: list entries\n";
      } else if (CheckThreadMap(context)) {
        Offset totalBytes = 0;
        SizedTally<Offset> tally(context, "stacks");
        typename ThreadMap<Offset>::const_iterator itEnd = _threadMap->end();
        for (typename ThreadMap<Offset>::const_iterator it =
                 _threadMap->begin();
             it != itEnd; ++it) {
          tally.AdjustTally(it->_stackLimit - it->_stackBase);
        }
      }
    }
    return numTokensAccepted;
  }

  virtual void AddCommandCallbacks(Commands::Runner& r) {
    r.AddCommand("list",
                 std::bind(&ThreadMapCommandHandler::ListStacks, this,
                           std::placeholders::_1, std::placeholders::_2));
    r.AddCommand("count",
                 std::bind(&ThreadMapCommandHandler::CountStacks, this,
                           std::placeholders::_1, std::placeholders::_2));
  }

 private:
  const ThreadMap<Offset>* _threadMap;
  bool CheckThreadMap(Commands::Context& context) {
    if (_threadMap == (ThreadMap<Offset>*)(0)) {
      context.GetError() << "No thread map is available.\n";
      return false;
    }
    return true;
  }
};

}  // namespace chap
