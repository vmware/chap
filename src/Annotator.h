// Copyright (c) 2024 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once

#include <functional>
#include <string>
#include "Commands/Runner.h"
#include "VirtualAddressMap.h"

namespace chap {
template <typename Offset>
class Annotator {
 public:
  Annotator(const std::string& name) : _name(name) {}

 public:
  /*
   * If the given range [address, address+limit) starts with and fully
   * contains something the annotator can describe, write an annotation
   * to the current output, calling the specified functor to create the
   * annotation header and starting the subsequent lines with the
   * specifid prefix.
   */
  typedef std::function<void(Offset /*base*/, Offset /*limit*/,
                             const std::string& /*annotator*/)>
      WriteHeaderFunction;
  virtual Offset Annotate(Commands::Context& context,
                          typename VirtualAddressMap<Offset>::Reader& reader,
                          WriteHeaderFunction writeHeader, Offset address,
                          Offset limit, const std::string& prefix) const = 0;

  const std::string& GetName() const { return _name; }

 protected:
  const std::string _name;
};
}  // namespace chap
