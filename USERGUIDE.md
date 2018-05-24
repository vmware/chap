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
    * [Depending on gdb to Convert Numbers to Symbols](#depending-on-gdb-to-Convert-Numbers-to-symbols)
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
    * [Detecting Memory Corruption](#detecting-memory-corruption)


## Overview
This manual describes how to use `chap` which is a tool that looks at process images and identifies dynamically allocated memory and how it is used.  Although `chap` can detect memory leaks, and should be used as part of any test framework to do so, given the relative infrequency of introduction of leaks to the code, probably the most common usage is as a supplement to gdb to help understand the current usage of various addresses in memory, meaning that anyone who uses gdb at all to debug processes that use any code at all written in C++ or C should probably have chap available as a supplement.  Another major use case is understanding why a process is as large as it is, particularly in the case of growth of containers such as sets, maps, vectors and queues and such but not limited to that.  Those and other use cases are covered later in this manual.

### Where to Run `chap`
At present this has only been tested on Linux, with the `chap` binary built for 64bit x86-64.

### Supported Process Image File Formats
At the time of this writing, the only process image file formats supported by `chap` are little-endian 32 bit ELF cores and little-endian 64 bit ELF cores, both of which are expected to be complete.  Run `chap` without any arguments to get a current list of supported process image file formats.

### Supported Memory Allocators
At present the only memory allocator for which `chap` will be able to find allocations in the process image is the version of malloc used by glibc on Linux.

### How to Start and Stop `chap`
Start `chap` from the command line, with the core file path as the only argument.  Commands will be read by `chap` from standard input, typically one command per line.  Interactive use is terminated by typing ctrl-d to terminate standard input.

### Getting Help
To get a list of the commands, type "help<enter>" from the `chap` prompt.  Doing that will cause `chap` to display a short list of commands to standard output.  From there one can request help on individual commands as described in the initial help message.

### General Concepts
Most of the commands in `chap` operate on sets.  Normally, for any set that is understood by chap one can count it (find out how many there are and possibly get some aggregate value such as the total size), summarize it (provide summary information about the set), list it (give simple information about each member such as address and size), enumerate it (give an identifier, such as an address, for each member) and show it (list each member and dump the contents).  See the help command in chap for a current list of verbs (count, list, show ...) and for which sets are supported for a given verb.

Most of the sets that one can identify with `chap` are related to **allocations**, which roughly correspond to memory ranges made available by memory allocation functions, such as malloc, in response to requests.  Allocations are considered **used** or **free**, where **used** allocations are ones that have not been freed since they last were made available by the allocator.  One can run any of the commands listed above (count, list ...) on **used**, the set of used allocations, **free**, the set of free allocations, or **allocations**, which includes all of them.  If a given type is recognizable by a **signature**, as described in [Allocation Signatures] or by a **pattern**, as described in [Allocation Patterns](#allocation-patterns) Types], one can further restrict any given set to contain only instances of that type. A very small set that is sometimes of interest is "allocation *address*"  which is non-empty only if there is an allocation that contains the given address.  Any specified allocation set can also be restricted in various other ways, such as constraining the size.  Use the help command, for example, "help count used", for details.

Other interesting sets available in `chap` are related to how various allocations are referenced.  For now this document will not provide a through discussion of references to allocations but will briefly address how `chap` understands such references to allocations.  From the perspective of `chap` a reference to an allocation is a pointer-sized value, either in a register or at a pointer-aligned location in memory, that points somewhere within the allocation.  Note that under these rules, `chap` currently often identifies things as references that really aren't, for example, because the given register or memory region is not really currently live.  It is also possible for certain programs, for example ones that put pointers in misaligned places such as in fields of packed structures, but this in general is easy to fix by constraining programs not to do that.  Given an address within an allocation one can look at the **outgoing** allocations (meaning the used allocations referenced by the specified allocation) or the **incoming** allocations (meaning the allocations that reference the specified allocation).  Use the help command, for example, "help list incoming" or "help show exactincoming", or "help summarize outgoing" for details of some of the information one can gather about references to allocations.

References from outside of dynamically allocated memory (for example, from the stack or registers for a thread or from statically allocated memory) are of interest because they help clarify how a given allocation is used.  A used allocation that is directly referenced from outside of dynamically allocated memory is considered to be an **anchor point**, and the reference itself is considered to be an **anchor**.  Any **anchor point** or any used allocation referenced by that **anchor point** is considered to be **anchored**, as are any used allocations referenced by any **anchored** allocations. A **used allocation** that is not **anchored** is considered to be **leaked**.  A **leaked** allocation that is not referenced by any other **leaked** allocation is considered to be **unreferenced**.  Try "help count leaked" or "help summarize unreferenced" for some examples.

Many of the remaining commands are related to redirection of output (try "help redirect") or input (try "help source") or related to trying to reduce the number of commands needed to traverse the graph (try "help enumerate chain").  This will be documented better some time in the next few weeks.  If there is something that you need to understand sooner than that, and the needed information happens not to be available from the help command within chap, feel free to file an issue stating what you would like to be addressed in the documentation.

## Allocations

An *allocation*, from the perspective of `chap` is a contiguous region of virtual memory that was made available to the caller by an allocation function or is currently reserved as writable memory by the process for that purpose.  At present the only allocations recognized by chap are those associated with libc malloc, and so made available to the caller by malloc(), calloc() or realloc() and freed by free() or realloc().  At present, regions of memory made available by other means, such as direct use of mmap(), are not considered allocations.


### Used Allocations
A *used allocation* is an *allocation* that was never given back to the allocator.  From the perspective of `chap`, this explicitly excludes regions of memory that are used for book-keeping about the allocation but does include the region starting from the address returned by the caller and including the full contiguous region that the caller might reasonably modify.  This region may be larger than the size requested at allocation, because the allocation function is always free to return more bytes than were requested.

We can show all the used allocations from `chap`:


### Free Allocations
A *free allocation* is a range of writable memory that can be used to satisfy allocation requests.  It is worthwhile to understand these regions because typically memory is requested from the operating system in multiples of 4K pages, which are subdivided into allocations.  It is more common than not that when an allocation gets freed, it just gets given back to the allocator but the larger region containing that allocation just freed cannot yet be returned to the operating system.

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

### Real References

### False References

### Missing References

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


### Depending on gdb to Convert Numbers to Symbols

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
## Allocation Sets
## Allocation Set Modifications
### Restricting by Signatures or Patterns
### Restricting by Counts of Incoming or Outgoing References
### Set Extensions


## Allocation Patterns

A **pattern** is a way of narrowing the type of an allocation based on the contents of that allocation or based on incoming or outgoing edges from that allocation.  A pattern can be used anywhere a signature can be used, but with a "%" preceding the pattern.  At present the following patterns are supported:
* LongString - dynamically allocated memory for std::string with >= 16 characters
* COWStringBody - old style copy-on-write std::string bodies
* VectorBody - dynamically allocated memory for std::vector
* SSL - SSL type associated with openssl
* SSL_CTX - SSL_CTX type associated with openssl

## Use Cases
### Detecting Memory Leaks
To detect whether a process has exercised any code paths that cause memory leaks, one basically just needs to do the following 3 steps:
1. Gather an image for the process.  For example, one might use gcore to do gather a live core.
2. Open the process image using `chap`.
3. Use **count leaked** to get a counts for the number of leaked allocations and the total number of bytes used by those allocations.

Two caveats to the above are that the quality of the leak check is at most as good as the code coverage leading up to the point at which the process image was gathered, and also some leaks may be missed because allocations may be falsely anchored by false references.

### Analyzing Memory Leaks
### Supplementing gdb
### Analyzing Memory Growth
Generally, the first thing one will want to do in analyzing memory growth is to understand in a very general sense, using various commands from chap, where the memory is used in the process.  Here are some sample commands that will provide this high level information:
* **count used** will tell you how much memory is taken by used allocations.
* **count leaked** will tell you how much memory is taken by leaked allocations, which are a subset of all the used allocations.  This number may be quite small even if the result from **count used** is quite large because the most common cause of growth is generally container growth, which some people consider to be a "logical leak" but is not counted as a leak by `chap`.
* **count free** will tell you how much memory is being used by free allocations.
* **count stacks** will tell you how much memory is used by stacks for threads.  It can be surprising to people but in some cases the default stack sizes are quite large and coupled with a large number of threads the stack usage can dominate.  Even though a stack in one sense shrinks as function calls return, it is common that the entire stack is counted in the committed memory for a process, even if the stack is rather inactive and so has no resident pages.  This distinction matters because when the sum of the committed memory across all the processes gets large enough, even processes that aren't unduly large can be refused the opportunity to grow further, because mmap calls will start failing.

What you do after running some or all of the above commands is determined by which of those numbers are the largest.

TODO: add some examples here.

#### Analyzing Memory Growth Due to Used Allocations
If the results of **count used** suggest that used allocations dominate, probably the next thing you will want to do is to use **redirect on** to redirect output to a file then **summarize used** to get an overall summary of the used allocations.  It is quite common that unsigned allocations will dominate but it can be useful to scan down to the tallies for particular signatures because often one particular count can stand out as being too high and often allocations with the given suspect signature can hold many unsigned allocations in memory, particularly if the class or struct in question has a field that is some sort of collection.  In the special case that the results of **count leaked** are similar to the results of **count used**, one can fall back on techniques for analyzing memory leaks but otherwise one is typically looking for container growth (for example,  a large set or map or queue).

Once one has a theory about the cause of the growth (for example, which container is too large) it is desirable to assess the actual cost of the growth associated with that theory.  For example in the case of a large std::map one might want to understand the cost of the allocations used to represent the std::map, as well as any other objects held in memory by this map.  The best way to do this is often to use the **/extend** switch to attempt to walk a graph of the relevant objects, generally as part of the **summarize** command or the **describe** command.

TODO: Add at least one example here.

#### Analyzing Memory Growth Due to Free allocations

There are definitely cases where free allocations can dominate the memory growth.  This can surprise people because it is not always obvious why, if most of the allocations were freed, the memory wasn't given back to the operating system.  At times it can also appear that the total amount of memory associated with free allocations can be much larger than the total amount of memory that was ever associated with used allocations.  To understand these behaviors in the case of glibc malloc, which at present is the only variant of malloc that `chap` understands, here are some brief explanations and examples.  

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

There is no actual leak associated with the above, but the process memory size can be much larger than one might see without (4) being true, and an observer gets the false impression of unbounded growth because the way in which an arena is selected makes it possible that it may take a very long time before the piggish operation in question uses any particular arena. Each time the piggish operation happens on an arena where it had never happened, that arena grows and so the process grows.

TODO: Provide examples of the specific case where we can find and eliminate the piggish operation (one finding it by looking at free allocations and one gathering a core at the point that the arena grows).

### Detecting Memory Corruption

Due to the fact that allocators use various data structures to keep track of allocation boundaries and free allocations and such, in many cases chap can detect corruption by examining those data structures at startup.  For example, chap can generally detect that someone has overflowed an allocation and can sometimes detect corruption caused by a double free or a use after free.  It doesn't explain how the corruption occurred but does put messages to standard error in the cases that it has detected such corruption.

TODO: Provide examples here.
