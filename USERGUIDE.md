# `chap` User's Guide

## Table of contents
* [Overview](#overview)
    * [Where to Run `chap`](#where-to-run-chap)
    * [Supported Process Image File Formats](#supported-process-image-file-formats)
    * [Supported Memory Allocators](#supported-memory-allocators)
    * [How to Start and Stop `chap`](#how-to-start-and-stop-chap)
    * [Getting Help](#getting-help)
    * [General Concepts](#general-concepts)
* [Allocations](#allocations)
    * [Used Allocations](#used-allocations)
    * [Free Allocations](#free-allocations)
    * [An Example](#an-example-of-used-and-free-allocations)
* [References](#references)
    * [Real References](#real-references)
    * [False References](#false-references)
    * [Missing References](#missing-references)
* [Allocation Signatures](#allocation-signatures)
    * [Finding Class Names and Struct Names from the Core](#finding-class-names-and-struct-names-from-the-core)
    * [Finding Class Names and Struct Names from the Core and Binaries](#finding-class-names-and-struct-names-from-the-core-and-binaries)
    * [Depending on gdb to Convert Addresses to Symbols](#depending-on-gdb-to-Convert-Numbers-to-symbols)
* [Allocation Patterns](#allocation-patterns)
* [Allocation Sets](#allocation-sets)
* [Allocation Set Modifications](#allocation-set-modifications)
    * [Restricting by Signatures or Patterns](#restricting-by-signatures-or-patterns)
    * [Restricting by Counts of Incoming or Outgoing References](#restricting-by-counts-of-incoming-or-outgoing-references)
    * [Set Extensions](#set-extensions)
* [Use Cases](#use-cases)
    * [Detecting Memory Leaks](#detecting-memory-leaks)
    * [Analyzing Memory Leaks](#analyzing-memory-leaks)
    * [Supplementing gdb](#supplementing-gdb)
    * [Analyzing Memory Growth](#analyzing-memory-growth)
        * [Analyzing Growth Due to Used Allocations](#analyzing-memory-growth-due-to-used-allocations)
        * [Analyzing Growth Due to Free Allocations](#analyzing-memory-growth-due-to-free-allocations)
    * [Detecting Memory Corruption](#detecting-memory-corruption)


## Overview
This manual describes how to use `chap` which is a tool that looks at process images and identifies dynamically allocated memory and how it is used.  Although `chap` can detect memory leaks, and should be used as part of any test framework to do so, given the relative infrequency of introduction of leaks to the code, probably the most common usage is as a supplement to gdb to help understand the current usage of various addresses in memory, meaning that anyone who uses gdb at all to debug processes that use any code at all written in C++ or C should probably have chap available as a supplement.  Another major use case is understanding why a process is as large as it is, particularly in the case of growth of containers such as sets, maps, vectors and queues and such but not limited to that.  Those and other use cases are covered later in this manual.

### Where to Run `chap`
At present this has only been tested on Linux, with the `chap` binary built for 64bit x86-64.

### Supported Process Image File Formats
At the time of this writing, the only process image file formats supported by `chap` are little-endian 32 bit ELF cores and little-endian 64 bit ELF cores, both of which are expected to be complete.  Run `chap` without any arguments to get a current list of supported process image file formats.

### Supported Memory Allocators
At present the only memory allocator for which `chap` will be able to find allocations in the process image is the version of malloc used by glibc on Linux.  Even lacking support for jemalloc or tcmalloc there are many processes for which `chap` is useful, because many processes use glibc malloc, including many processes that are mostly using C++, C, Python, or Java code.  Python, at least on Linux, heavily uses glibc malloc.   The use of glibc malloc in programs written mostly in java is often due to use of shared libraries written in C.

A quick way to determine whether `chap` is likely to be useful for your process is to gather a core (for example, using gcore) then open chap and use **count allocations**.  If the count is non-zero, `chap` is applicable.

```
-bash-4.1$ chap core.33190
chap> count allocations
734 allocations use 0x108900 (1,083,648) bytes.
chap>
```

### How to Start and Stop `chap`
Start `chap` from the command line, with the core file path as the only argument.  Commands will be read by `chap` from standard input, typically one command per line.  Interactive use is terminated by typing ctrl-d to terminate standard input.

### Getting Help
To get a list of the commands, type "help<enter>" from the `chap` prompt.  Doing that will cause `chap` to display a short list of commands to standard output.  From there one can request help on individual commands as described in the initial help message.

### General Concepts
Most of the commands in `chap` operate on sets.  Normally, for any set that is understood by chap one can count it (find out how many there are and possibly get some aggregate value such as the total size), summarize it (provide summary information about the set), list it (give simple information about each member such as address and size), enumerate it (give an identifier, such as an address, for each member) and show it (list each member and dump the contents).  See the help command in chap for a current list of verbs (count, list, show ...) and for which sets are supported for a given verb.

Most of the sets that one can identify with `chap` are related to **allocations**, which roughly correspond to memory ranges made available by memory allocation functions, such as malloc, in response to requests.  Allocations are considered **used** or **free**, where **used** allocations are ones that have not been freed since they last were made available by the allocator.  One can run any of the commands listed above (count, list ...) on **used**, the set of used allocations, **free**, the set of free allocations, or **allocations**, which includes all of them.  If a given type is recognizable by a [signature](#allocation-signatures) or by a [pattern](#allocation-patterns), one can further restrict any given set to contain only instances of that type. A very small set that is sometimes of interest is "allocation *address*"  which is non-empty only if there is an allocation that contains the given address.  Any specified allocation set can also be restricted in various other ways, such as constraining the size.  Use the help command, for example, **help count used**, for details.

Other interesting sets available in `chap` are related to how various allocations are referenced.  For now this document will not provide a thorough discussion of references to allocations but will briefly address how `chap` understands such references to allocations.  From the perspective of `chap` a reference to an allocation is normally a pointer-sized value, either in a register or at a pointer-aligned location in memory, that points somewhere within the allocation.  Note that under these rules, `chap` currently often identifies things as references that really aren't, for example, because the given register or memory region is not really currently live.  It is also possible for certain programs, for example ones that put pointers in misaligned places such as in fields of packed structures, but this in general is easy to fix by constraining programs not to do that.  Given an address within an allocation one can look at the **outgoing** allocations (meaning the used allocations referenced by the specified allocation) or the **incoming** allocations (meaning the allocations that reference the specified allocation).  Use the help command, for example, **help list incoming** or **help show exactincoming**, or **help summarize outgoing** for details of some of the information one can gather about references to allocations.

References from outside of dynamically allocated memory (for example, from the stack or registers for a thread or from statically allocated memory) are of interest because they help clarify how a given allocation is used.  A used allocation that is directly referenced from outside of dynamically allocated memory is considered to be an **anchor point**, and the reference itself is considered to be an **anchor**.  Any **anchor point** or any used allocation referenced by that **anchor point** is considered to be **anchored**, as are any used allocations referenced by any **anchored** allocations. A **used allocation** that is not **anchored** is considered to be **leaked**.  A **leaked** allocation that is not referenced by any other **leaked** allocation is considered to be **unreferenced**.  Try **help count leaked** or **help summarize unreferenced** for some examples.

Many of the remaining commands are related to redirection of output (try **help redirect**) or input (try **help source**) or related to trying to reduce the number of commands needed to traverse the graph (try **help enumerate chain**).  This is being documented rather gradually.  If there is something that you need to understand sooner than that, and the needed information happens not to be available from the help command within chap, feel free to file an issue stating what you would like to be addressed in the documentation.

## Allocations

An **allocation**, from the perspective of `chap` is a contiguous region of virtual memory that was made available to the caller by an allocation function or is currently reserved as writable memory by the process for that purpose.  At present the only allocations recognized by chap are those associated with libc malloc, and so made available to the caller by malloc(), calloc() or realloc() and freed by free() or realloc().  At present, regions of memory made available by other means, such as direct use of mmap(), are not considered allocations.


### Used Allocations
A **used allocation** is an **allocation** that was never given back to the allocator.  From the perspective of `chap`, this explicitly excludes regions of memory that are used for book-keeping about the allocation but does include the region starting from the address returned by the caller and including the full contiguous region that the caller might reasonably modify.  This region may be larger than the size requested at allocation, because the allocation function is always free to return more bytes than were requested.

### Free Allocations
A **free allocation** is a range of writable memory that can be used to satisfy allocation requests.  It is worthwhile to understand these regions because typically memory is requested from the operating system in multiples of 4K pages, which are subdivided into allocations.  It is more common than not that when an allocation gets freed, it just gets given back to the allocator but the larger region containing that allocation just freed cannot yet be returned to the operating system.

### An Example of Used and Free Allocations

Consider the following nonsense code from [here in the test code](https://github.com/vmware/chap/blob/master/test/generators/generic/singleThreaded/OneAllocated/OneAllocated.c), which allocates an int, sets it to 92, and crashes:

```
#include <malloc.h>
int main(int argc, const char**argv) {
   int *pI = (int *) (malloc(sizeof(int)));
   *pI = 92;
   *((int *) 0) = 92; // crash
   return 0;
}
```

If we look at a 64 bit core compiled from this process, we can understand more about the allocations.  We can see that the one used allocation is a bit larger than requested and that at the time the core was generated the glibc malloc was holding on to a single free allocation of size 0x20fe0.  Any of those commands shown (count, list, show) could have been run with any of those arguments (allocations, used, free) but one probably wouldn't want to show that large free allocation to the screen.

```
-bash-4.1$ chap core.48555
chap> count allocations
2 allocations use 0x20fe8 (135,144) bytes.
chap> count used
1 allocations use 0x18 (24) bytes.
chap> count free
1 allocations use 0x20fd0 (135,120) bytes.
chap> list allocations
Used allocation at 601010 of size 18

Free allocation at 601030 of size 20fd0

2 allocations use 0x20fe8 (135,144) bytes.
chap> show used
Used allocation at 601010 of size 18
              5c                0                0

1 allocations use 0x18 (24) bytes.
```

## References

Aside, from allocations, the most important thing to understand about ```chap``` is what it considers to be a **reference**.  Basically a **reference** is an interpretation of any range of memory or contents of a register as a live address to somewhere in that allocation.  In the most straight-forward case this is just a pointer in the native order (which is little-endian everywhere `chap` has been tested.).

### Real References

A real reference is a case where the interpretation is valid and in fact the given register or memory is still in use to reach the given allocation.

### False References

A false reference is where the interpretation is wrong.  This can happen for many reasons.  The most common cause of false references is that the allocator has returned a range of memory that is bigger than what was requested and so the bytes at the end are simply left over from the last time.

Another cause of false references is that some part of the allocation, even in the part of the range actually known to the code that requested the allocation, is simply not currently in use.  This is common with std::vector, which often has a capacity that is larger than the number of elements used or in any similar case where the allocation contains some range of memory that can be used as a buffer but for which the part of that range is actually in use varies.

Another case of false references are when the range just happens to look like it contains a reference but in fact contains one or more smaller values.

### Missing References

A missing reference would be a case where a reference exists but `chap` can't detect it.  One example of this might be if the reference was intentionally obscured by some reversible function.  Fortunately, this is extremely rare on Linux.  This is a good thing because, as will be seen below, accurate leak detection depends on not having any missing references.

## Allocation Signatures

A **signature** is a pointer at the very start of an allocation to memory that is not writable.   In the case of 'C++' a **signature** might point to a vtable, which can be used to identify the name of a class or struct associated with the given allocation.  A **signature** might also point to a function or a constant string literal.  Many commands in chap allow one to use a signature to limit the scope of the command.

`chap` has several ways to attempt to map signatures to names.  One is that `chap` will always attempt, using just the core, to follow the signature to a vtable to the typeinfo to the mangled type name and unmangle the name.  Another, if the mangled name is not available in the core, is to use a combination of the core and the associated binaries to obtain the mangled type name.  Another is to create requests for gdb, in a file called _core-path_.symreqs, depend on the user to run that as a script from gdb, and read the results from a file called _core-path_.symdefs.  

### Finding Class Names and Struct Names from the Core

There is no user intervention required here, but finding the name for any given signature solely based on the contents of the core requires all the following to be true:
* The signature must actually be a vtable pointer (as opposed to a pointer to a function or something else).
* The .data.ro section for the executable or shared library associated with the vtable must be present in the core.
* The .data.ro section for the executable or shared library associated with the typeinfo (usually, but not necessarily the same module as for the vtable) must be present in the core.
* The .rodata section for the executable or shared library associated with the mangled name (expected to be the same module as for the typeinfo) must be present in the core.

You don't have to bother yourself about those details if you don't want to (particularly as there are two other ways to get the signatures) but if you do, you can look into setting /proc/_process-id_/core_dump filter to control how complete the core is.

If you simply want to know whether the class names and struct names have generally been found in the core you can use "summarize signatures" to get general status or use "summarize used" to see for all the signatures found in the file, which ones have associated names.  For example, here is a what the output of "summarize signatures" looks like when most of the mangled type names were available in the core because it shows that 2086 signatures were vtable pointers from the process image.

```
chap> summarize signatures
167 signatures are unwritable addresses pending .symdefs file creation.
2086 signatures are vtable pointers with names from the process image.
2253 signatures in total were found.
```   

Note that there were still 167 signatures not found.  Those are signatures that fail one of the requirements.

### Finding Class Names and Struct Names from the Core and binaries

Unless all the signatures have been resolved from the core alone, `chap` will also attempt to use the binaries associated with the core.  In this case, unless the user is running from the same server as where the core was generated, the user is responsible to make sure that the executable and libraries are present in the correct version at the same path where they resided on the server where the core was generated.  It is fine if the binaries or libraries are stripped because `chap` does not depend on the DWARF information for this approach.  At some point the restrictions will be relaxed a bit to allow an alternative root for locating the binaries.  Note also that at present there are no checks that the binaries present are the right versions.

Finding the name for any given signature solely based on the contents of the core requires all the following to be true (in addition to having the binaries and libraries in the correct place as mentioned above):
* The signature must actually be a vtable pointer (as opposed to a pointer to a function or something else).
* The .data.ro section for the executable or shared library associated with the vtable must be present in the core or must be resolved within the corresponding binary.
* The .data.ro section for the executable or shared library associated with the typeinfo (usually, but not necessarily the same module as for the vtable) must be present in the core or must be resolved within the corresponding binary.
* The .rodata section for the executable or shared library associated with the mangled name (expected to be the same module as for the typeinfo) must be present in the corresponding binary.

If this approach works you will expect to see that most of the signatures are "vtable pointers with names from libraries or executables"
```
chap> summarize signatures
54 signatures are unwritable addresses pending .symdefs file creation.
692 signatures are vtable pointers with names from libraries or executables.
746 signatures in total were found.
```


### Depending on gdb to Convert Addresses to Symbols

In a case where none of the signatures could be found based on the core alone or based on the core and binaries, the output of "summarize signatures" will show a large number of signatures "pending.symdefs file creation" as shown:
```
chap> summarize signatures
1585 signatures are unwritable addresses pending .symdefs file creation.
1585 signatures in total were found.
```

For any **signature** that cannot (because the mangled name is not in the core or the binaries are not available or because the **signature** is not a vtable pointer) chap will add a request to  _core-path_.symreqs.  If you have the symbols associated with the core (for example, as .debug files or unstripped files associated with the main executable and libraries) you can start gdb from the same directory where you started `chap` with suitable command arguments to make the symbols visible.  If you are not sure you have the symbol files set up right, one way to do a quick sanity check from gdb is to use some command like **bt** that depends on the gdb having been started correctly. Once you are satisfied that gdb has been started correctly, you can run "source _core-path_.symreqs" at the gdb prompt to get gdb to create a file called _core-path_.symdefs.  As long as `chap` has not yet read _core-path_.symreqs, it checks for the file at the start of each command.

After you have used gdb to create the .symdefs, you can check using "summarize signatures" and expect to see that most of the signatures are "vtable pointers defined in the .symdefs file":

```
chap> summarize signatures
46 signatures are unwritable addresses missing from the .symdefs file.
1531 signatures are vtable pointers defined in the .symdefs file.
8 signatures are unwritable addresses defined in the .symdefs file.
1585 signatures in total were found.
```

So most of the signatures were vtable pointers for which gdb was able to add definitions to _core-path_.symdefs.  There were also 8 other signatures that gdb understood that corresponded to something other than vtable pointers (probably function pointers).  If you need such symbols it probably makes sense to create the .symdefs even if reading from the core and/or binaries worked.  Another reason you might want to run _core-path_.symreqs from gdb is that it is also used to associate symbols with static anchors, but again you might not care about that because typically the number of static anchors that are of interest for any given run of `chap` is rather small.

Just as a reminder, if you use this method you must make sure when gdb runs that it sees the correct binaries or debug files.  If gdb is not set up correctly at the time you create _core-path_.symdefs, chap will report that most or all of the signatures are "unwritable addresses missing from the .symdefs file":

```
chap> summarize signatures
1585 signatures are unwritable addresses missing from the .symdefs file.
1585 signatures in total were found.
```

## Allocation Patterns

A **pattern** is a way of narrowing the type of an allocation based on the contents of that allocation or based on incoming or outgoing edges from that allocation.  A pattern can be used anywhere a signature can be used, but with a "%" preceding the pattern.  At present the following patterns are supported:
* LongString - dynamically allocated memory for std::string with >= 16 characters
* COWStringBody - old style copy-on-write std::string bodies
* VectorBody - dynamically allocated memory for std::vector
* SSL - SSL type associated with openssl
* SSL_CTX - SSL_CTX type associated with openssl

## Allocation Sets

`chap` commands operate on sets of allocations.  The simplest of the sets are based on whether the allocations are used or free and for the used ones whether they are anchored or leaked, and for the anchored allocations, whether or not they are allocated in a certain way.  For all of these sets, the allocations are visited in increasing order of address.  Here is a rough hierarchy:

* **allocations** refers to the set of all allocations, used or free.
  * **used** refers to the subset of **allocations** consisting of allocations that have been returned by an allocation call such as malloc() but not yet been freed.
    * **anchored** refers the subset of **used** consisting of allocations that are reachable by following from a reference outside of dynamically allocated memory (for example, from a stack or a register or statically allocated memory) to an allocation then following one or more references starting from that allocation.
      * **anchorpoints** refers to the subset of **anchored** that is referenced from outside of dynamically allocated memory.
        * **stackanchorpoints** refers to the subset of **anchorpoints** that is referenced from the stack for at least one thread.
        * **registeranchorpoints** refers to the subset of **anchorpoints** that is referenced from at least one register for at least on thread.
        * **staticanchorpoints** refers to the subset of **anchorpoints** that is referenced from at least one region considered to be statically allocated.  Not that one should be somewhat skeptical of this set because `chap` generally considers writable memory that it cannot otherwise explain to be statically allocated.  Some cases where this is incorrect includes memory used for python objects or java objects or memory otherwise dynamically allocated using mmap.  The reason `chap` behaves this way is to avoid reporting false leaks.
        * **threadonlyanchorpoints** refers to the subset of **anchorpoints** directly referenced by registers or stack for at least one thread but not anchored in any other way.
      * **stackanchored** refers to the subset of **anchored** that is reachable by following zero or more references from a stack anchor point.
      * **registeranchored** refers to the subset of **anchored** that is reachable by following zero or more references from a register anchor point.
      * **staticanchored** refers to the subset of **anchored** that is reachable by following zero or more references from a static anchor point.
      * **threadonlyanchored** refers to the subset of **anchored** that is reachable by following zero or more references from a member of **threadonlyanchored** minus the members of **stackanchored**.  This is useful for understanding temporary allocations.
  * **leaked** refers to the subset of **used** that is not **anchored**.
    * **unreferenced** refers to the subset of **leaked** that are not referenced by any other members of **leaked**.
* **free** refers to the subset of **allocations** that are not **used**.

Other sets are defined relative to a particular allocation:

* **allocation** *address* refers to the allocation that contains the given address.  It is an error here and for any of the sets described in this section to specify an address that does not belong to an allocation.
* **outgoing** *address* refers to the subset of **used** that is referenced by the allocation that contains the given address, specifically excluding that allocation.
* **incoming** *address* refers to the subset of **used** that has at least one reference to the allocation that contains the given address, again specifically excluding that allocation.
  * **exactincoming** *address* refers to the subset of **incoming** *address* that has a reference to the start of the allocation containing the given address.
* **freeoutgoing** *address* refers to the subset of **free** that has at least one reference to the allocation that contains the given address, again specifically excluding that allocation.
* **chain** *address* *offset-in-hex* refers to the subset of **used** starting at the
allocation containing the specified address and following links at the given
offset until the link offset doesn't fit in the allocation or the target is not
in an allocation.  Note that unlike most of the sets, allocations are visited in the order of the chain.  The **chain** set specification was defined before the notion of set extensions described below, and is deprecated but is kept for backwards compatibility with existing chap scripts.
* **reversechain** *address-in-hex* *source-offset* *target-offset* refers to the subset of **used** starting at the allocation containing the specified address and following incoming edges that are constrained so that the reference is at the specified offset in the source and points to the specified offset in the target. This is intended for following long singly linked lists backwards.  The chain is terminated either when no suitable incoming edge exists or when multiple such edges do.

## Allocation Set Modifications

Any of the allocation sets as describe above can be further restricted or, if the set does not already match **allocations** can generally be extended.

### Restricting by Signatures or Patterns

One way to restrict a set is to provide a signature or a pattern following the set specification.  Here is a `chap` script with a few examples:

```
# Provide the addresses of all allocations that, based on the signature, appear to be of type Foo:
enumerate used Foo

# Show all the leaked allocations that appear to be instances of SSL_CTX from openssl.
show leaked %SSL_CTX

```

### Restricting by Counts of Incoming or Outgoing References

Sets can be further constrained by requiring a minimum or maximum number of incoming or outgoing references possibly constraining the type of the referencing or referenced object.  For example here is a script with some commands preceded by comments about what they do:

```
# This possibly useless command counts all the leaked allocations that are not
# also unreferenced.
count leaked /minincoming=1

# This overly verbose command is equivalent to "count unreferenced".
count leaked /maxincoming=0

# This describes all used allocations of type Foo that are referenced by at
# least 100 allocations of type Bar.

describe used Foo /minincoming Bar=100
```

### Set Extensions

Any sets created in the above manner can be created by applying one or more **/extend** switches.  Each **/extend** switch takes a single extension rule specification argument and declares an **extension rule**.  The **declaration order** of an **extension rule** is just the order in which the corresponding **/extend** switch appeared in the given chap command.

To understand the extension rules it is helpful to understand the general notion of set extensions.  Basically, if any **/extend** switch is present, as the members of the base set are visited for the first time in the order associated with that base set (often in increasing order of allocation address but sometimes in other orders for some outlying set-specifications mentioned earlier in this guide) the extension rules are visited in **declaration order** to see which ones apply to the given member of the base set.  Any given extension rule may add one or more new members to the set (adjacent to the given member).  Extension rules are applied at most once to any given member, including both members of the base allocation set or allocations added via an extension rule.  Note that the traversal is DFS, in the sense that allocation rules are applied to the most newly added member first.

An extension rule specification has the following parts, some optional as indicated by [],  in the given order (but without any embedded blanks):

[*member-constraints*] *direction* [*extension-constraints*] [**=>** *new-extension-state*]

The *member-constraints*, which may be omitted, are checked as an allocation in the set is being visited for the first time during the command.  If the *member-constraints* are not satisfied, either for the given member or for the offset within the member, the extension will not be applied.  More detail on *member-constraints* will be given later.  

The *direction*, which is always present and currently must be either **->** or **<-** determines, when one is visiting an allocation for the first time, whether the objects to be added to the set are ones referenced by that allocation (**->**) or ones that reference that allocation (**<-**).

The *extension-constraints*, which may be omitted, apply to allocations in the specified *direction* from the member being visited for the first time.  If the *extension-constraints* are not satisfied, either for the allocation under consideration for extending the set or for the offset within that allocation, the given extension candidate will not be added to the set.

The *new-extension-state* gives a name to the extension state used for evaluating the *member-constraints*.  For any member of the original set being visited for the first time, the default extension state always applies.  For an extension being visited for the first time, the extension state by the given name applies or the default extension state applies if none is specified.

The *member-constraints*, if present, have the following parts:

[ *extension-state* | *signature* | *pattern* ][ ``@`` *offset-in-hex*]

If an *extension-state* is supplied, the given *extension-rule* is applicable only in that *extension-state*.  If a *signature* or *pattern* is supplied, extensions can only be applied to members of the set that have that *signature* or *pattern*.  The meaning of the offset depends on the specified *direction*.  If the *direction* is **->** the reference from the member allocation must appear at that offset in the member allocation.  If the *direction* is **<-** the reference from the extension candidate to the member allocation must refer to that offset in the member allocation.

The *extension-constraints*, if present, have the following parts:

[ *signature* | *pattern* ][``@`` *offset-in-hex*]

If a *signature* or *pattern* is supplied, allocations reachable from the given member by traversing in the specified *direction* can be added only if they match that *signature* or *pattern*.  The meaning of the offset depends on the specified *direction*.   If the *direction* is **->** the reference from the member allocation must refer to the specified offset in the extension candidate.  If the *direction* is **<-** the reference to the member allocation must appear at the specified offset in the extension candidate.

Some examples:

```
# Count the set formed by starting at the allocation containing address 0x7ffbe0840 then traversing to
# anything reachable by traversing one or more outgoing edges.
count allocation 7ffbe0840 /extend ->

# List the given allocation and allocations that it references directly.  The reason the traversal
# stops is that the extension rule as defined applies only in the default state.
list allocation 7ffbe0840 /extend ->=>StopHere

# List the given allocation and anything reachable from it by traversing up to 2 outgoing references.
list allocation 7ffbe0840 /extend ->=>Out1 /extend Out1->=>Out2

# Show all anchored allocations, but reflecting the way they are reached from the anchor points rather
# than in increasing order of address.
show anchored /extend ->

# For every object that matches signature Foo, describe it and any adjacent allocations that match the
# pattern %COWStringBody.
describe used Foo /extend Foo->%COWStringBody

# This is almost the same as the previous command, but as a way to help avoid false edges, insist that any
# reference from an instance of Foo to an instance of %COWStringBody must point to offset 0x18 of that
# %COWStringBody.  This hard-coded offset would be for a 64-bit process.
describe used Foo /extend Foo->%COWStringBody@18

# Summarize the set with every allocation that matches the pattern %SSL_CTX and every allocation that
# points to the start of an allocation that matches the pattern %SSL_CTX.
summarize used %SSL_CTX /extend %SSL_CTX@0<-

# List every instance of Foo that is referenced by at least 100 instances of Bar, as well as all the
# referencing instances of Bar.
list Foo /minincoming Bar=100 /extend Foo<-Bar

```


## Use Cases
### Detecting Memory Leaks
To detect whether a process has exercised any code paths that cause memory leaks, one basically just needs to do the following 3 steps:
1. Gather an image for the process.  For example, one might use gcore to do gather a live core.
2. Open the process image using `chap`.
3. Use **count leaked** to get a counts for the number of leaked allocations and the total number of bytes used by those allocations.
4. If you are reporting these leaks to someone else, they'll need the core file as well as enough information to get proper symbols (e.g., build number).  You can use **show leaked** to see what the leaked allocations actually looked like or perhaps **describe leaked** to check if any of the unsigned leaked allocations match some known pattern.  You might want to report the results of **show unreferenced** as well.

Two caveats to the above are that the quality of the leak check is at most as good as the code coverage leading up to the point at which the process image was gathered, and also some leaks may be missed because allocations may be falsely anchored by false references.

### Analyzing Memory Leaks

To analyze memory leaks, starting from a core for which **count leaks** gave a non-zero count, probably the first thing you will want to do (assuming that you have set up symbols properly as describe in an earlier section) is to distinguish which leaked allocations are also **unreferenced**, because if one can explain why all the **unreferenced** objects were leaked, this will often explain the remaining leaked objects.  To start with this, use something like **show unreferenced**.

One of the most common causes of leaks is a failure to do the last dereference on a reference counted object (or failing to take a reference in the first place and allowing any raw pointers to the object to go out of scope).  For such objects you basically want to figure out the type, which `chap` might help you with based on a **signature** or a **pattern** then use gdb or some such thing to figure out where the reference count resides if you don't already know.

### Supplementing gdb

When you look at a core with gdb it is very desirable to know the how various addresses seen in registers on the stack are used.  Try **describe** *address* to get an understanding of whether the given address points to a used allocation or a free allocation or stack or something else.  If the address corresponds to a used allocation, use **list incoming** *allocation-address* to understand whether that allocation is referenced elsewhere.

### Analyzing Memory Growth
Generally, the first thing one will want to do in analyzing memory growth is to understand in a very general sense, using various commands from chap, where the memory is used in the process.  Here are some sample commands that will provide this high level information:
* **count writable** will tell you how much writable memory was being used by the process.
* **count used** will tell you how much memory is taken by used allocations.
* **count leaked** will tell you how much memory is taken by leaked allocations, which are a subset of all the used allocations.  This number may be quite small even if the result from **count used** is quite large because the most common cause of growth is generally container growth, which some people consider to be a "logical leak" but is not counted as a leak by `chap`.
* **count free** will tell you how much memory is being used by free allocations.
* **count stacks** will tell you how much memory is used by stacks for threads.  It can be surprising to people but in some cases the default stack sizes are quite large and coupled with a large number of threads the stack usage can dominate.  Even though a stack in one sense shrinks as function calls return, it is common that the entire stack is counted in the committed memory for a process, even if the stack is rather inactive and so has no resident pages.  This distinction matters because when the sum of the committed memory across all the processes gets large enough, even processes that aren't unduly large can be refused the opportunity to grow further, because mmap calls will start failing.


TODO: add some examples here.

#### Analyzing Memory Growth Due to Used Allocations
If the results of **count writable** and **count used** suggest that used allocations occupy most of the writable memory, probably the next thing you will want to do is to make sure that chap is set up properly to handle named signatures, as described [here](#allocation-signatures) then use **redirect on** to redirect output to a file then **summarize used** to get an overall summary of the used allocations.  It is quite common that unsigned allocations will dominate but it can be useful to scan down to the tallies for particular signatures because often one particular count can stand out as being too high and often allocations with the given suspect signature can hold many unsigned allocations in memory, particularly if the class or struct in question has a field that is some sort of collection.  In the special case that the results of **count leaked** are similar to the results of **count used**, one can fall back on techniques for analyzing memory leaks but otherwise one is typically looking for container growth (for example,  a large set or map or queue).

Once one has a theory about the cause of the growth (for example, which container is too large) it is desirable to assess the actual cost of the growth associated with that theory.  For example in the case of a large std::map one might want to understand the cost of the allocations used to represent the std::map, as well as any other objects held in memory by this map.  The best way to do this is often to use the **/extend** switch to attempt to walk a graph of the relevant objects, generally as part of the **summarize** command or the **describe** command.

TODO: Add at least one example here.

#### Analyzing Memory Growth Due to Free allocations

There are definitely cases where free allocations can occupy most of the writable memory.  This can surprise people because it is not always obvious why, if most of the allocations were freed, the memory wasn't given back to the operating system.  At times it can also appear that the total amount of memory associated with free allocations can be much larger than the total amount of memory that was ever associated with used allocations.  To understand these behaviors in the case of glibc malloc, which at present is the only variant of malloc that `chap` understands, here are some brief explanations and examples.  

glibc malloc grabs memory from the operating system in multiple 4k pages at a time and typically carves those pages up into smaller allocations. A given page cannot be returned to the operating system as long as any allocations are present on that page and in fact the restrictions are much worse than that, as I will describe below. The key point here is that those pages that have not been given back to the operating systems can be used to satisfy subsequent requests and that the process will not grow in such cases where part of an existing page can be used to satisfy the request.

TODO: put an example here with sample code from a single threaded process here that leaves one allocation on the last page.

glibc malloc has the notion of an "arena", which loosely is to allow different threads to allocate memory without waiting for each other, by allocating memory from different arenas. An arena has the characteristic that an allocation from that arena must be returned to that arena. When a thread calls malloc, libc malloc attempts to find a free (not locked by another thread) arena and creates a new arena if no arena is currently unlocked. Once it has selected an arena for a given malloc call, which it does without regard to how much free memory is associated with that arena, it will use that arena for the duration of that single malloc call, even if using that arena forces the process to grow and using a different arena would not.

TODO: put an example here with multiple arenas

The arenas other than the main arena, which is the one arena that is initialized as of the first malloc, have the characteristic that they are divided into 64MB heaps and no memory within a heap that precedes the last used allocation within that heap can be freed. This would even be true with a single allocation in the very last page of that heap and nothing actually still in use in that heap before that last allocation. The main arena has plenty of issues where an allocation on one page can prevent others from being freed, but those are more difficult to describe here.

As a performance optimization, glibc malloc remembers the last arena used by a given thread, and attempts to use it on the next allocation request for that thread, as long as the arena is not already locked by some other thread. The reason that in many cases this improves performances is that if you have k threads and k arenas, the process can reach a steady state where each thread is effectively using a different arena and so there are no collisions. In practice the number of arenas is typically smaller than the number of threads because a new arena is allocated only if no unlocked arena can be found.

TODO: put another example here.  Perhaps one of the demos from the talk.

Now to understand a particularly bad case that can occur given the above constraints, consider the case where we have had sufficient collisions on malloc that we have roughly 20 arenas. Consider the case that there is some operation that happens on a single thread and uses a large amount of memory on a temporary basis (for example to store the results of a large reply from a request to a server). When the operation is finished, that memory has been returned to malloc but not to the operating system, for reasons along the line of what I mentioned above.

In such a situation, favoring the last arena used by a given thread can become a big problem. For the sake of specific numbers, suppose the operation uses 40MB, mostly on a temporary basis and gets "lucky" so that each time it makes a request, the previously used arena is already unlocked. All the allocations will happen on that arena in such a case, until the given thread sees a collision on a malloc request, which at times may not happen for the duration of the entire operation. In the worst case, which is not uncommon, even though much of the 40MB get free they only get returned to the arena, and the pages don't go back to the operating system.

Suppose that same operation later happens on another thread that, based on the last allocation done by that thread attempts and succeeds to use a different arena throughout the course of that operation. Potentially that thread can cause the process to grow by 40MB to satisfy requests on this other arena, even though the arena used the previous time the operation occurred already has 40MB free. I am oversimplifying a bit but one can see that if this should eventually happen on all 20 of the arenas, the overhead in free allocations might approach 20 * 40MB = 800 MB.

There is no actual leak associated with the above, but the process memory size can be much larger than one might see without the libc characteristic of preferring to use the arena must recently used by the current thread, and an observer gets the false impression of unbounded growth because the way in which an arena is selected makes it possible that it may take a very long time before the piggish operation in question uses any particular arena. Each time the piggish operation happens on an arena where it had never happened, that arena grows and so the process grows.  The **describe allocation** command can add insight into this situation because it allows one to easily spot a discrepancy in the sizes of the arenas or in the number of bytes used by free allocations associated with each arena.

TODO: Provide examples of the specific case where we can find and eliminate the piggish operation (one finding it by looking at free allocations and one gathering a core at the point that the arena grows).

### Detecting Memory Corruption

Due to the fact that allocators use various data structures to keep track of allocation boundaries and free allocations and such, in many cases chap can detect corruption by examining those data structures at startup.  For example, chap can generally detect that someone has overflowed an allocation and can sometimes detect corruption caused by a double free or a use after free.  It doesn't explain how the corruption occurred but does put messages to standard error in the cases that it has detected such corruption.

TODO: Provide examples here.
