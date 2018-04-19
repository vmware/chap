# `chap` User's Guide

This manual will eventually describe how to use `chap` which is a tool that looks at process images and identifies dynamically allocated memory and how it is used.  For the next few weeks while this user guide is still being written it is hoped that the help command from within the tool will supplement this documentation.

### Where to Run `chap`.
At present this has only been tested on Linux, with the `chap` binary built for 64bit x86-64.

### Supported process image file formats.
At the time of this writing, the only process image file formats supported by `chap` are little-endian 32 bit ELF cores and little-endian 64 bit ELF cores, both of which are expected to be complete.  Run `chap` without any arguments to get a current list of supported process image file formats.

### Supported memory allocators.
At present the only memory allocator for which `chap` will be able to find allocations in the process image is the version of malloc used by glibc on Linux.

### How to start and stop `chap`
Start `chap` from the command line, with the core file path as the only argument.  Commands will be read by `chap` from standard input.  Interactive use is terminated by typing ctrl-d to terminate standard input.

### Getting help.
To get a list of the commands, type "help<enter>" from the `chap` prompt.  Doing that will cause `chap` to display a short list of commands to standard output.  From there one can request help on individual commands as described in the initial help message.

### General concepts.
Most of the commands in `chap` operate on sets.  Normally, for any set that is understood by chap one can count it (find out how many there are and possibly get some aggregate value such as the total size), summarize it (provide summary information about the set), list it (give simple information about each member such as address and size), enumerate it (give an identifier, such as an address, for each member) and show it (list each member and dump the contents).  See the help command in chap for a current list of verbs (count, list, show ...) and for which sets are supported for a given verb.

Most of the sets that one can identify with `chap` are related to **allocations**, which roughly correspond to memory ranges made available by memory allocation functions, such as malloc, in response to requests.  Allocations are considered **used** or **free**, where **used** allocations are ones that have not been freed since they last were made available by the allocator.  One can run any of the commands listed above (count, list ...) on **used**, the set of used allocations, **free**, the set of free allocations, or **allocations**, which includes all of them.  If a given type is recognizable, as described below in [How Chap Recognizes Allocation Types](#how-chap-recognizes-allocation-types), one can further restrict any given set to contain only instances of that type. A very small set that is sometimes of interest is "allocation *address*"  which is non-empty only if there is an allocation that contains the given address.  Any specified allocation set can also be restricted in various other ways, such as constraining the size.  Use the help command, for example, "help count used", for details.

Other interesting sets available in `chap` are related to how various allocations are referenced.  For now this document will not provide a through discussion of references to allocations but will briefly address how `chap` understands such references to allocations.  From the perspective of `chap` a reference to an allocation is a pointer-sized value, either in a register or at a pointer-aligned location in memory, that points somewhere within the allocation.  Note that under these rules, `chap` currently often identifies things as references that really aren't, for example, because the given register or memory region is not really currently live.  It is also possible for certain programs, for example ones that put pointers in misaligned places such as in fields of packed structures, but this in general is easy to fix by constraining programs not to do that.  Given an address within an allocation one can look at the **outgoing** allocations (meaning the used allocations referenced by the specified allocation) or the **incoming** allocations (meaning the allocations that reference the specified allocation).  Use the help command, for example, "help list incoming" or "help show exactincoming", or "help summarize outgoing" for details of some of the information one can gather about references to allocations.

References from outside of dynamically allocated memory (for example, from the stack or registers for a thread or from statically allocated memory) are of interest because they help clarify how a given allocation is used.  A used allocation that is directly referenced from outside of dynamically allocated memory is considered to be an **anchor point**, and the reference itself is considered to be an **anchor**.  Any **anchor point** or any used allocation referenced by that **anchor point** is considered to be **anchored**, as are any used allocations referenced by any **anchored** allocations. A **used allocation** that is not **anchored** is considered to be **leaked**.  A **leaked** allocation that is not referenced by any other **leaked** allocation is considered to be **unreferenced**.  Try "help count leaked" or "help summarize unreferenced" for some examples.

Many of the remaining commands are related to redirection of output (try "help redirect") or input (try "help source") or related to trying to reduce the number of commands needed to traverse the graph (try "help enumerate chain").  This will be documented better some time in the next few weeks.  If there is something that you need to understand sooner than that, and the needed information happens not to be available from the help command within chap, feel free to file an issue stating what you would like to be addressed in the documentation.

### How Chap Recognizes Allocation Types

In general, `chap` currently has very limited capabilities, for any given allocation, to recognize the type.  In general, the two current forms of identification are via **signatures** and **patterns**.

A **signature** is a pointer to memory that is not writable.   In the case of 'C++' a **signature** might point to a vtable, which can be used to identify the name of a class or struct associated with the given allocation.  A **signature** might also point to a function or a constant string literal.

A **pattern** is a way of narrowing the type of an allocation based on the contents of that allocation or based on incoming or outgoing edges from that allocation.  At present there is just one pattern supported (old style Copy-on-write string bodies) but that will change in the near future to include nodes used in most of the std containers.

#### How Chap Maps Signature Numbers to Names

`chap` has several ways to attempt to map signatures to names.  One is that `chap` will always attempt, using just the core, to follow the signature to a vtable to the typeinfo to the mangled type name and unmangle the name.  Another, if the mangled name is not available in the core, is to use a combination of the core and the associated binaries to obtain the mangled type name.  Another is to create requests for gdb, in a file called _core-path_.symreqs, depend on the user to run that as a script from gdb, and read the results from a file called _core-path_.symdefs.  

##### Finding Class Names and Struct Names from the Core

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
2253 signatures in total were found. ```   

Note that there were still 167 signatures not found.  Those are signatures that fail one of the requirements.

##### Finding Class Names and Struct Names from the Core and binaries

Unless all the signatures have been resolve from the core alone, `chap` will also attempt to use the binaries associated with the core.  In this case, unless the user is running from the same server as where the core was generated, the user is responsible to make sure that the executable and libraries are present in the correct version at the same path where they resided on the server where the core was generated.  It is fine if the binaries or libraries are stripped because `chap` does not depend on the DWARF information for this approach.  At some point the restrictions will be relaxed a bit to allow an alternative root for locating the binaries.  Note also that at present there are no checks that the binaries present are the right versions.

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


##### Depending on gdb to Convert Numbers to Symbols

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
