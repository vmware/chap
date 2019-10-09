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

  /*
   * On both passes through the allocations each allocation will be visited
   * in address order, and each tagger will be run through the following phases
   * on the given allocation.  This is terminated early for any allocation that
   * has already been tagged or if all taggers have returned
   * NO_TAGGING_DONE_FROM_HERE for that allocation during the current pass or
   * if one tagger has returned TAGGING_DONE during the given pass on the given
   * allocation.
   */

  enum Pass { FIRST_PASS_THROUGH_ALLOCATIONS, LAST_PASS_THROUGH_ALLOCATIONS };

  enum Phase {
    QUICK_INITIAL_CHECK,  // Fast initial check, match must be solid
    MEDIUM_CHECK,         // Sublinear if reject, match must be solid
    SLOW_CHECK,           // May be expensive, match must be solid
    WEAK_CHECK            // May be expensive, weak results OK
  };

  enum Result {
    NOT_SURE_YET,         // At least one more phase is needed
    TAGGING_DONE,         // A match was found and tagging was done
    NO_TAGGING_FROM_HERE  // No match is possible
  };
  Tagger() {}

  virtual Result TagFromAllocation(Pass pass, AllocationIndex index,
                                   Phase phase, const Allocation& allocation,
                                   bool isUnsigned) = 0;
};
}  // namespace Allocations
}  // namespace chap
