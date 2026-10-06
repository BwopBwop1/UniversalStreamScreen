
// gcc complains about sdl unless done this way probably a user error.
// ffs. -Byte
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 stb` -lm
//
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 sdl3-ttf` -lm
// gcc --debug  -o Test.out main.c `pkg-config --cflags --libs sdl3 sdl3-ttf` -lm
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#define SDL_MAIN_USE_CALLBACKS 1
#include "SDL3/SDL.h"
#include "SDL3/SDL_stdinc.h"
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"

#include <SDL3_ttf/SDL_ttf.h>

#define MSToS(value) (value * 1000)

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

#define Kilobytes(number) ((number) * 1024ull)
#define Megabytes(number) (Kilobytes(number) * 1024ull)
#define Gigabytes(number) (Megabytes(number) * 1024ull)

typedef struct memoryArena {
 uint32_t size;
 uint32_t used;

 void *memory;
} memoryArena;
memoryArena arena;

// Two textures to swap what text is drawn for fade in/out.
SDL_Texture *a_Texture = 0;
SDL_Texture *b_Texture = 0;

void str_zero(char *b1) {
 for (char *c = b1; *c; c++) { 
  *c = 0;
 }
}

void str_copy_to(char *b1, int amount, char *b2) {
 char *src = b1;
 char *dst = b2;

 for (int i = 0; i < amount; i++) {
  *dst++ = *src++;
 }
}

char * str_copy_inplace(const char *b1, int amount, char *b2) {
 const char *src = b1;
 char *dst = b2;

 for (int i = 0; i < amount; i++) {
    *dst++ = *src++;
 }
 return dst;
}

void str_total(uint8_t *file, uint8_t delimiter, uint32_t *total) {
 int t = 0;
 for (char *c = file; *c; c++) { 
  char current_char = *c;
  
  if (current_char == delimiter) {
   t++;
  }
 }
 
  *total = t;
}

void str_cut(char *file, int idx, int *location, int *size, char delimiter) {
 int loc = 0;
 int s = 0;

 int index = 0;
 int i = 0;

 for (char *c = file; *c; c++) { 
  char current_char = *c;
  
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

void generate_text_texture(SDL_Texture **tex, char *txt, SDL_Color fg, SDL_Color bg) {
 SDL_Texture *temp = 0;

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

// I am having trouble just spewing something out that doesn't 
// take into account every single detail so this is what I am 
// going with -Bwop
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
particle_system ps;

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

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {

 arena.size = Megabytes(2);
 arena.memory = malloc(arena.size);


 if (!SDL_Init(SDL_INIT_VIDEO)) {
  SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 if (!SDL_CreateWindowAndRenderer("Test Application", 
			 0, 0, 
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
 SDL_AddTimer(MSToS(1), spawn_particle, &ps);
 SDL_AddTimer(MSToS(15), change_text, 0);
 dirty = 1;

 if (!SDL_SetWindowFullscreen(window, true)) {
  SDL_Log("Couldn't make window fullscreen: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

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

   if (event->key.key == SDLK_ESCAPE && !event->key.repeat) {
    return SDL_APP_SUCCESS;
   } 

   if (event->key.key == SDLK_SPACE && !event->key.repeat) {
    int r = SDL_rand(256);
    int g = SDL_rand(256);
    int b = SDL_rand(256);

    SDL_Color fg = {r, g, b, SDL_ALPHA_OPAQUE};
    SDL_Color bg = {255, 255, 255, 0};

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

 if (dirty) {
  SDL_Color fg = {255, 255, 255, SDL_ALPHA_OPAQUE};
  SDL_Color bg = {255, 255, 255, 0};
  
  int r = SDL_rand(total_messages + 1);
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

