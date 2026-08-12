#include <SDL3/SDL.h>
#include <stdio.h>

enum STATUS {

  SUCCESS = 0,
  ERR_AUDIO_READ = 1,
  ERR_AUDIO_AVAILABLE = 2,
  SAMPLE_WAIT = 3,
  ERR_DEVICE_SPEC = 4,
};
static int get_device_spec(uint32_t device) {
  SDL_AudioSpec device_spec;
  int sample_frames;

  if (!SDL_GetAudioDeviceFormat(device, &device_spec, &sample_frames)) {
    fprintf(stderr, "SDL_GetAudioDeviceFormat: %s\n", SDL_GetError());
    return ERR_DEVICE_SPEC;
  }

  printf("freq: %d Hz\n", device_spec.freq);
  printf("channels: %d\n", device_spec.channels);
  printf("format: 0x%x\n", device_spec.format);
  printf("sample frames per device buffer: %d\n", sample_frames);

  printf("is float: %d\n", SDL_AUDIO_ISFLOAT(device_spec.format));
  printf("bits/sample: %d\n", SDL_AUDIO_BITSIZE(device_spec.format));
  printf("bytes/sample: %d\n", SDL_AUDIO_BYTESIZE(device_spec.format));
  return SUCCESS;
}

int process(SDL_AudioStream *stream) {
  int available = SDL_GetAudioStreamAvailable(stream);

  if (available < 0) {
    fprintf(stderr, "audio available: %s\n", SDL_GetError());
    return ERR_AUDIO_AVAILABLE;
  }

  int16_t samples[1024];
  if (available < (int)sizeof samples) {
    SDL_Delay(3);
    return SAMPLE_WAIT;
  }

  int bytes = SDL_GetAudioStreamData(stream, samples, sizeof samples);

  if (bytes < 0) {
    fprintf(stderr, "audio read: %s\n", SDL_GetError());
    return ERR_AUDIO_READ;
  }

  int count = bytes / (int)sizeof(int16_t);

  float max = -1.0;
  float min = 1.0;

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

  // printf("bytes=%d count=%d sizeof(samples)=%zu\n", bytes, count,
  //        sizeof samples);

  return SUCCESS;
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
  if (get_device_spec(device) != SUCCESS){
    return 1;
  };

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

  for (;;) {
    int status = process(stream);
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        goto done;
      }
    }

    if (status == ERR_AUDIO_AVAILABLE || status == ERR_AUDIO_READ) {
      break;
    };
  }
done:
  SDL_DestroyAudioStream(stream);
  SDL_Quit();
}
