
// gcc complains about sdl unless done this way probably a user error.
// ffs. -Byte
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 stb` -lm
//
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 sdl3-ttf` -lm
// gcc --debug  -o Test.out main.c `pkg-config --cflags --libs sdl3 sdl3-ttf` -lm

// SDL has a standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include "SDL3/SDL.h"
#include "SDL3/SDL_thread.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"

#include <SDL3_ttf/SDL_ttf.h>

#define MAX_READ_EVENTS 64

#define SToMS(value) (value * 1000)
#define MSToNS(value) (value * 1000000)

#define Kilobytes(number) ((number) * 1024ull)
#define Megabytes(number) (Kilobytes(number) * 1024ull)
#define Gigabytes(number) (Megabytes(number) * 1024ull)

typedef struct event {
 int32_t type;
 int32_t scancode;
 uint16_t modstate;
} event;

typedef struct memoryArena {
 uint32_t size;
 uint32_t used;

 void *memory;
} memoryArena;

#define MAX_PARTICLE 10
typedef struct particle {
 float x, y;
 float dx, dy;
 float lifetime;
} particle;

// The head index is so that it will bias 
// towards looking at old particles first.
typedef struct particle_system {
 particle particles[MAX_PARTICLE];
 size_t count;
 size_t head;
 int32_t size;
} particle_system;

int32_t connect_to_listener(int32_t *fd);
int32_t check_and_get_event(int32_t fd);

void str_zero(uint8_t *b1);
int32_t str_length(uint8_t *b1);
bool str_compare(uint8_t *b1, uint8_t *b2);
void str_copy_to(uint8_t *b1, uint32_t amount, uint8_t *b2);
uint8_t * str_copy_inplace(const uint8_t *b1, uint32_t amount, uint8_t *b2);
void str_total(uint8_t *file, uint8_t delimiter, uint32_t *total);
void str_cut(uint8_t *file, int32_t idx, int32_t *location, int32_t *size, uint8_t delimiter);

void generate_text_texture(SDL_Texture **tex, uint8_t *txt, SDL_Color fg, SDL_Color bg);
void draw_text(SDL_Renderer *renderer, float x, float y, float scale, SDL_Texture *tex);

Uint32 spawn_particle(void *userdata, SDL_TimerID id, Uint32 interval);
Uint32 change_text(void *userdata, SDL_TimerID id, Uint32 interval);

void *pushSize(memoryArena *arena, size_t size);

particle_system ps;
memoryArena arena;

// Two textures to swap what text is drawn for fade in/out.
SDL_Texture *a_Texture = 0;
SDL_Texture *b_Texture = 0;

const char *font_path = ("assets/fonts/font.ttf");
const char *gone_away_path = ("assets/txts/gone_away_messages.txt");

void **messages = 0;
int32_t total_messages = 0;

SDL_Window *window = 0;
SDL_Renderer *renderer = 0;
TTF_Font *font = 0;

double nowMS = 0;
double lastMS = 0;

int32_t dirty = 0;
float refreshRatePerSecond = 60;
float targetMS = 0;

int32_t file_descriptor = 0;

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {

 arena.size = Megabytes(2);
 arena.memory = malloc(arena.size);

 connect_to_listener(&file_descriptor);

 if (!SDL_Init(SDL_INIT_VIDEO)) {
  SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 if (!SDL_CreateWindowAndRenderer("Test Application", 
			 960, 540, 
			 SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS, &window, &renderer)) {
  SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 if (!TTF_Init()) {
  SDL_Log("Couldn't initialize SDL_ttf: %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 font = TTF_OpenFontIO(SDL_IOFromFile(font_path, "r"), true, 96.0f);
 if (!font) {
  SDL_Log("Couldn't open font: %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 SDL_IOStream *gone_away_file = SDL_IOFromFile(gone_away_path, "r");

 int file_size = SDL_GetIOSize(gone_away_file);
 void * gone_away_messages = pushSize(&arena, file_size);
 SDL_ReadIO(gone_away_file, gone_away_messages, file_size);

 if (!gone_away_file) {
  SDL_Log("Couldn't read gone_away_file %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 SDL_CloseIO(gone_away_file);

 if (!gone_away_messages) {
  SDL_Log("Couldn't load gone_away_file into memory %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }
 
 str_total(gone_away_messages, '\n', &total_messages);
 messages = (void **)pushSize(&arena, sizeof(void *) * total_messages);

 for (int i = 0; i < total_messages; i++) {
  int location = 0;
  int size = 0;
  str_cut(gone_away_messages, i, &location, &size, '\n');
  messages[i] = pushSize(&arena, size + 1);
  str_copy_to((char *)(gone_away_messages + location), size, messages[i]);
 }

 ps.size = 16;
 SDL_AddTimer(SToMS(1), spawn_particle, &ps);
 SDL_AddTimer(SToMS(15), change_text, 0);
 dirty = 1;

 //if (!SDL_SetWindowFullscreen(window, true)) {
  //SDL_Log("Couldn't make window fullscreen: %s", SDL_GetError());
  //return SDL_APP_FAILURE;
 //}

 lastMS = (SDL_GetTicks());

 int displayID = SDL_GetPrimaryDisplay();
 const SDL_DisplayMode *displayMode = SDL_GetCurrentDisplayMode(displayID);
 refreshRatePerSecond = displayMode->refresh_rate;
 targetMS = 1000.0 / refreshRatePerSecond;

 if(!SDL_SetRenderVSync(renderer, 1)) {
  SDL_Log("Couldn't make renderer vsync: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 return SDL_APP_CONTINUE;  
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
 switch (event->type) {
  case SDL_EVENT_KEY_DOWN: {

   printf("sdl: %s : %d : %d\n", SDL_GetKeyName(event->key.key), event->key.repeat, event->key.key);

   if (event->key.key == SDLK_ESCAPE && !event->key.repeat) {
    return SDL_APP_SUCCESS;
   } 

   if (event->key.key == SDLK_SPACE && !event->key.repeat) {
    printf("Space was pressed\n");
   }
  } break;
  case SDL_EVENT_QUIT: {
   return SDL_APP_SUCCESS;
  } break;

  default: {
  } break;
 }

 return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
 nowMS = SDL_GetTicks();
 float time_step = (float)(nowMS - lastMS) / 1000.0; // Seconds

 if (check_and_get_event(file_descriptor)) {

 } else {
  // TODO: Try and recoonect.
 }

 if (dirty) {
  SDL_Color fg = {255, 255, 255, SDL_ALPHA_OPAQUE};
  SDL_Color bg = {255, 255, 255, 0};
  
  int r = SDL_rand(total_messages);
  generate_text_texture(&a_Texture, messages[r], fg, bg);

  dirty = 0;

 }

 {
  Uint8 texture_mod;
  SDL_GetTextureAlphaMod(a_Texture, &texture_mod);
  
  int32_t new_texture_mod = texture_mod - (10) * time_step;
  if (new_texture_mod < 0) {
   new_texture_mod = 0;
  }

  SDL_SetTextureAlphaMod(a_Texture, (Uint8)new_texture_mod);

 }

 SDL_SetRenderDrawColorFloat(renderer, 0.0, 0.0, 0.0, SDL_ALPHA_OPAQUE_FLOAT); 

 SDL_RenderClear(renderer);

 int w;
 int h;
 SDL_GetCurrentRenderOutputSize(renderer, &w, &h);

 for (int i = 0; i < MAX_PARTICLE; i++) {
  if (ps.particles[i].lifetime > 0.0) {
    if (ps.particles[i].y < h - ps.size) {
     ps.particles[i].x += ps.particles[i].dx * time_step;
     ps.particles[i].y += ps.particles[i].dy * time_step;
    } else {
     ps.particles[i].lifetime = 0.0;
    }
    
    int x = ps.particles[i].x;
    int y = ps.particles[i].y;

    
    SDL_FRect dst = {.x = x, .y = y, .w = ps.size, .h = ps.size};

    SDL_SetRenderDrawColorFloat(renderer, 0.0, 0.0, 0.8, SDL_ALPHA_OPAQUE_FLOAT); 
    SDL_RenderRect(renderer, &dst);

    }
 }

 SDL_SetRenderDrawColorFloat(renderer, 1.0, 1.0, 1.0, SDL_ALPHA_OPAQUE_FLOAT);
 
 draw_text(renderer, (w / 2), (h / 2), 0.5f, a_Texture);
 SDL_RenderPresent(renderer);

 float stepMS = (float)(nowMS - lastMS);
 if (stepMS < targetMS) {
  SDL_Delay((targetMS - stepMS));
 }
 lastMS = nowMS;

 return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
 // TODO: Actual Object handling.
 free(arena.memory);

 SDL_DestroyTexture(a_Texture);

 if (font) {
  TTF_CloseFont(font);
 }
 TTF_Quit();

}

int32_t str_length(uint8_t *b1) {
 int32_t result = 0;
 for (const uint8_t *c = b1; *c; c++) {
  result++;
 }
 return result;
}

void str_zero(uint8_t *b1) {
 for (uint8_t *c = b1; *c; c++) { 
  *c = 0;
 }
}

bool str_compare(uint8_t *b1, uint8_t *b2) {

 if (str_length(b1) != str_length(b2)) {
  return false;
 }

 for (uint8_t *c = b1; *c; c++) {
  uint8_t *b = b2++;

  if (*b == *c) {
   continue;
  }

  return false;
 }

 return true;
}

void str_copy_to(uint8_t *b1, uint32_t amount, uint8_t *b2) {
 uint8_t *src = b1;
 uint8_t *dst = b2;

 for (uint32_t i = 0; i < amount; i++) {
  *dst++ = *src++;
 }
}

uint8_t * str_copy_inplace(const uint8_t *b1, uint32_t amount, uint8_t *b2) {
 const char *src = b1;
 char *dst = b2;

 for (int i = 0; i < amount; i++) {
    *dst++ = *src++;
 }
 return dst;
}

void str_total(uint8_t *file, uint8_t delimiter, uint32_t *total) {
 int32_t t = 0;
 for (uint8_t *c = file; *c; c++) { 
  uint8_t current_char = *c;
  
  if (current_char == delimiter) {
   t++;
  }
 }
 
  *total = t;
}

void str_cut(uint8_t *file, int32_t idx, int32_t *location, int32_t *size, uint8_t delimiter) {
 int32_t loc = 0;
 int32_t s = 0;

 int32_t index = 0;
 int32_t i = 0;

 for (uint8_t *c = file; *c; c++) { 
  uint8_t current_char = *c;
  
  if (current_char == delimiter) {
    s = i - loc;

   if (index == idx) {
    break;
   } 

   loc = i + 1;
   index++;
  }
  
  i++;
 }

 *location = loc;
 *size = s;
}

void generate_text_texture(SDL_Texture **tex, uint8_t *txt, SDL_Color fg, SDL_Color bg) {

 SDL_Texture *temp = 0;
 // NOTE: This can fail when txt is a null ptr eventually we might want a string type.
 SDL_Surface *text = TTF_RenderText_Shaded(font, txt, 0, fg, bg);
 if (text) {
  temp = SDL_CreateTextureFromSurface(renderer, text);
  SDL_DestroySurface(text);
 }
 if (!temp) {
  SDL_Log("Couldn't create text: %s\n", SDL_GetError());
 }
 *tex = temp;
}

void draw_text(SDL_Renderer *renderer, float x, float y, float scale, SDL_Texture *tex) {
 if (!tex) {
  return;
 }

 float width = tex->w * scale;
 float height = tex->h * scale;

 SDL_FRect dst = {.x = (x) - (width / 2), .y = (y) - (height / 2), .w = width, .h = height};
 SDL_RenderTexture(renderer, tex, NULL, &dst);
}

Uint32 spawn_particle(void *userdata, SDL_TimerID id, Uint32 interval) {
 
 particle_system *par_sar = (particle_system *)userdata;
 // This will go through and initialize the particles at a given interval.
 // Bwop
 int try_spawn = 1;
 while (try_spawn) {
  if (par_sar->head > MAX_PARTICLE - 1) {
    par_sar->head = 0;
    try_spawn = 0;
    return interval;
  }

  if (par_sar->particles[par_sar->head].lifetime <= 0.0) {
    int w;
    int h;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);

    int half_w = (w/2);

    int r = SDL_rand(half_w + 1) + (half_w - (par_sar->size));
    int y = 0;

    par_sar->particles[par_sar->head].x = r;
    par_sar->particles[par_sar->head].y = y;

    par_sar->particles[par_sar->head].dx = -50;
    par_sar->particles[par_sar->head].dy = 50;

    par_sar->particles[par_sar->head].lifetime = 1.0;
    try_spawn = 0;
  }

  par_sar->head++;
 }

 return interval;
}

Uint32 change_text(void *userdata, SDL_TimerID id, Uint32 interval) {

 dirty = 1;

 return interval;
}

void *pushSize(memoryArena *arena, size_t size) {
 uint8_t *result = 0;
 if (arena->used + size < arena->size) {
  result = (uint8_t *)arena->memory + arena->used;

  arena->used += size;
 }
 return (void *)(result);
}

int32_t connect_to_listener(int32_t *fd) {
 
 const char *path = "/run/user/1000/keyboardListener";

 struct sockaddr_un sockaddr = { .sun_family = AF_UNIX };
 strcpy(sockaddr.sun_path, path);

 int32_t socket_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
 if (socket_fd == -1) {
  printf("Unable to open socket\n");
  printf("errno value %s\n", strerror(errno));
  return 0;
 }

 int32_t connect_result = connect(socket_fd, 
                                  (struct sockaddr *)&sockaddr, 
                                  offsetof(struct sockaddr_un, sun_path) + strlen(sockaddr.sun_path) + 1);

 if (connect_result == -1) {
  printf("Unable to connect to socket\n");
  printf("errno value %s\n", strerror(errno));
  close(socket_fd);
  return 0;
 }
 
 if (socket_fd != 0) {
  *fd = socket_fd;
 }

 int setflag_result = fcntl(socket_fd, F_SETFL, O_NONBLOCK);
 if (setflag_result == -1) {
  printf("Couldn't set socket to non blocking\n");
  printf("errno value %s\n", strerror(errno));
  close(socket_fd);
  return 0;
 } 

 return 1;
}

int32_t check_and_get_event(int32_t fd) {

 if (fd > 0) {
  return 0;
 }

 struct event read_events[MAX_READ_EVENTS];
 int32_t read_events_size = sizeof(struct event) * MAX_READ_EVENTS;
 
 ssize_t bytes_read = read(fd, read_events, read_events_size);

 if (bytes_read == -1) {
  printf("errno value %s\n", strerror(errno));
  return 0;
 }
 
 int32_t num_of_events = bytes_read / sizeof(struct event);
 for (uint32_t i = 0; i > num_of_events; i++) {
  SDL_Event ev = {0};

  ev.type = read_events[i].type;
  ev.key.timestamp = SDL_GetTicksNS();
  ev.key.scancode = read_events[i].scancode;
  ev.key.mod = read_events[i].modstate;
  ev.key.key = SDL_GetKeyFromScancode(ev.key.scancode, ev.key.mod, true);
  ev.key.repeat = 0;

  if (!SDL_PushEvent(&ev)) {
   SDL_Log("Push Event either failed or was filtered: %s", SDL_GetError());
  }

 }

 return 1;
}
