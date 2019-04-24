// Copyright (c) 2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#pragma once
#include <string.h>
#include "Allocations/PatternRecognizer.h"
#include "ProcessImage.h"

namespace chap {
template <typename Offset>
class DoublyLinkedListNodeRecognizer
    : public Allocations::PatternRecognizer<Offset> {
 public:
  typedef typename Allocations::Finder<Offset>::AllocationIndex AllocationIndex;
  typedef typename Allocations::PatternRecognizer<Offset> Base;
  typedef typename Allocations::Finder<Offset>::Allocation Allocation;
  DoublyLinkedListNodeRecognizer(const ProcessImage<Offset>& processImage)
      : Allocations::PatternRecognizer<Offset>(processImage,
                                               "DoublyLinkedListNode") {}

  bool Matches(AllocationIndex index, const Allocation& allocation,
               bool isUnsigned) const {
    return Visit(nullptr, index, allocation, isUnsigned, false);
  }

  /*
  *If the address is matches any of the registered patterns, provide a
  *description for the address as belonging to that pattern
  *optionally with an additional explanation of why the address matches
  *the description.  Return true only if the allocation matches the
  *pattern.
  */
  virtual bool Describe(Commands::Context& context, AllocationIndex index,
                        const Allocation& allocation, bool isUnsigned,
                        bool explain) const {
    return Visit(&context, index, allocation, isUnsigned, explain);
  }

 private:
  virtual bool Visit(Commands::Context* context, AllocationIndex index,
                     const Allocation& allocation, bool isUnsigned,
                     bool explain) const {
    if (!isUnsigned) {
      /*
       * Such nodes are already unsigned because the links are at the start.
       */
      return false;
    }
    Offset allocationSize = allocation.Size();
    if (allocationSize < 2 * sizeof(Offset)) {
      // There must be space for two links.
      return false;
    }
    Offset allocationAddress = allocation.Address();
    typename VirtualAddressMap<Offset>::Reader reader(Base::_addressMap);
    Offset next = reader.ReadOffset(allocationAddress, 0xbadbad);
    Offset prev =
        reader.ReadOffset(allocationAddress + sizeof(Offset), 0xbadbad);

    /*
     * Do simple checks to see if the links are of any interest, to avoid
     * spending much time on a failed check.
     */

    if (next == allocationAddress) {
      /*
       * We are not interested in nodes that reference themselves because in
       * the case of std::list there is always a header and we don't want
       * to identify an allocation that happens to contain an empty std::list
       * as the first field as being a list node.  The case of some other
       * sort of list where, for example, the nodes are linked in a circular
       * fashion and the header points to the first or the last, is not
       * all that interesting if the list just contains one element because
       * it will probably be pretty easy for the user to figure out in some
       * other way now the list is used.
       */
      return false;
    }

    if (next == 0) {
      /*
       * We are interested only in things that at least superficially are
       * doubly linked.  Ideally, we'd want to enforce a ring, just to pick
       * up mostly candidates for std::list, but to avoid huge times in the
       * describe command, as opposed to the explain command, we just look
       * in the immediate neighborhood of the allocation being recognized.
       */
      return false;
    }

    if ((next & (sizeof(Offset) - 1)) != 0) {
      return false;
    }
    if ((next != prev) && (prev == allocationAddress || prev == 0 ||
                           (prev & (sizeof(Offset) - 1)) != 0)) {
      return false;
    }

    /*
     * Make sure any reference points back.
     */

    if (reader.ReadOffset(next + sizeof(Offset), 0) != allocationAddress) {
      return false;
    }
    if (reader.ReadOffset(prev, 0) != allocationAddress) {
      return false;
    }

    /*
     * So this looks as if it might be on a doubly linked list or might be
     * a header.  Figure out the allocation status of the adjacent nodes and
     * possibly use that to figure out the header.
     */

    AllocationIndex numAllocations = Base::_finder->NumAllocations();

    /*
     * The header may be in another allocation or not.  We don't always
     * need to calculate the header, but in any case it is not yet known
     * here.
     */

    Offset header = 0;
    AllocationIndex headerIndex = numAllocations;
    const Allocation* headerAllocation = nullptr;

    const Allocation* nextAllocation = nullptr;
    AllocationIndex nextIndex = numAllocations;

    const Allocation* prevAllocation = nullptr;
    AllocationIndex prevIndex = numAllocations;

    nextIndex = Base::_finder->AllocationIndexOf(next);
    if (nextIndex != numAllocations) {
      nextAllocation = Base::_finder->AllocationAt(nextIndex);
      if (nextAllocation->Address() != next) {
        /*
         * Given that the links are not at the start of the next allocation,
         * the next allocation is not in the list and probably contains
         * the list header.  Note that the fact that we insist that
         * a list node must have links at the start of the allocation already
         * means that we would rule out any signed allocation as matching
         * the pattern.
         */
        header = next;
        headerIndex = nextIndex;
        headerAllocation = nextAllocation;
      }
    } else {
      header = next;
    }

    if (next == prev) {
      prevIndex = nextIndex;
      prevAllocation = nextAllocation;
    } else {
      prevIndex = Base::_finder->AllocationIndexOf(prev);
      if (prevIndex != numAllocations) {
        prevAllocation = Base::_finder->AllocationAt(prevIndex);
        if (prevAllocation->Address() != prev) {
          /*
           * Given that the links are not at the start of the prev allocation,
           * the prev allocation is not in the list and probably contains
           * the list header.
           */
          if (header != 0) {
            /*
             * It doesn't match the pattern if the allocation has two
             * different adjacent nodes and both appear to be headers.
             */
            return false;
          }
          header = prev;
          headerIndex = prevIndex;
          headerAllocation = prevAllocation;
        }
      } else {
        if (header != 0) {
          /*
           * It doesn't match the pattern if the allocation has two
           * different adjacent nodes and both appear to be headers.
           */
          return false;
        }
        header = prev;
      }
    }

    if (header == 0) {
      /*
       * We haven't figured out the header node yet, which is not
       * surprising, given that we have only looked in the immediate
       * neighborhood of the allocation, which may not be near the
       * and of the list.  In the interest of avoiding making the cost
       * of matching the pattern or simply describing the allocation
       * overly expensive, we won't necessarily find the header here,
       * but would like want to avoid falsely matching an allocation that
       * obviously  contains the header for an std::list at the start of
       * the allocation.  Also, we need to handle the case of a candidate
       * with a single non-null link.
       */
      if (next == prev) {
        /*
         * We would have found the header if either adjacent node were not
         * an allocation or the links did not point to the other node.  At
         * this point we want to try to figure out whether this node is a
         * header or not, with the caveat that it is impossible to tell in
         * some cases, such as for a node that contains only an std::list<T>
         * followed by a T and where the node with the std::list was leaked.
         */
        const AllocationIndex* pFirstIncoming;
        const AllocationIndex* pPastIncoming;
        Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);
        if ((pPastIncoming - pFirstIncoming) > 1 ||
            Base::_graph->GetStaticAnchors(index) != nullptr ||
            Base::_graph->GetStackAnchors(index) != nullptr) {
          Base::_graph->GetIncoming(nextIndex, &pFirstIncoming, &pPastIncoming);
          if ((pPastIncoming - pFirstIncoming) == 1 &&
              Base::_graph->GetStaticAnchors(nextIndex) == nullptr &&
              Base::_graph->GetStackAnchors(nextIndex) == nullptr) {
            /*
             * The other node appears to be reachable only via this one and this
             * one is reachable in other ways.  Let's consider this one to be a
             * header.
             */
            return false;
          }
        }
      } else {
        /*
         * The links are different but any non-null link points to the start
         * of an allocation.
         */
        if (nextAllocation != nullptr) {
          if (prevAllocation != nullptr) {
            const AllocationIndex* pFirstIncoming;
            const AllocationIndex* pPastIncoming;
            Base::_graph->GetIncoming(index, &pFirstIncoming, &pPastIncoming);
            if ((pPastIncoming - pFirstIncoming) > 2 ||
                Base::_graph->GetStaticAnchors(index) != nullptr ||
                Base::_graph->GetStackAnchors(index) != nullptr) {
              Base::_graph->GetIncoming(nextIndex, &pFirstIncoming,
                                        &pPastIncoming);
              if ((pPastIncoming - pFirstIncoming) == 2 &&
                  Base::_graph->GetStaticAnchors(nextIndex) == nullptr &&
                  Base::_graph->GetStackAnchors(nextIndex) == nullptr) {
                /*
                 * The next node appears to be reachable only via this one and
                 * this one is reachable in other ways.  Let's consider this one
                 * to be a header.
                 */
                return false;
              }
              Base::_graph->GetIncoming(prevIndex, &pFirstIncoming,
                                        &pPastIncoming);
              if ((pPastIncoming - pFirstIncoming) == 2 &&
                  Base::_graph->GetStaticAnchors(prevIndex) == nullptr &&
                  Base::_graph->GetStackAnchors(prevIndex) == nullptr) {
                /*
                 * The prev node appears to be reachable only via this one and
                 * this one is reachable in other ways.  Let's consider this one
                 * to be a header.
                 */
                return false;
              }
            }
            /*
             * TODO: we might consider this a header if it is radically
             * different in size from the adjacent nodes and they are similar
             * in size to each other.
             */
            /*
             * TODO: we might consider this a header if the contents differ
             * substantially from the other nodes and they are similar to each
             * other.
             */
          }
        }
      }
    }

    if (context != 0) {
      Commands::Output& output = context->GetOutput();
      output << "This allocation matches pattern DoublyLinkedListNode.\n";
      if (explain) {
        if (header != 0) {
          if (prev == header) {
            if (next == header) {
              output << "This is probably the only node on a circular list.\n";
            } else {
              output << "This is probably the first node on a circular list.\n";
            }
          } else {
            if (next == header) {
              output << "This is probably the last node on a circular list.\n";
            }
          }
        } else {
          Offset firstNode = allocationAddress;
          Offset listNode = prev;
          while (listNode != allocationAddress) {
            AllocationIndex nodeIndex =
                Base::_finder->AllocationIndexOf(listNode);
            if (nodeIndex == numAllocations) {
              header = listNode;
              break;
            }

            const Allocation* nodeAllocation =
                Base::_finder->AllocationAt(nodeIndex);
            if (nodeAllocation->Address() != listNode) {
              header = listNode;
              break;
            }
            firstNode = listNode;
            listNode = reader.ReadOffset(listNode + sizeof(Offset), 0xbad);
            if (listNode == 0xbad) {
              output << "The list appears to be corrupt.\n";
              break;
            }
            if (listNode == 0) {
              output << "The list appears not to be circular.\n";
              output << "The first node on the list is guessed to be at 0x"
                     << std::hex << firstNode << ".\n";
              break;
            }
          }
          if (listNode == allocationAddress) {
            /*
             * We made it around a ring of allocations without figuring out
             * which one was the header.  Try to resolve this by finding
             * an allocation that is more heavily refernced.
             */
            listNode = prev;
            while (listNode != allocationAddress) {
              AllocationIndex nodeIndex =
                  Base::_finder->AllocationIndexOf(listNode);
              const AllocationIndex* pFirstIncoming;
              const AllocationIndex* pPastIncoming;
              Base::_graph->GetIncoming(nodeIndex, &pFirstIncoming,
                                        &pPastIncoming);
              if ((pPastIncoming - pFirstIncoming) > ((next == prev) ? 1 : 2) ||
                  Base::_graph->GetStaticAnchors(nodeIndex) != nullptr ||
                  Base::_graph->GetStackAnchors(nodeIndex) != nullptr) {
                /*
                 * As with other places this trick is employed, it is brittle
                 * due to the possibility of iterators or false edges.
                 */
                header = listNode;
                break;
              }
              listNode = reader.ReadOffset(listNode + sizeof(Offset));
            }
          }
        }
        if (header != 0) {
          output << "The list header appears to be at 0x" << std::hex << header
                 << ".\n";
        }
      }
    }
    return true;
  }
};
}  // namespace chap
