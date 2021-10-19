// Copyright (c) 2019-2021 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include "ContiguousImage.h"
#include "Directory.h"
#include "Graph.h"

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
  typedef typename Directory<Offset>::AllocationIndex AllocationIndex;
  typedef typename Directory<Offset>::Allocation Allocation;
  typedef typename Graph<Offset>::EdgeIndex EdgeIndex;
  typedef typename VirtualAddressMap<Offset>::Reader Reader;

  /*
   * On both passes through the allocations each allocation will be visited
   * in address order, and each tagger will be run through the following phases
   * on the given allocation.  This is terminated early for any allocation for
   * which all the taggers have returned true from TagFromAllocation on that
   * allocation.
   */

  enum Phase {
    QUICK_INITIAL_CHECK,  // Fast initial check, match must be solid
    MEDIUM_CHECK,         // Sublinear if reject, match must be solid
    SLOW_CHECK,           // May be expensive, match must be solid
    WEAK_CHECK            // May be expensive, weak results OK
  };

  Tagger() {}
  virtual ~Tagger() {}

  /*
   * Look at the allocation to figure out if the contents of this allocation
   * can be used to resolve information about this allocation and possibly
   * others.  Return true if and only if there is no need for this tagger to
   * look any more at this allocation during the given pass.
   */
  virtual bool TagFromAllocation(const ContiguousImage<Offset>& contiguousImage,
                                 Reader& reader, AllocationIndex index,
                                 Phase phase, const Allocation& allocation,
                                 bool isUnsigned) = 0;

  /*
   * Look the allocation to figure out if the contents of this allocation
   * can be used to resolve information about referenced allocations.
   * Return true if and only if there is no need for this tagger to
   * look any more at this allocation during the given pass.
   */
  virtual bool TagFromReferenced(
      const ContiguousImage<Offset>& /* contiguousImage */,
      Reader& /* reader */, AllocationIndex /* index */, Phase /* phase */,
      const Allocation& /* allocation */,
      const AllocationIndex* /* unresolvedOutgoing */) {
    return true;
  }

  /*
   * If any targets of the given allocation still need to be marked as
   * favored, do so.
   */
  virtual void MarkFavoredReferences(
      const ContiguousImage<Offset>& /* contiguousImage */,
      Reader& /* reader */, AllocationIndex /* index */,
      const Allocation& /* allocation */,
      const EdgeIndex* /* outgoingEdgeIndices */) {}
};
}  // namespace Allocations
}  // namespace chap
