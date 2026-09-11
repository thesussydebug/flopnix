static int archive_icon_name(const Kapi *k,const char *n)
{const char *e=k->path_ext(n);return !k->strcasecmp(e,"fpa")||!k->strcasecmp(e,"pz");}
static void archive_icon(const Kapi *k,int x,int y,int s)
{
    k->fill_rect(x+2*s,y+3*s,12*s,11*s,C_OLIVE);
    k->fill_rect(x+s,y+2*s,14*s,4*s,C_YELLOW);
    k->rect(x+s,y+2*s,14*s,12*s,C_BLACK);
    k->hline(x+2*s,y+6*s,12*s,C_BLACK);
    k->fill_rect(x+7*s,y+3*s,2*s,10*s,C_SILVER);
    k->rect(x+6*s,y+7*s,4*s,3*s,C_BLACK);
}
