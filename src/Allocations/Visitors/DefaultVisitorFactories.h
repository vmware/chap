// Copyright (c) 2017 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "../Visitors/Counter.h"
#include "../Visitors/Describer.h"
#include "../Visitors/Enumerator.h"
#include "../Visitors/Explainer.h"
#include "../Visitors/Lister.h"
#include "../Visitors/Shower.h"
#include "../Visitors/Summarizer.h"
#include "../Describer.h"
namespace chap {
namespace Allocations {
namespace Visitors {
template <class Offset>
class DefaultVisitorFactories {
 public:
  DefaultVisitorFactories(const Allocations::Describer<Offset>& describer)
      : _describerFactory(describer), _explainerFactory(describer) {}
  typename Visitors::Counter<Offset>::Factory _counterFactory;
  typename Visitors::Summarizer<Offset>::Factory _summarizerFactory;
  typename Visitors::Enumerator<Offset>::Factory _enumeratorFactory;
  typename Visitors::Lister<Offset>::Factory _listerFactory;
  typename Visitors::Shower<Offset>::Factory _showerFactory;
  typename Visitors::Describer<Offset>::Factory _describerFactory;
  typename Visitors::Explainer<Offset>::Factory _explainerFactory;
};

}  // namespace Subcommands
}  // namespace Allocations
}  // namespace chap
