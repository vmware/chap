// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
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
  PatternRecognizerRegistry(const ProcessImage<Offset>* processImage) {
    SetProcessImage(processImage);
  }

  void SetProcessImage(const ProcessImage<Offset>* processImage) {
    _processImage = processImage;
    if (processImage == 0) {
      _addressMap = 0;
      _finder = 0;
      _graph = 0;
    } else {
      _addressMap = &(processImage->GetVirtualAddressMap());
      _finder = processImage->GetAllocationFinder();
      _graph = processImage->GetAllocationGraph();
    }
    for (typename RecognizerMap::iterator it = _recognizers.begin();
         it != _recognizers.end(); ++it) {
      it->second->SetProcessImage(_processImage);
    }
  }

  void Register(PatternRecognizer<Offset>* recognizer) {
    _recognizers.insert(std::make_pair(recognizer->GetName(), recognizer));
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
    size_t numPatternsMatched = 0;
    for (typename RecognizerMap::const_iterator it = _recognizers.begin();
         it != _recognizers.end(); ++it) {
      if (it->second->Describe(context, index, allocation, isUnsigned,
                               explain)) {
        numPatternsMatched++;
      }
    }
    if (numPatternsMatched > 1) {
      context.GetError()
          << "Warning: This allocation matches multiple patterns.\n";
    }
  }

  const PatternRecognizer<Offset>* Find(const std::string& name) const {
    typename RecognizerMap::const_iterator it = _recognizers.find(name);
    if (it != _recognizers.end()) {
      return it->second;
    } else {
      return (PatternRecognizer<Offset>*)(0);
    }
  }

 private:
  const ProcessImage<Offset>* _processImage;
  const VirtualAddressMap<Offset>* _addressMap;
  const Finder<Offset>* _finder;
  const Graph<Offset>* _graph;
  RecognizerMap _recognizers;
};
}  // namespace Allocations
}  // namespace chap
