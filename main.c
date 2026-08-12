#include <SDL3/SDL.h>
#include <stdio.h>

#define CHUNK_SIZE

static int device_data(uint8_t device) {
  SDL_AudioSpec actual;
  int sample_frames;

  if (!SDL_GetAudioDeviceFormat(device, &actual, &sample_frames)) {
    fprintf(stderr, "SDL_GetAudioDeviceFormat: %s\n", SDL_GetError());
    return 1;
  }

  printf("freq: %d Hz\n", actual.freq);
  printf("channels: %d\n", actual.channels);
  printf("format: 0x%x\n", actual.format);
  printf("sample frames per device buffer: %d\n", sample_frames);

  printf("is float: %d\n", SDL_AUDIO_ISFLOAT(actual.format));
  printf("bits/sample: %d\n", SDL_AUDIO_BITSIZE(actual.format));
  printf("bytes/sample: %d\n", SDL_AUDIO_BYTESIZE(actual.format));
  return 0;
}
void process(){

}
int main(void) {
  if (!SDL_Init(SDL_INIT_AUDIO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  SDL_AudioSpec spec = {
      .format = SDL_AUDIO_S16,
      .channels = 1,
      .freq = 48000,
  };

  int c = 0;
  SDL_AudioDeviceID *devices = SDL_GetAudioRecordingDevices(&c);
  if (!devices) {
    fprintf(stderr, "%s\n", SDL_GetError());
    return 1;
  }
  SDL_AudioDeviceID device = devices[0];
  device_data((uint8_t)device);

  SDL_AudioStream *stream =
      SDL_OpenAudioDeviceStream(device, &spec, NULL, NULL);

  if (!stream) {
    fprintf(stderr, "open audio: %s\n", SDL_GetError());
    return 1;
  }

  if (!SDL_ResumeAudioStreamDevice(stream)) {
    fprintf(stderr, "resume audio: %s\n", SDL_GetError());
    return 1;
  }

  // float samples[1024];

  SDL_Event e;

  float max = -1.0;
  float min = 1.0;
  for (;;) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        goto done;
      }
    }

    int available = SDL_GetAudioStreamAvailable(stream);

    if (available < 0) {
      fprintf(stderr, "audio available: %s\n", SDL_GetError());
      break;
    }

    int16_t samples[1024];

    if (available < (int)sizeof samples) {
      SDL_Delay(1);
      continue;
    }

    int bytes = SDL_GetAudioStreamData(stream, samples, sizeof samples);

    if (bytes < 0) {
      fprintf(stderr, "audio read: %s\n", SDL_GetError());
      break;
    }

    if (bytes < 0) {
      fprintf(stderr, "audio read: %s\n", SDL_GetError());
      break;
    }

    int count = (uint32_t)bytes / sizeof(float);

    // printf("bytes=%d count=%d sizeof(samples)=%zu\n", bytes, count,
    //        sizeof samples);

    // there are 1024 samples per buffer (0..1023)
    for (int i = 0; i < count; i++) {
      // samples[i] ranges from -32768..32767, normalize:
      float x = samples[i] / (float)32768.0;
      // this leaves us with a slight asymmetry but it's negligible, and
      // irrelevant for pitch detection

      if (x > max) {
        max = x;
      }
      if (x < min) {
        min = x;
      }

      if (i % 256 == 0) {
        printf("x: %f, min: %f, max: %f\n", x, min, max);
      }
    }
  }
done:
  SDL_DestroyAudioStream(stream);
  SDL_Quit();
}
