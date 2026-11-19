// WWE '13 PC: presented-frame counter (crash_report.cpp's freeze watchdog reads it).
// The native renderer calls Wwe13OnGuestPresent once per guest Present (native/wwe13_native_stubs.cpp).

#include <atomic>
#include <cstdint>

extern std::atomic<uint64_t> g_wwe13_present_count;  // crash_report.cpp

void Wwe13RingLockOnPresent();  // d3d_ring_lock.cpp

void Wwe13OnGuestPresent(uint8_t*) {
  Wwe13RingLockOnPresent();
  g_wwe13_present_count.fetch_add(1, std::memory_order_relaxed);
}
