#pragma once

// Everything the emulator assumes about the Cardputer ADV (ESP32-S3FN8, no PSRAM),
// in one place. MEASURED comes from the firmware's ELF, its sdkconfig or the core
// dump of a real crash. ESTIMATE is a published or typical figure not yet checked
// on real hardware; STATUS on the device shows the real free heap to compare with.
namespace model {

// MEASURED: the heap runs from _heap_start in the ELF to the end of main SRAM
// (0x3FCE9710), plus the 32 KB region at 0x3FCF0000. The real crash dump held live
// pointers up to 0x3FCF750C, which is what proves that second region is in use.
constexpr unsigned kHeapMainEnd = 0x3FCE9710;
constexpr unsigned kHeapExtraRegion = 32 * 1024;
constexpr unsigned kDefaultHeapStart = 0x3FCA7368;  // v0.54 build; the Makefile reads the real one.

// MEASURED sdkconfig: CONFIG_HEAP_POISONING_LIGHT (8 bytes before, 4 after), plus
// TLSF's 4 byte header. Blocks are 4 byte aligned with a 16 byte minimum.
constexpr unsigned kBlockOverhead = 16;
constexpr unsigned kMinBlock = 16;

// ESTIMATE: what the system holds before the firmware's own setup() does anything.
struct SystemBlock { const char *what; unsigned bytes; };
constexpr SystemBlock kSystemBlocks[] = {
    {"idle tasks, stacks 1536 x 2", 3600},
    {"esp_timer task", 3900},
    {"ipc tasks, stacks 1024 x 2", 2600},
    {"default event loop + Arduino event task", 4500},
    {"lwIP tiT task and pools", 6000},
    {"Wi-Fi driver task and static buffers", 42000},  // Static RX 10 x 1.6 KB, TX, mgmt, tasks.
    {"ESP-NOW", 1200},
    {"USB CDC buffers", 1000},
    {"M5Unified speaker task and DMA buffers", 7000},
    {"M5Unified, M5GFX display driver state", 3000},
    {"FATFS mount, SD driver", 5000},
    // CALIBRATED, not measured: with the estimates above, the two actions that crashed Albie's
    // Cardputer bottomed out with a few KB free. The real dumps show bad_alloc on a tiny
    // allocation, so the real figure is lower than the estimates add up to. Reading STATUS on
    // the device replaces this with a measurement.
    {"calibration against the two real crashes", 14000},
};

// MEASURED sdkconfig: CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN 16384, in and out, no
// asymmetric sizing. ESTIMATE: context, and what the handshake holds only briefly.
constexpr unsigned kTlsRecordBuffer = 16384 + 325;
constexpr unsigned kTlsContext = 3000;
constexpr unsigned kTlsHandshakeExtra = 24000;  // Certificates, ECDHE: about 60 KB peak in all.
// ESTIMATE: dynamic Wi-Fi RX/TX buffers and lwIP pbufs a busy secure connection keeps
// in flight (dynamic buffers are 1.6 KB each; the firmware also leaves promiscuous mode on).
constexpr unsigned kTlsTrafficBytes = 8000;
constexpr unsigned kTlsHandshakeMs = 1500;      // Software ECC at 240 MHz, roughly.

// ESTIMATE: an lwIP socket. MEASURED: Arduino's 1436 byte WiFiClient RX buffer and
// the 5760 byte TCP window from sdkconfig; a stream keeps that window full of pbufs.
constexpr unsigned kSocketBytes = 800 + 1436;
constexpr unsigned kTcpWindowBytes = 5760;

// ESTIMATE: an open file: FATFS FIL with its per-file 4 KB sector cache, plus a stdio stream.
constexpr unsigned kOpenFileBytes = 4096 + 600;
constexpr unsigned kSdBytesPerSecond = 2000000;

// ESTIMATE: CPU per MP3 frame. Helix takes about 12% of 240 MHz for 128 kbps at 44.1 kHz stereo.
constexpr unsigned kMp3UsPerSample = 1;  // Per output sample: 2304 samples is about 2.3 ms... of 26 ms.

constexpr unsigned kWatchdogMs = 5000;   // MEASURED sdkconfig: CONFIG_ESP_TASK_WDT_TIMEOUT_S, CPU0 idle.

// x86-64 frames run larger than Xtensa's, so stack high-water only fails past this.
constexpr unsigned kHostStackFactorPercent = 170;

}  // namespace model
