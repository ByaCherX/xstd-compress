// ---------------------------------------------------------------------------
// page_manager_test.cc — Tests for the bitmap-based PageManager.
// ---------------------------------------------------------------------------
#include <gtest/gtest.h>
#include "page_manager.h"

using namespace xstd;

TEST(PageManagerTest, AllocateReturnsSequentialIds) {
    PageManager pm;
    EXPECT_EQ(pm.Allocate(), 0);
    EXPECT_EQ(pm.Allocate(), 1);
    EXPECT_EQ(pm.Allocate(), 2);
}

TEST(PageManagerTest, AllocatedCountTracked) {
    PageManager pm;
    pm.Allocate();
    pm.Allocate();
    pm.Allocate();
    EXPECT_EQ(pm.AllocatedCount(), 3);
}

TEST(PageManagerTest, IsAllocatedAfterAlloc) {
    PageManager pm;
    int32_t id = pm.Allocate();
    EXPECT_TRUE(pm.IsAllocated(id));
}

TEST(PageManagerTest, FreeReleasesPage) {
    PageManager pm;
    int32_t id = pm.Allocate();
    pm.Free(id);
    EXPECT_FALSE(pm.IsAllocated(id));
    EXPECT_EQ(pm.AllocatedCount(), 0);
}

TEST(PageManagerTest, ReusesFreeSlot) {
    PageManager pm;
    int32_t a = pm.Allocate();
    int32_t b = pm.Allocate();
    pm.Free(a);
    int32_t c = pm.Allocate();   // should reuse slot 'a'
    EXPECT_EQ(c, a);
    (void)b;
}

TEST(PageManagerTest, AllocateAtSpecificId) {
    PageManager pm;
    pm.AllocateAt(5);
    EXPECT_TRUE(pm.IsAllocated(5));
    EXPECT_FALSE(pm.IsAllocated(4));
}

TEST(PageManagerTest, AllocateAtDuplicateThrows) {
    PageManager pm;
    pm.AllocateAt(3);
    EXPECT_THROW(pm.AllocateAt(3), std::runtime_error);
}

TEST(PageManagerTest, GrowsBeyondInitialCapacity) {
    PageManager pm(4);   // only 4 initial slots
    for (int i = 0; i < 100; ++i) pm.Allocate();
    EXPECT_EQ(pm.AllocatedCount(), 100);
}

TEST(PageManagerTest, SerialiseDeserialiseRoundTrip) {
    PageManager src;
    src.Allocate();
    src.Allocate();
    src.Allocate();
    src.Free(1);

    auto bytes = src.Serialise();

    PageManager dst;
    dst.Deserialise(bytes);

    EXPECT_TRUE(dst.IsAllocated(0));
    EXPECT_FALSE(dst.IsAllocated(1));
    EXPECT_TRUE(dst.IsAllocated(2));
}

TEST(PageManagerTest, NegativeIdIsNeverAllocated) {
    PageManager pm;
    EXPECT_FALSE(pm.IsAllocated(-1));
}

TEST(PageManagerTest, FreeNegativeIdNoOp) {
    PageManager pm;
    EXPECT_NO_THROW(pm.Free(-1));
}
