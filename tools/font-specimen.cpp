// Draws every candidate font in firmware/lab/font_samples.h with the real M5GFX, on a sprite the size of
// the screen. Measures what fits and how tall the letters really are, and writes each screen as raw
// RGB565 for tools/font-specimen.py. It says nothing about how readable a font is on the glass.
#include <M5GFX.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "font_samples.h"

// The Arduino core links these in; nothing here runs on a clock or prints numbers through String.
unsigned long millis() { return 0; }
extern "C" char *ltoa(long value, char *out, int radix);
extern "C" char *ultoa(unsigned long value, char *out, int radix);
extern "C" char *itoa(int value, char *out, int radix) { return ltoa(value, out, radix); }
extern "C" char *utoa(unsigned value, char *out, int radix) { return ultoa(value, out, radix); }

namespace {
// A 1.14 inch diagonal over 240x135, from the panel's published size: not measured on a device.
const double kPixelMm = 1.14 * 25.4 / std::sqrt(240.0 * 240.0 + 135.0 * 135.0);
constexpr double kViewingMm = 300;

// The inked height of one letter, counted from the pixels drawn.
int inkedHeight(lgfx::LGFX_Sprite &sprite, const fontlab::Sample &sample, const char *letter) {
  sprite.fillScreen(TFT_BLACK);
  fontlab::useSample(sprite, sample);
  sprite.setTextColor(TFT_WHITE, TFT_BLACK);
  sprite.setCursor(10, 10);
  sprite.print(letter);
  int top = 999, bottom = -1;
  for (int y = 0; y < 135; y++) for (int x = 0; x < 240; x++) {
    if (sprite.readPixel(x, y) != 0) { top = std::min(top, y); bottom = std::max(bottom, y); }
  }
  return bottom < 0 ? 0 : bottom - top + 1;
}

// Which of French's letters the font has. Font0 is mapped to CP437 by the firmware, so it has them.
std::string missingAccents(const fontlab::Sample &sample) {
  if (sample.cp437) return "";
  if (sample.byteFont) return "wrong letters: see the sheet";
  static const uint16_t codes[] = {0xE0, 0xE2, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEE, 0xEF, 0xF4, 0xF9, 0xFB, 0xFC, 0xC9};
  static const char *names[] = {"à", "â", "ç", "è", "é", "ê", "ë", "î", "ï", "ô", "ù", "û", "ü", "É"};
  lgfx::FontMetrics metrics;
  std::string missing;
  for (size_t index = 0; index < sizeof(codes) / sizeof(codes[0]); index++)
    if (!sample.font->updateFontMetric(&metrics, codes[index])) missing += names[index];
  return missing;
}
}  // namespace

int main(int argc, char **argv) {
  const std::string folder = argc > 1 ? argv[1] : ".";
  lgfx::LGFX_Sprite sprite;
  sprite.setColorDepth(16);
  if (!sprite.createSprite(240, 135)) { fprintf(stderr, "no sprite\n"); return 1; }
  printf("font\tcap_px\tcap_mm\tarcmin_at_30cm\tx_px\tline_px\tcolumns\tlines\tlongest_label_px\tmissing_accents\n");
  for (int index = 0; index < fontlab::kSampleCount; index++) {
    const fontlab::Sample &sample = fontlab::kSamples[index];
    const int cap = inkedHeight(sprite, sample, "H");
    const int small = inkedHeight(sprite, sample, "x");
    fontlab::useSample(sprite, sample);
    const int lineHeight = sprite.fontHeight() + 2;
    const int columns = (240 - 6) / sprite.textWidth("n");
    const int lines = (135 - fontlab::kHeaderHeight - 3 + 2) / lineHeight;
    const int label = sprite.textWidth("CHASSE AU RENARD");
    const double capMm = cap * kPixelMm;
    const double arcmin = 2 * std::atan(capMm / 2 / kViewingMm) * 180 / M_PI * 60;
    printf("%s\t%d\t%.2f\t%.1f\t%d\t%d\t%d\t%d\t%d\t%s\n", sample.name, cap, capMm, arcmin, small, lineHeight, columns, lines, label,
           missingAccents(sample).c_str());
    fontlab::drawSample(sprite, index);
    char path[512];
    snprintf(path, sizeof(path), "%s/%02d.rgb565", folder.c_str(), index);
    FILE *file = fopen(path, "wb");
    if (!file) { fprintf(stderr, "cannot write %s\n", path); return 1; }
    fwrite(sprite.getBuffer(), 2, 240 * 135, file);  // Byte-swapped, as LovyanGFX keeps 16 bit sprites.
    fclose(file);
  }
  return 0;
}
