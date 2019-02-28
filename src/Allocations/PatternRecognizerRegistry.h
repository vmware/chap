// Copyright (c) 2017-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../ProcessImage.h"
#include "Finder.h"
#include "Graph.h"
#include "PatternRecognizer.h"

namespace chap {
namespace Allocations {
template <typename Offset>
class PatternRecognizerRegistry {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  typedef typename std::multimap<std::string, PatternRecognizer<Offset>*>
      RecognizerMap;
  PatternRecognizerRegistry() {}

  void Register(PatternRecognizer<Offset>& recognizer) {
    _recognizers.insert(std::make_pair(recognizer.GetName(), &recognizer));
  }

  /*
  * If the address is matches any of the registered patterns, provide a
  * description for the address as belonging to that pattern
  * optionally with an additional explanation of why the address matches
  * the description.
  */
  void Describe(Commands::Context& context, AllocationIndex index,
                const Allocation& allocation, bool isUnsigned,
                bool explain) const {
    for (typename RecognizerMap::const_iterator it = _recognizers.begin();
         it != _recognizers.end(); ++it) {
      /*
       * Allow any pattern recognizers where a match has occurred to report that
       * match.  At times, particularly with std::vector, there will be false
       * matches but this seems better than having no description at all and in
       * any case both matches will appear in the output, giving the user a
       * chance to make an educated decision.  Previously, the case of multiple
       * pattern matches was also reported to standard error, but there were
       * some
       * cores for which this was really annoying.
       */
      (void)(it->second->Describe(context, index, allocation, isUnsigned,
                                  explain));
    }
  }

  const PatternRecognizer<Offset>* Find(const std::string& name) const {
    typename RecognizerMap::const_iterator it = _recognizers.find(name);
    return (it != _recognizers.end()) ? it->second : nullptr;
  }

 private:
  RecognizerMap _recognizers;
};
}  // namespace Allocations
}  // namespace chap
