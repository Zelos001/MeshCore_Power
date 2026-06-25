#include <gtest/gtest.h>
#include <cstdint>

// Mirror constants from examples/companion_radio/MyMesh.cpp to guard against
// accidental changes that break the backwards-compatibility guarantee.
#define SEND_TIMEOUT_BASE_MILLIS        500
#define FLOOD_SEND_TIMEOUT_FACTOR       16.0f
#define DIRECT_SEND_PERHOP_FACTOR       6.0f
#define DIRECT_SEND_PERHOP_EXTRA_MILLIS 250

static uint32_t calcFloodTimeout(uint32_t airtime_ms, uint8_t mult) {
  return (uint32_t)((SEND_TIMEOUT_BASE_MILLIS + (FLOOD_SEND_TIMEOUT_FACTOR * airtime_ms)) * mult);
}

static uint32_t calcDirectTimeout(uint32_t airtime_ms, uint8_t path_len, uint8_t mult) {
  uint8_t hops = path_len & 63;
  return (uint32_t)((SEND_TIMEOUT_BASE_MILLIS +
                     ((airtime_ms * DIRECT_SEND_PERHOP_FACTOR + DIRECT_SEND_PERHOP_EXTRA_MILLIS) *
                      (hops + 1))) * mult);
}

// At mult=1, results must be identical to the pre-feature formula.
TEST(AckTimeoutMult, FloodDefaultMultiplierIsUnchanged) {
  EXPECT_EQ(calcFloodTimeout(500, 1),  500 + (uint32_t)(16.0f * 500));
  EXPECT_EQ(calcFloodTimeout(1000, 1), 500 + (uint32_t)(16.0f * 1000));
  EXPECT_EQ(calcFloodTimeout(2600, 1), 500 + (uint32_t)(16.0f * 2600));
}

TEST(AckTimeoutMult, DirectDefaultMultiplierIsUnchanged) {
  // 1-hop path (path_len=1, hops=1, so (hops+1)=2)
  uint32_t expected = 500 + (uint32_t)((1000 * 6.0f + 250) * 2);
  EXPECT_EQ(calcDirectTimeout(1000, 1, 1), expected);
}

// Scaling: mult=N must produce exactly N times the default result.
TEST(AckTimeoutMult, FloodScalesLinearly) {
  for (uint8_t mult = 1; mult <= 10; mult++) {
    uint32_t base = calcFloodTimeout(1000, 1);
    EXPECT_EQ(calcFloodTimeout(1000, mult), base * mult);
  }
}

TEST(AckTimeoutMult, DirectScalesLinearly) {
  for (uint8_t mult = 1; mult <= 10; mult++) {
    uint32_t base = calcDirectTimeout(1000, 2, 1);
    EXPECT_EQ(calcDirectTimeout(1000, 2, mult), base * mult);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
