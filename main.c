
// gcc complains about sdl unless done this way probably a user error.
// ffs. -Byte
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 stb` -lm
//
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 sdl3-ttf` -lm

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

uint8_t *center_text_buffer = 0;
int32_t center_text_len = 4096;

int32_t dirty = 0;
#define MSToS(value) (value * 1000)

const char *font_path = ("assets/fonts/font.ttf");
const char *gone_away_path = ("assets/txts/gone_away_messages.txt");

void *gone_away_messages = 0;

SDL_Window *window = 0;
SDL_Renderer *renderer = 0;
SDL_Texture *texture = 0;
TTF_Font *font = 0;

double now = 0;
double last = 0;

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

void str_total(char *file, char delimiter, int *total) {
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


// TODO: function could be converted to be more universal.
void regen_text_buffer(char *txt) {
 int total = 0;
 str_total(gone_away_messages, '\n', &total);
 int r = SDL_rand(total);

 int location = 0;
 int size = 0;
 str_cut(gone_away_messages, r, &location, &size, '\n');
 str_zero(txt);
 str_copy_to(gone_away_messages + location, size, txt);
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

 // This will go through and initialize the particles at a given interval.
 // Bwop
 if (ps.head >= MAX_PARTICLE - 1) {
  ps.head = 0;
  return 0;
 }

 int w;
 int h;
 SDL_GetCurrentRenderOutputSize(renderer, &w, &h);

 int half_w = (w/2);

 int r = SDL_rand(half_w + 1) + (half_w - (ps.size));
 int y = 0;

 ps.particles[ps.head].x = r;
 ps.particles[ps.head].y = y;

 ps.particles[ps.head].dx = -50;
 ps.particles[ps.head].dy = 50;

 ps.particles[ps.head].lifetime = 1.0;

 ps.head++;

 ps.count++;
 if (ps.count > MAX_PARTICLE - 1) {
  ps.count = MAX_PARTICLE - 1;
 }

 return interval;
}

Uint32 change_text(void *userdata, SDL_TimerID id, Uint32 interval) {

 printf("%s : %d\n", center_text_buffer, interval);

 regen_text_buffer(center_text_buffer);
 dirty = 1;

 return interval;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
 if (!SDL_Init(SDL_INIT_VIDEO)) {
  SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 SDL_IOStream *gone_away_file = SDL_IOFromFile(gone_away_path, "r");
 if (!gone_away_file) {
  SDL_Log("Couldn't read gone_away_file %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }
 
 int file_size = SDL_GetIOSize(gone_away_file);
 gone_away_messages = malloc(file_size);
 SDL_ReadIO(gone_away_file, gone_away_messages, file_size);
 if (!gone_away_messages) {
  SDL_Log("Couldn't load gone_away_file into memory %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 SDL_CloseIO(gone_away_file);

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

 SDL_Color fg = {255, 255, 255, SDL_ALPHA_OPAQUE};
 SDL_Color bg = {255, 255, 255, 0};
 
 center_text_buffer = malloc(center_text_len);
 
 regen_text_buffer(center_text_buffer);
 generate_text_texture(&texture, center_text_buffer, fg, bg);
 
 ps.size = 16;
 SDL_AddTimer(MSToS(1), spawn_particle, 0);
 SDL_AddTimer(MSToS(15), change_text, 0);

 if (!SDL_SetWindowFullscreen(window, true)) {
  SDL_Log("Couldn't make window fullscreen: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 last = ((double)SDL_GetTicks()) / 1000.0;

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

    regen_text_buffer(center_text_buffer);
    generate_text_texture(&texture, center_text_buffer, fg, bg);
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

void draw_text(SDL_Renderer *renderer, float x, float y, float scale) {
 float width = texture->w * scale;
 float height = texture->h * scale;

 SDL_FRect dst = {.x = (x) - (width / 2), .y = (y) - (height / 2), .w = width, .h = height};
 SDL_RenderTexture(renderer, texture, NULL, &dst);

}

SDL_AppResult SDL_AppIterate(void *appstate) {
 if (dirty) {
  SDL_Color fg = {255, 255, 255, SDL_ALPHA_OPAQUE};
  SDL_Color bg = {255, 255, 255, 0};

  generate_text_texture(&texture, center_text_buffer, fg, bg);
 }
 

 now = ((double)SDL_GetTicks()) / 1000.0;  

 const float red = (float) (0.5 + 0.5 * SDL_sin(now));

 SDL_SetRenderDrawColorFloat(renderer, 0.0, 0.0, 0.0, SDL_ALPHA_OPAQUE_FLOAT); 

 SDL_RenderClear(renderer);

 int w;
 int h;
 SDL_GetCurrentRenderOutputSize(renderer, &w, &h);

 for (int i = 0; i < ps.count; i++) {

  if (ps.particles[i].y < h - ps.size) {
   ps.particles[i].x += ps.particles[i].dx * (float)(now - last);
   ps.particles[i].y += ps.particles[i].dy * (float)(now - last);
  } else {
   int half_w = (w/2);

   int r = SDL_rand(half_w + 1) + (half_w - (ps.size));
   int y = 0;

   ps.particles[i].x = r;
   ps.particles[i].y = y;

   ps.particles[i].dx = -50;
   ps.particles[i].dy = 50;
  }
  
  int x = ps.particles[i].x;
  int y = ps.particles[i].y;

  
  SDL_FRect dst = {.x = x, .y = y, .w = ps.size, .h = ps.size};

  SDL_SetRenderDrawColorFloat(renderer, 0.0, 0.0, 0.8, SDL_ALPHA_OPAQUE_FLOAT); 
  SDL_RenderRect(renderer, &dst);
 }

 SDL_SetRenderDrawColorFloat(renderer, 1.0, 1.0, 1.0, SDL_ALPHA_OPAQUE_FLOAT);
 
 draw_text(renderer, (w / 2), (h / 2), 0.5f);
 SDL_RenderPresent(renderer);

 last = now;

 return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {

  free(gone_away_messages);
  free(center_text_buffer);

  SDL_DestroyTexture(texture);

  if (font) {
   TTF_CloseFont(font);
  }
  TTF_Quit();
}

