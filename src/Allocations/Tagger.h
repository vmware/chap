// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "Finder.h"

namespace chap {
namespace Allocations {

/*
 * A Tagger can tag one or more allocations based on the characteristics of
 * a starting allocation and possibly of references to the starting allocation
 * or following references starting at that allocation.  Certain allocations
 * may be expensive to rule in or out fully, so a multi-phased approach is done
 * where the first phases are expected to be quite cheap unless there is a
 * clear match, in which case it is fine to take more time to complete the
 * tagging.
 */
template <typename Offset>
class Tagger {
 public:
  typedef typename Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Finder<Offset>::Allocation Allocation;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  /*
   * On both passes through the allocations each allocation will be visited
   * in address order, and each tagger will be run through the following phases
   * on the given allocation.  This is terminated early for any allocation for
   * which all the taggers have returned true from TagFromAllocation on that
   * allocation.
   */

  enum Pass { FIRST_PASS_THROUGH_ALLOCATIONS, LAST_PASS_THROUGH_ALLOCATIONS };

  enum Phase {
    QUICK_INITIAL_CHECK,  // Fast initial check, match must be solid
    MEDIUM_CHECK,         // Sublinear if reject, match must be solid
    SLOW_CHECK,           // May be expensive, match must be solid
    WEAK_CHECK            // May be expensive, weak results OK
  };

  Tagger() {}

  /*
   * Look the allocation once to figure out if the contents of this allocation
   * can be used to resolve information about this allocation or others.  Return
   * true if and only if there is no need for this tagger to look any more at
   * this allocation during the given pass.
   */
  virtual bool TagFromAllocation(Reader& reader, Pass pass,
                                 AllocationIndex index, Phase phase,
                                 const Allocation& allocation,
                                 bool isUnsigned) = 0;
};
}  // namespace Allocations
}  // namespace chap
