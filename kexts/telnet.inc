typedef struct { u8 state, command, cr, escape; } Telnet;
static int telnet_byte(Telnet *t,u8 c,void (*reply)(const u8 *,int))
{
    if(t->state==1){
        t->state=0;
        if(c==255)return 255;
        if(c>=251&&c<=254){t->command=c;t->state=2;}
        else if(c==250)t->state=3;
        else if(c==244||c==248)return 3;
        else if(c==247)return 8;
        return -1;
    }
    if(t->state==2){
        u8 response[3]={255,0,c};t->state=0;
        if(t->command==253&&c!=1&&c!=3)response[1]=252;
        if(t->command==251&&c!=3)response[1]=254;
        if(response[1])reply(response,3);
        return -1;
    }
    if(t->state==3){if(c==255)t->state=4;return -1;}
    if(t->state==4){t->state=c==240?0:3;return -1;}
    if(c==255){t->state=1;return -1;}
    if(t->cr){t->cr=0;if(c==0||c==10)return -1;}
    if(c==13){t->cr=1;return 10;}
    if(t->escape){
        if(t->escape==1&&(c=='['||c=='O'))t->escape=2;
        else if(t->escape==1||(c>=0x40&&c<=0x7e))t->escape=0;
        return -1;
    }
    if(c==27){t->escape=1;return -1;}
    return c;
}
