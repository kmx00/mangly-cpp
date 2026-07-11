# plans - overall design of project, subject to change
## mangly-cpp - a tidy and fast cxx header and bundled cli tool for de/remangling itanium cxx ABI mangled names/CSVs 
mangly-cpp should exist as a lightweight cxx header  that can be retrieved easily with CPM or CMakes FetchContent. an existing python library like this (though possibly incomplete) is at `..\mangly` for refrence. we have the added benefit of being able to build dummy targets with several different named symbols to use as a test corpus. 

# development philosophy
development should be test driven through each feature/fix/etc added. development should use feat/fix/tidy/doc branches and prs then face up to (2)  review phases then afterwards merged into master.

every concern addressed during review should have a test (See testing.md) developed for it. 

the header using only cstdlib keeps it extremely portable without introducing some unnecessary overhead wrt how stl is generally emitted in objs (std::format/print etc and others are far too heavy in how they emit code compared to std::printf for example). 
 
# references:
https://github.com/travitch/itanium-abi - An implementation of C++ name mangling for the Itanium ABI
https://medium.com/@bengisu.batmaz/name-mangling-with-itanium-abi-00a5c4dbc3c4 - Name Mangling with Itanium ABI
https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling - Itanium C++ ABI docs on mangling