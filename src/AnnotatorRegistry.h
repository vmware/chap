// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string>
#include <unordered_map>
#include "Annotator.h"
#include "Commands/Runner.h"

namespace chap {
template <typename Offset>
class AnnotatorRegistry {
 public:
  AnnotatorRegistry() {}
  void RegisterAnnotator(const Annotator<Offset>& annotator) {
    if (!_annotatorsByName.emplace(annotator.GetName(), &annotator).second) {
      std::cerr << "Warning: attempt to register annotator name "
                << annotator.GetName() << " twice.\n";
      return;
    }
    _annotatorsInRegistrationOrder.push_back(&annotator);
  }

  const Annotator<Offset>* FindAnnotator(const std::string& name) const {
    const auto it = _annotatorsByName.find(name);
    if (it != _annotatorsByName.end()) {
      return it->second;
    }
    return nullptr;
  }

  const std::list<const Annotator<Offset>* >& Annotators() const {
    return _annotatorsInRegistrationOrder;
  }

 protected:
  std::unordered_map<std::string, const Annotator<Offset>*> _annotatorsByName;
  std::list<const Annotator<Offset>*> _annotatorsInRegistrationOrder;
};
}  // namespace chap
