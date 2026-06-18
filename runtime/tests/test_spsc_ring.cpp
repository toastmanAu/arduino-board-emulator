#include <gtest/gtest.h>
#include "spsc_ring.h"

#include <cstdint>
#include <vector>

using boardghost::SpscRing;

TEST(SpscRing, DrainEmptyReturnsZero) {
    SpscRing<int16_t, 8> r;
    int16_t out[8];
    EXPECT_EQ(r.drain(out, 8), 0u);
}

TEST(SpscRing, PushThenDrainPreservesFifoOrder) {
    SpscRing<int16_t, 8> r;
    for (int16_t v : {10, 20, 30}) EXPECT_TRUE(r.push(v));
    int16_t out[8] = {0};
    ASSERT_EQ(r.drain(out, 8), 3u);
    EXPECT_EQ(out[0], 10);
    EXPECT_EQ(out[1], 20);
    EXPECT_EQ(out[2], 30);
}

TEST(SpscRing, DrainRespectsMax) {
    SpscRing<int16_t, 8> r;
    for (int16_t v : {1, 2, 3, 4}) r.push(v);
    int16_t out[2] = {0};
    EXPECT_EQ(r.drain(out, 2), 2u);
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 2);
    // The rest remain for the next drain.
    int16_t rest[8] = {0};
    EXPECT_EQ(r.drain(rest, 8), 2u);
    EXPECT_EQ(rest[0], 3);
    EXPECT_EQ(rest[1], 4);
}

TEST(SpscRing, FullDropsNewItems) {
    SpscRing<int16_t, 4> r;  // usable capacity = 3
    EXPECT_TRUE(r.push(1));
    EXPECT_TRUE(r.push(2));
    EXPECT_TRUE(r.push(3));
    EXPECT_FALSE(r.push(4)) << "4th push into a 4-slot ring must drop";
    int16_t out[4] = {0};
    EXPECT_EQ(r.drain(out, 4), 3u);
    EXPECT_EQ(out[2], 3);  // the dropped 4 never made it in
}

TEST(SpscRing, WrapsAroundPreservingOrder) {
    SpscRing<int16_t, 4> r;  // usable capacity = 3
    // Fill, drain part, refill to force write/read indices past the boundary.
    r.push(1); r.push(2); r.push(3);
    int16_t two[2];
    ASSERT_EQ(r.drain(two, 2), 2u);   // removes 1,2 → read idx advances
    EXPECT_TRUE(r.push(4));            // now wraps
    EXPECT_TRUE(r.push(5));
    int16_t out[4] = {0};
    ASSERT_EQ(r.drain(out, 4), 3u);   // 3,4,5 in order
    EXPECT_EQ(out[0], 3);
    EXPECT_EQ(out[1], 4);
    EXPECT_EQ(out[2], 5);
}

TEST(SpscRing, SizeTracksOccupancy) {
    SpscRing<int16_t, 8> r;
    EXPECT_EQ(r.size(), 0u);
    r.push(1); r.push(2);
    EXPECT_EQ(r.size(), 2u);
    int16_t out[1];
    r.drain(out, 1);
    EXPECT_EQ(r.size(), 1u);
}
