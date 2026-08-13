#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_FREQUENCY 48000
#define MIN_DEPTH_TO_MATCH 1e-4
#define NOISE_GATE_RMS 0.004

typedef struct {
  int idx;
  double value;
} Err;

enum {
  SUCCESS = 0,
  ERR_AUDIO_READ = 1,
  ERR_AUDIO_AVAILABLE = 2,
  SAMPLE_WAIT = 3,
  ERR_DEVICE_SPEC = 4,
  ERR_BAD_FREQ = 5,
};

enum {
  max_dt = 2400, // 20 Hz
  scope = 5,
  chunk_size = 1024,
  chunks_per_buffer = 8,
  buffer_size = chunk_size * chunks_per_buffer,
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

void ingest_next_chunk(
    int16_t *buffer,
    const int16_t *samples,
    double *error_total
) {
  const double scale = 1.0 / 32768.0;

  /*
   * Remove the pair errors belonging to the chunk
   * which is about to fall off the front.
   */
  for (int t = 1; t <= max_dt; t++) {
    for (int i = 0; i < chunk_size; i++) {
      int difference =
          abs(buffer[i + t] - buffer[i]);

      error_total[t] -= difference;
    }
  }

  memmove(
      buffer,
      buffer + chunk_size,
      (buffer_size - chunk_size) * sizeof(buffer[0])
  );

  memcpy(
      buffer + (buffer_size - chunk_size),
      samples,
      chunk_size * sizeof(buffer[0])
  );

  /*
   * Add the pair errors introduced by the new chunk.
   *
   * For each lag t, these are exactly the pairs whose
   * second sample lies inside the newly inserted chunk.
   */
  for (int t = 1; t <= max_dt; t++) {
    int start = buffer_size - chunk_size - t;
    int end = buffer_size - t;

    for (int i = start; i < end; i++) {
      double difference =
          fabs((double)buffer[i + t] - (double)buffer[i]) * scale;

      error_total[t] += difference;
    }
  }
}

void print_diff(const int t, double difference) {
  const int scale = 10000;

  int magnitude = (int)fabs(scale * difference);
  int middle = 100;

  int pos_diff = (difference > 0) ? magnitude : 0;
  int neg_diff = (difference < 0) ? magnitude : 0;

  for (int i = 0; i < middle - neg_diff; i++) {
    putchar(' ');
  }

  for (int i = 0; i < neg_diff; i++) {
    putchar('#');
  }

  putchar('|');
  printf("%d", t);
  putchar('|');

  for (int i = 0; i < pos_diff; i++) {
    putchar('#');
  }

  putchar('\n');
}

double get_rms(const int16_t *buffer) {
  double total = 0.0;

  for (int i = 0; i < buffer_size; i++) {
    double sample = buffer[i] / 32768.0;
    total += sample * sample;
  }

  return sqrt(total / (float)buffer_size);
}

bool is_local_min(const double *lag_err, int i, int scope) {
  if (i - scope < 0 || i + scope > max_dt) {
    return false;
  }

  for (int j = i - scope; j <= i + scope; j++) {
    if (j == i) {
      continue;
    }

    if (lag_err[j] < lag_err[i]) {
      return false;
    }
  }

  return true;
}

bool matches_close_enough(const double *lag_err, int i, int scope) {
  if (!is_local_min(lag_err, i, scope)) {
    return false;
  }

  double surrounding_total = 0.0;
  int count = 0;

  for (int j = i - scope; j <= i + scope; j++) {
    if (j == i) {
      continue;
    }

    surrounding_total += lag_err[j];
    count++;
  }

  double surrounding_mean = surrounding_total / count;
  double check_depth = surrounding_mean - lag_err[i];

  return check_depth > MIN_DEPTH_TO_MATCH;
}

int find_period(const double *lag_err) {
  Err prev_match = {-1, 0.0};
  int dt = 0;

  for (int i = 0; i <= max_dt; i++) {
    if (!matches_close_enough(lag_err, i, scope)) {
      continue;
    }

    if (prev_match.idx != -1) {
      dt = i - prev_match.idx;
      printf("\n=======================\nMATCH: dt = %d\n", dt);
    }

    prev_match = (Err){i, lag_err[i]};
  }

  if (dt <= 0 || dt > max_dt) {
    return 0;
  }

  return dt;
}

int get_freq_hz(
    const int16_t *buffer,
    const double *error_total
) {
  double rms = get_rms(buffer);

  if (rms < NOISE_GATE_RMS) {
    return ERR_BAD_FREQ;
  }

  double lag_err[max_dt + 1];

  lag_err[0] = 0.0;

  for (int t = 1; t <= max_dt; t++) {
    lag_err[t] = error_total[t] / (buffer_size - t);
  }

  int dt = find_period(lag_err);

  if (dt == 0) {
    return ERR_BAD_FREQ;
  }

  return (int)((double)SAMPLE_FREQUENCY / dt);
}

int process(SDL_AudioStream *stream) {
  int available = SDL_GetAudioStreamAvailable(stream);

  if (available < 0) {
    fprintf(stderr, "audio available: %s\n", SDL_GetError());
    return ERR_AUDIO_AVAILABLE;
  }

  int16_t samples[chunk_size] = {0};

  int available_chunks =
      (int)((size_t)available / sizeof samples);

  if (available_chunks > 3) {
    printf("backlog: %d chunks\n", available_chunks);
  }

  if (available < (int)sizeof samples) {
    SDL_Delay(1);
    return SAMPLE_WAIT;
  }

  int bytes =
      SDL_GetAudioStreamData(stream, samples, sizeof samples);

  if (bytes < 0) {
    fprintf(stderr, "audio read: %s\n", SDL_GetError());
    return ERR_AUDIO_READ;
  }

  static int16_t buffer[buffer_size] = {0};
  static double error_total[max_dt + 1] = {0};

  uint64_t start = SDL_GetTicksNS();

  ingest_next_chunk(buffer, samples, error_total);

  uint64_t end = SDL_GetTicksNS();

  printf(
      "ingest + error update runtime: %.3f ms\n",
      (double)(end - start) / 1e6
  );

  get_freq_hz(buffer, error_total);

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

  SDL_AudioDeviceID *devices =
      SDL_GetAudioRecordingDevices(&c);

  if (!devices) {
    fprintf(stderr, "%s\n", SDL_GetError());
    return 1;
  }

  SDL_AudioDeviceID device = devices[0];

  if (get_device_spec(device) != SUCCESS) {
    return 1;
  }

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

  SDL_Event e;

  for (;;) {
    uint64_t start = SDL_GetTicksNS();

    int process_status = process(stream);

    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        goto done;
      }
    }

    if (process_status == ERR_AUDIO_AVAILABLE ||
        process_status == ERR_AUDIO_READ) {
      break;
    }

    uint64_t end = SDL_GetTicksNS();

    printf(
        "total runtime %.3f ms\n",
        (double)(end - start) / 1e6
    );
  }

done:
  SDL_DestroyAudioStream(stream);
  SDL_Quit();
}
