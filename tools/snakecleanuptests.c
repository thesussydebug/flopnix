#include <stdio.h>
#include "kapi.h"
static int sounding,deleted=-1;
static u32 now=100;
static void tone(u32 hz){sounding=hz!=0;}
static void silence(void){sounding=0;}
static void cancel(int id){deleted=id;}
static const Kapi test_api={.ticks=&now,.speaker_tone=tone,.speaker_off=silence,.timer_del=cancel};
#include "../kexts/snake.c"
int main(void)
{
    api=&test_api;timer_id=3;blip(440,10);snake_close(0);
    int bad=sounding||beep_off_at||deleted!=3||timer_id!=-1;
    deleted=-1;snake_close(0);bad|=deleted!=-1;
    printf("SNAKE CLEANUP: %s\n",bad?"FAIL":"PASS");return bad;
}
