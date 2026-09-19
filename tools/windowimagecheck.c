#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint32_t u32;
#define MAXWIN 2
#define SW 96
#define SH 80
#define TBH 8
#define C_FACE 33
static u8 screen[SW*SH];
#define BACKBUF screen
static struct {u32 mem_kb;} boot={32768};
#define BOOTINFO (&boot)
typedef struct {int x,y,w,h;} Win;
static Win wins[MAXWIN];
#define CLIX(v) ((v)->x)
#define CLIY(v) ((v)->y)
#define CLIW(v) ((v)->w)
#define CLIH(v) ((v)->h)
static u32 ticks;
static int running[MAXWIN],allocations;
static int app_handler_running(int i){return running[i];}
static void *kmalloc(u32 n){void *p=malloc(n);if(p)allocations++;return p;}
static void kfree(void *p){if(p)allocations--;free(p);}
static void mem_track(const char *name,const void *p,u32 n){(void)name;(void)p;(void)n;}
static void fill_rect(int x,int y,int w,int h,u8 color){for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(x+i>=0&&x+i<SW&&y+j>=0&&y+j<SH)screen[(y+j)*SW+x+i]=color;}
#include "../src/winimage.inc"
static int checks,failures;
#define CHECK(x) do{checks++;if(!(x)){failures++;printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
int main(void){
 wins[0].x=4;wins[0].y=5;wins[0].w=40;wins[0].h=30;
 fill_rect(4,5,40,30,12);win_image_save(0);
 CHECK(win_images[0].bytes>0);CHECK(allocations==1);
 memset(screen,0,sizeof screen);CHECK(win_image_draw(0));CHECK(screen[5*SW+4]==12);CHECK(screen[34*SW+43]==12);CHECK(screen[34*SW+44]==0);
 fill_rect(4,5,40,30,2);ticks=9;win_image_save(0);memset(screen,0,sizeof screen);win_image_draw(0);CHECK(screen[5*SW+4]==12);
 fill_rect(4,5,40,30,2);ticks=10;win_image_save(0);memset(screen,0,sizeof screen);win_image_draw(0);CHECK(screen[5*SW+4]==2);
 wins[0].w=32;fill_rect(4,5,32,30,14);ticks=11;win_image_save(0);CHECK(win_images[0].w==32);memset(screen,0,sizeof screen);win_image_draw(0);CHECK(screen[5*SW+4]==14);CHECK(screen[5*SW+36]==0);
 running[0]=1;win_image_trim();CHECK(win_images[0].data!=0);running[0]=0;win_image_trim();CHECK(allocations==0);CHECK(!win_image_draw(0));
 wins[0].x=-5;fill_rect(0,5,27,30,7);win_image_save(0);CHECK(win_images[0].x==5);CHECK(win_images[0].w==27);memset(screen,0,sizeof screen);win_image_draw(0);CHECK(screen[5*SW]==7);CHECK(screen[5*SW+27]==0);
 ticks=0xFFFFFFFEu;win_image_free(0);fill_rect(0,5,27,30,3);win_image_save(0);ticks=9;fill_rect(0,5,27,30,4);win_image_save(0);memset(screen,0,sizeof screen);win_image_draw(0);CHECK(screen[5*SW]==4);
 win_image_free(0);CHECK(allocations==0);CHECK(win_image_bytes==0);
 printf("Window snapshots: %d checks, %d failures\n",checks,failures);return failures!=0;
}
