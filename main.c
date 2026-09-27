
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

char *center_text_buffer = 0;
int center_text_len = 4096;

const char *font_path = ("assets/fonts/font.ttf");
const char *gone_away_path = ("assets/txts/gone_away_messages.txt");

void *gone_away_messages = 0;

int Window_Width = 960;
int Window_Height = 540;

SDL_Window *window = 0;
SDL_Renderer *renderer = 0;
SDL_Texture *texture = 0;
TTF_Font *font = 0;

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

// Will parse and fill two ints for bounds with location and size
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
			 Window_Width, Window_Height, 
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
 
 int total = 0;
 str_total(gone_away_messages, '\n', &total);
 int r = SDL_rand(total);
 
 int location = 0;
 int size = 0;
 str_cut(gone_away_messages, r, &location, &size, '\n');
 str_copy_to(gone_away_messages + location, size, center_text_buffer);
 printf("%s\n", center_text_buffer);
 SDL_Surface *text = TTF_RenderText_Shaded(font, center_text_buffer, 0, fg, bg);
 if (text) {
  texture = SDL_CreateTextureFromSurface(renderer, text);
  SDL_DestroySurface(text);
 }
 if (!texture) {
  SDL_Log("Couldn't create text: %s\n", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 
 if (!SDL_SetWindowFullscreen(window, true)) {
  SDL_Log("Couldn't make window fullscreen: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 return SDL_APP_CONTINUE;  
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
 if (event->type == SDL_EVENT_QUIT) {
  return SDL_APP_SUCCESS;
 }

 return SDL_APP_CONTINUE;
}

struct Star {
 float x;
 float y;
 SDL_FRect region;
 SDL_Texture *texture;
};

void draw_text(SDL_Renderer *renderer, float x, float y, float scale) {
 float width = texture->w * scale;
 float height = texture->h * scale;

 SDL_FRect dst = {.x = (x) - (width / 2), .y = (y) - (height / 2), .w = width, .h = height};
 SDL_RenderTexture(renderer, texture, NULL, &dst);

}

SDL_AppResult SDL_AppIterate(void *appstate) {
 const double now = ((double)SDL_GetTicks()) / 1000.0;  

 const float red = (float) (0.5 + 0.5 * SDL_sin(now));

 SDL_SetRenderDrawColorFloat(renderer, 0.0, 0.0, 0.0, SDL_ALPHA_OPAQUE_FLOAT); 

 SDL_RenderClear(renderer);

 SDL_SetRenderDrawColorFloat(renderer, 1.0, 1.0, 1.0, SDL_ALPHA_OPAQUE_FLOAT);
 
 int w;
 int h;
 SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
 
 draw_text(renderer, (w / 2), (h / 2), 0.5f);
 SDL_RenderPresent(renderer);

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

