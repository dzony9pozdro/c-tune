#include <SDL3/SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>

#define SAMPLE_FREQUENCY 48000
#define MIN_DEPTH_TO_MATCH 1e-4

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
  max_dt = 2400, // 20hz
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

void ingest_next_chunk(int16_t *buffer, const int16_t *samples) {

  // for buffer of chunks A B C D -> B C D D
  memmove(
      buffer,
      buffer + chunk_size,
      (buffer_size - chunk_size) * sizeof(buffer[0])
  );

  // B C D D -> B C D E
  memcpy(
      buffer + (buffer_size - chunk_size),
      samples,
      chunk_size * sizeof(buffer[0])
  );
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

double get_error_mean(const int16_t *buffer, const int t) {
  double error_total = 0.0;
  int error_count = 0;

  for (int i = 0; i + t < buffer_size; i++) {
    double a = buffer[i] / 32768.0;
    double b = buffer[i + t] / 32768.0;

    error_total += fabs(b - a);
    error_count++;
  }

  return error_total / error_count;
}

bool is_local_min(const double *lag_err, int i, int scope) {
  if (i - scope < 0 || i + scope >= buffer_size) {
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

  printf("i=%d err=%f depth=%f\n", i, lag_err[i], check_depth);
  return check_depth > MIN_DEPTH_TO_MATCH;
}

int find_period(double *lag_err) {
  Err prev_match = {-1, 0.0};
  int dt = 0;

  for (int i = 0; i < buffer_size; i++) {
    if (!matches_close_enough(lag_err, i, scope)) {

      continue;
    }

    if (prev_match.idx != -1) {
      dt = i - prev_match.idx;
      printf("\n=======================\nMATCH: dt = %d\n", dt);
    }

    prev_match = (Err){i, lag_err[i]};
  }

  if (dt > max_dt) {
    return 0;
  }
  return dt;
}

int get_freq_hz(const int16_t *buffer) {
  double lag_err[buffer_size];

  for (int t = 0; t < buffer_size; t++) {
    lag_err[t] = get_error_mean(buffer, t);
  }

  int dt = find_period(lag_err);

  if (dt == 0) {
    return ERR_BAD_FREQ;
  }

  return (int)((float)SAMPLE_FREQUENCY / (float)dt);
}

int process(SDL_AudioStream *stream) {
  int available = SDL_GetAudioStreamAvailable(stream);

  if (available < 0) {
    fprintf(stderr, "audio available: %s\n", SDL_GetError());
    return ERR_AUDIO_AVAILABLE;
  }

  int16_t samples[chunk_size] = {0};

  if (available < (int)sizeof samples) {
    SDL_Delay(1);
    return SAMPLE_WAIT;
  }

  int bytes = SDL_GetAudioStreamData(stream, samples, sizeof samples);

  if (bytes < 0) {
    fprintf(stderr, "audio read: %s\n", SDL_GetError());
    return ERR_AUDIO_READ;
  }

  static int16_t buffer[buffer_size] = {0};
  ingest_next_chunk(buffer, samples);
  get_freq_hz(buffer);

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
  if (get_device_spec(device) != SUCCESS) {
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

  SDL_Event e;

  for (;;) {
    int process_status = process(stream);
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_EVENT_QUIT) {
        goto done;
      }
    }

    if (process_status == ERR_AUDIO_AVAILABLE ||
        process_status == ERR_AUDIO_READ) {
      break;
    };
  }
done:
  SDL_DestroyAudioStream(stream);
  SDL_Quit();
}
