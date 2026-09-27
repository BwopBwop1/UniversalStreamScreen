
// gcc complains about sdl unless done this way probably a user error.
// ffs. -Byte
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 stb` -lm
//
// gcc -o Test.out main.c `pkg-config --cflags --libs sdl3 sdl3_ttf` -lm


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
#include "SDL3/SDL_main.h"
#include "SDL3/SDL_surface.h"
#include "SDL3/SDL_render.h"
#include "SDL3/SDL_timer.h"
#include "SDL3/SDL_video.h"

#define STB_TRUETYPE_IMPLEMENTATION 1
#include "stb_truetype.h"

char *center_text = ("Be Right Back.");
const char *font_path = ("font.ttf");

int fontfd = 0;
void *fontdata = 0;
struct stat st = {};

int Window_Width = 960;
int Window_Height = 540;

SDL_Window *window = 0;
SDL_Renderer *renderer = 0;
SDL_Texture *glyph_texture = 0;

const int font_size = 32;
const int atlas_size = 512;
const int baked_character_amount = 256;
stbtt_bakedchar cdata[256];

float get_text_width(char *txt) {
 float x = 0;
 float y = 0;
 for (char *c = txt; *c; c++) {
  stbtt_aligned_quad q;
  stbtt_GetBakedQuad(cdata, atlas_size, atlas_size, *c, &x, &y, &q, 1);
 }

 return x;
}

float get_text_height(char *txt) {
 float x = 0;
 float y = 0;
 for (char *c = txt; *c; c++) {
  stbtt_aligned_quad q;
  stbtt_GetBakedQuad(cdata, atlas_size, atlas_size, *c, &x, &y, &q, 1);
 }

 return y;
}

void draw_text(char *txt, float x, float y) {

 for (char *c = txt; *c; c++) {
  stbtt_aligned_quad q;
  stbtt_GetBakedQuad(cdata, atlas_size, atlas_size, *c, &x, &y, &q, 1);
 
  float w = q.x1-q.x0;
  float h = q.y1-q.y0;

  SDL_FRect src = {.x = q.s0*atlas_size, .y = q.t0*atlas_size, .w = w, .h = h};
  SDL_FRect dst = {.x = q.x0, .y = font_size + q.y0, .w = w, .h = h};

  SDL_RenderTexture(renderer, glyph_texture, &src, &dst);
 }

}

void draw_center_aligned_text(char *txt, float x, float y) {
 draw_text(txt, (x) - (get_text_width(txt) / 2), (y) - (font_size / 2));
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
 if (!SDL_Init(SDL_INIT_VIDEO)) {
  SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 if (!SDL_CreateWindowAndRenderer("Test Application", 
			 Window_Width, Window_Height, 
			 SDL_WINDOW_RESIZABLE | SDL_WINDOW_BORDERLESS, &window, &renderer)) {
  SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }
 
 if (!SDL_SetWindowFullscreen(window, true)) {
  SDL_Log("Couldn't make window fullscreen: %s", SDL_GetError());
  return SDL_APP_FAILURE;
 }

 SDL_SetRenderLogicalPresentation(renderer, Window_Width, Window_Height, SDL_LOGICAL_PRESENTATION_LETTERBOX);

 fontfd = open(font_path, O_RDONLY);
 if (fontfd < 0) {
  SDL_Log("Couldn't open font_file");
  return SDL_APP_FAILURE;
 }

 if (fstat(fontfd, &st) < 0) {
  SDL_Log("couldn't stat font file");
  close(fontfd);
  return SDL_APP_FAILURE; }
 
 fontdata = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fontfd, 0);
 if (!fontdata) {
  SDL_Log("couldn't map font file");
  close(fontfd);
  return SDL_APP_FAILURE;
 }
 
 SDL_Surface *glyphdata = SDL_CreateSurface(atlas_size, atlas_size, SDL_PIXELFORMAT_RGBA8888);

 if (!glyphdata) {
  SDL_Log("couldn't map font file");
  munmap(fontdata, st.st_size);
  close(fontfd);
  return SDL_APP_FAILURE;
 }
 
 void *glyphblock = malloc(atlas_size * atlas_size);

 stbtt_BakeFontBitmap(fontdata, stbtt_GetFontOffsetForIndex(fontdata, 0), font_size, 
		 glyphblock, atlas_size, atlas_size, 0, baked_character_amount, cdata);
 
 int32_t *pixel = (int32_t *)glyphdata->pixels;
 uint8_t *glyphbyte = (uint8_t *)glyphblock;
 for (int i = 0; i < (atlas_size * atlas_size);){

  *pixel = 255 | *glyphbyte << 8 | *glyphbyte << 16 | *glyphbyte << 24;
  printf("Pixel data: %x Index: %d \n", *pixel, i);

  i += sizeof(int32_t);
  pixel++;
  glyphbyte++;
 }

 glyph_texture = SDL_CreateTextureFromSurface(renderer, glyphdata);

 if (!glyph_texture) {
  SDL_Log("couldn't map font file");
  munmap(fontdata, st.st_size);
  close(fontfd);
  return SDL_APP_FAILURE;
 }
 
 SDL_SetTextureScaleMode(glyph_texture, SDL_SCALEMODE_LINEAR);

 SDL_DestroySurface(glyphdata);
 munmap(fontdata, st.st_size);
 close(fontfd);


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

SDL_AppResult SDL_AppIterate(void *appstate) {
 const double now = ((double)SDL_GetTicks()) / 1000.0;  

 const float red = (float) (0.5 + 0.5 * SDL_sin(now));

 SDL_SetRenderDrawColorFloat(renderer, 0.0, 0.0, 0.0, SDL_ALPHA_OPAQUE_FLOAT); 

 SDL_RenderClear(renderer);

 SDL_SetRenderDrawColorFloat(renderer, 1.0, 1.0, 1.0, SDL_ALPHA_OPAQUE_FLOAT);
 
 draw_center_aligned_text(center_text, (Window_Width / 2), (Window_Height / 2));

 SDL_RenderPresent(renderer);

 return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
 munmap(fontdata, st.st_size);
 close(fontfd);
}

