// Helix decodes here far faster than on the device, so each frame is charged its device cost.
#include <libhelix-mp3/mp3dec.h>

#include "device_model.h"
#include "emu.h"

extern "C" {
int __real_MP3Decode(HMP3Decoder, unsigned char **, int *, short *, int);
int __wrap_MP3Decode(HMP3Decoder decoder, unsigned char **input, int *left, short *output, int useSize) {
  const int result = __real_MP3Decode(decoder, input, left, output, useSize);
  if (result == ERR_MP3_NONE) {
    MP3FrameInfo info;
    MP3GetLastFrameInfo(decoder, &info);
    emu::chargeUs(info.outputSamps * model::kMp3UsPerSample);
  }
  return result;
}
}
