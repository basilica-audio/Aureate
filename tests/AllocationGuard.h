#pragma once

#include <cstddef>

// Shared instrumentation for the audio-thread allocation regression tests
// (tests/AllocationTests.cpp). A single translation unit
// (AllocationGuard.cpp) globally replaces operator new/delete to count
// allocations while a guard is active; every other test file only needs
// this declaration to construct an AllocationGuard and read its count().
//
// See basilica-audio/Aureate issue #22: no existing test/CI gate caught
// process()-time heap allocation before this - pluginval/auval don't do
// allocation-instrumented profiling, and none of the pre-existing Catch2
// tests had an allocation-counting mechanism. Same pattern as sibling
// plugin overture's tests/AllocationGuard.h (basilica-audio/Overture
// issue #12).
namespace TestAlloc
{
    // Number of operator-new calls observed while any AllocationGuard is
    // alive, since the most recently constructed guard reset it to zero.
    // Not meaningful outside a guard's lifetime.
    std::size_t currentAllocationCount() noexcept;

    // RAII scope: resets the shared allocation counter to zero on
    // construction and stops counting on destruction. Guards are not
    // reentrant/nestable - only construct one at a time (which is all these
    // tests need).
    class AllocationGuard
    {
    public:
        AllocationGuard() noexcept;
        ~AllocationGuard() noexcept;

        AllocationGuard (const AllocationGuard&) = delete;
        AllocationGuard& operator= (const AllocationGuard&) = delete;

        // Allocations counted by the global operator new override since
        // this guard was constructed.
        std::size_t count() const noexcept { return currentAllocationCount(); }
    };
}
