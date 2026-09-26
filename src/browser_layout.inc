#pragma once
#define BR_BOXES_MAX 65535
typedef struct {int x,y,w,h;u32 start,end;int object;} BrBox;
typedef struct {BrBox *box;int cap,count,width,height,clipped;int tmin[BR_TABLES],tmax[BR_TABLES];short cmin[BR_TABLES][8],cmax[BR_TABLES][8],fixed[BR_TABLES][8];u8 ccol[BR_CELLS],cspan[BR_CELLS];} BrLayout;
static int br_min(int a,int b){return a<b?a:b;}
static int br_max(int a,int b){return a>b?a:b;}
static void br_scale_row(u8 *out,const u8 *src,int start,int count,int src_w,int box_w){int q=src_w/box_w,r=src_w%box_w,sx=start*src_w/box_w,acc=start*src_w%box_w;for(int i=0;i<count;i++){out[i]=src[sx];sx+=q;if((acc+=r)>=box_w){acc-=box_w;sx++;}}}
static int br_px(int value,int width,int fallback){return value<0?br_max(1,width*(-value)/100):value?value:fallback;}
static int br_box_room(BrLayout *l)
{
    if(l->count<l->cap)return 1;int cap=l->cap?l->cap*2:512;if(cap>BR_BOXES_MAX)cap=BR_BOXES_MAX;
    BrBox *b=cap>l->cap&&tw_grow?tw_grow(l->box,(u32)cap*sizeof *b):0;if(!b){l->clipped=1;return 0;}
    l->box=b;l->cap=cap;return 1;
}
static void br_layout_free(BrLayout *l){if(l->cap)tw_grow(l->box,0);l->box=0;l->cap=l->count=0;}
static int br_box(BrLayout *l,int x,int y,int w,int h,int start,int end,int obj)
{
    if(!br_box_room(l))return -1;int i=l->count++;BrBox *b=&l->box[i];b->x=x;b->y=y;b->w=w;b->h=h;b->start=(u32)start;b->end=(u32)end;b->object=obj;return i;
}
static int br_object_at(const BrPage *b,int off){for(int i=0;i<b->objects;i++)if((int)b->object[i].start==off)return i;return -1;}
static void br_align_line(BrLayout *l,int first,int x,int width,int used,int align)
{
    int shift=align==1?(width-used)/2:align==2?width-used:0;if(shift<0)shift=0;
    for(int i=first;i<l->count;i++)if(l->box[i].x>=x)l->box[i].x+=shift;
}
static int br_indent(BrStyle st,int width){return br_min(((st.flags>>4)&7)*24,width/2);}
static void br_measure(const BrPage *b,const BrLayout *l,int start,int end,int *mn,int *mx)
{
    int line=0,word=0,lo=0,hi=0,ri=br_run_at(b,start);
    for(int p=start;p<end;p++){
        while(ri+1<b->runs&&(int)b->run[ri+1].start<=p)ri++;
        BrStyle st=b->runs?b->run[ri].style:(BrStyle){0,15,0,0};int c=(u8)b->page.text[p],pre=st.flags&4,ind=((st.flags>>4)&7)*24;
        if(c=='\n'){hi=br_max(hi,line);line=word=0;continue;}
        if(!line)line=ind;
        if(c==1){int oi=br_object_at(b,p);if(oi<0)continue;const BrObject *o=&b->object[oi];
            if(o->kind==BR_TABLE){hi=br_max(hi,line);line=word=0;lo=br_max(lo,ind+l->tmin[o->ref]);hi=br_max(hi,ind+l->tmax[o->ref]);p=br_max(p,(int)o->end-1);continue;}
            int w=o->width>0?o->width:o->width<0||o->kind==BR_RULE?0:96;line+=w+4;word=0;lo=br_max(lo,ind+w);continue;}
        line+=8;if(c==' '&&!pre)word=0;else {word+=8;lo=br_max(lo,pre?line:ind+word);}
    }
    *mn=br_min(lo,4096);*mx=br_min(br_max(hi,line),4096);
}
static void br_measure_table(const BrPage *b,BrLayout *l,int ti)
{
    const BrTable *t=&b->table[ti];int cols=br_min(8,br_max(1,t->cols)),rows=br_min(32,br_max(1,t->rows)),gap=t->spacing,pad=t->padding;
    u8 used[32][8];for(int i=0;i<256;i++)((volatile u8 *)used)[i]=0;
    short *mn=l->cmin[ti],*mx=l->cmax[ti],*fx=l->fixed[ti];for(int c=0;c<8;c++){((volatile short *)mn)[c]=0;((volatile short *)mx)[c]=0;((volatile short *)fx)[c]=0;}
    for(int i=0;i<b->cells;i++){
        const BrCell *c=&b->cell[i];if(c->table!=ti)continue;l->ccol[i]=255;if(c->row>=rows)continue;
        int col=0;while(col<cols&&used[c->row][col])col++;int span=br_min(c->span,cols-col),rs=br_min(c->rows,rows-c->row);if(span<1){l->clipped=1;continue;}
        while(span>1){int ok=1;for(int k=0;k<span;k++)if(used[c->row][col+k])ok=0;if(ok)break;span--;}
        l->ccol[i]=(u8)col;l->cspan[i]=(u8)span;for(int r=0;r<rs;r++)for(int k=0;k<span;k++)used[c->row+r][col+k]=1;
    }
    for(int pass=0;pass<2;pass++)for(int i=0;i<b->cells;i++){
        const BrCell *c=&b->cell[i];if(c->table!=ti||l->ccol[i]==255)continue;int col=l->ccol[i],span=l->cspan[i];if((span==1)!=(pass==0))continue;
        int lo,hi;br_measure(b,l,(int)c->start,(int)c->end,&lo,&hi);lo+=2*pad;hi+=2*pad;
        if(span==1){if(c->width>0)fx[col]=(short)br_max(fx[col],br_min(c->width,2048));else if(c->width<0&&fx[col]<=0)fx[col]=(short)br_min(fx[col],c->width);
            mn[col]=(short)br_max(mn[col],lo);mx[col]=(short)br_max(mx[col],hi);continue;}
        int slo=gap*(span-1),shi=slo;for(int k=0;k<span;k++){slo+=mn[col+k];shi+=mx[col+k];}
        for(int k=0;k<span;k++){if(lo>slo)mn[col+k]=(short)(mn[col+k]+(lo-slo)/span);if(hi>shi)mx[col+k]=(short)(mx[col+k]+(hi-shi)/span);}
    }
    int lo=gap*(cols+1),hi=lo;for(int c=0;c<cols;c++){int f=fx[c]>0?br_max(mn[c],fx[c]):0;lo+=f?f:mn[c];hi+=f?f:br_max(mn[c],mx[c]);}
    if(t->width>0)lo=hi=br_max(lo,t->width);
    l->tmin[ti]=br_min(lo,4096);l->tmax[ti]=br_min(hi,4096);
}
static void br_columns(const BrLayout *l,int ti,int cols,int avail,int *cw)
{
    int mn[8],mx[8],fixed[8],smin=0,smax=0,sfree=0,free=0;
    for(int c=0;c<cols;c++){int f=l->fixed[ti][c];mn[c]=l->cmin[ti][c];mx[c]=br_max(l->cmax[ti][c],mn[c]);if(f<0)f=br_max(1,avail*(-f)/100);
        fixed[c]=f>0;if(f>0)mn[c]=mx[c]=br_max(mn[c],f);smin+=mn[c];smax+=mx[c];if(!fixed[c]){sfree+=mx[c];free++;}}
    int given=0,last=-1;
    if(avail>=smax){int extra=avail-smax,base=free?sfree:smax,n=free?free:cols;
        for(int c=0;c<cols;c++){cw[c]=mx[c];if(free&&fixed[c])continue;int add=base>0?extra*mx[c]/base:extra/n;cw[c]+=add;given+=add;last=c;}
        if(last>=0)cw[last]+=extra-given;
    }else if(avail>smin){int room=avail-smin,span=smax-smin;
        for(int c=0;c<cols;c++){int add=(mx[c]-mn[c])*room/span;cw[c]=mn[c]+add;given+=add;if(mx[c]>mn[c])last=c;}
        if(last>=0)cw[last]+=room-given;
    }else for(int c=0;c<cols;c++)cw[c]=smin?mn[c]*avail/smin:avail/cols;
    for(int c=0;c<cols;c++)cw[c]=br_max(8,cw[c]);
}
static int br_layout_range(const BrPage *b,BrLayout *l,int start,int end,int x,int y,int width,int depth);
static int br_layout_table(const BrPage *b,BrLayout *l,int oi,int x,int y,int width,int depth)
{
    const BrObject *obj=&b->object[oi];int ti=obj->ref;const BrTable *t=&b->table[ti];int cols=br_min(8,br_max(1,t->cols)),rows=br_min(32,br_max(1,t->rows));
    int cw[8],rh[32],cx[9],ry[33],height[BR_CELLS];u16 first[BR_CELLS],last[BR_CELLS];
    int gap=t->spacing,pad=t->padding,tw;
    if(t->frame){tw=br_min(width,br_px(t->width,width,width));int avail=br_max(8,tw-gap*(cols+1));for(int c=0;c<cols;c++)cw[c]=avail/cols;
        if(!t->vertical&&t->sizes[0]){int p=0,total=0,stars=0,weights[8]={0};for(int c=0;c<cols;c++){char v[32];int a=p;while(t->sizes[p]&&t->sizes[p]!=',')p++;tw_copy(v,sizeof v,t->sizes+a,br_min(31,p-a));if(t->sizes[p])p++;if(tw_contains(v,"*")){weights[c]=br_number(v,1,100);stars+=weights[c];cw[c]=0;}else {cw[c]=br_px(br_dimension(v,0,2048),avail,avail/cols);total+=cw[c];}}if(stars){for(int c=0;c<cols;c++)if(weights[c])cw[c]=br_max(8,(avail-total)*weights[c]/stars);}}
        int sum=0;for(int c=0;c<cols;c++)sum+=cw[c];if(sum>avail)for(int c=0;c<cols;c++)cw[c]=br_max(8,cw[c]*avail/sum);}
    else {tw=t->width?br_min(width,br_px(t->width,width,width)):br_min(width,l->tmax[ti]);tw=br_max(tw,br_min(width,l->tmin[ti]));br_columns(l,ti,cols,br_max(8,tw-gap*(cols+1)),cw);}
    for(int r=0;r<rows;r++)rh[r]=24;
    cx[0]=x+gap;for(int c=0;c<cols;c++)cx[c+1]=cx[c]+cw[c]+gap;
    tw=cx[cols]-x;if(tw<width){int shift=t->align==1?(width-tw)/2:t->align==2?width-tw:0;x+=shift;for(int c=0;c<=cols;c++)cx[c]+=shift;}
    int outer=br_box(l,x,y,tw,1,(int)obj->start,(int)obj->end,oi);
    for(int i=0;i<b->cells;i++){
        const BrCell *c=&b->cell[i];if(c->table!=ti||l->ccol[i]==255)continue;int col=l->ccol[i],span=l->cspan[i],rs=br_min(c->rows,rows-c->row);
        int w=cx[col+span]-cx[col]-gap;first[i]=(u16)l->count;br_box(l,cx[col],0,w,1,(int)c->start,(int)c->end,-2-i);
        int h=br_layout_range(b,l,(int)c->start,(int)c->end,cx[col]+pad,pad,br_max(8,w-2*pad),depth+1)+2*pad;height[i]=br_max(24,h);last[i]=(u16)l->count;
        if(rs==1)rh[c->row]=br_max(rh[c->row],height[i]);
    }
    for(int i=0;i<b->cells;i++){const BrCell *c=&b->cell[i];if(c->table!=ti||l->ccol[i]==255)continue;int rs=br_min(c->rows,rows-c->row),h=gap*(rs-1);for(int r=0;r<rs;r++)h+=rh[c->row+r];if(h<height[i])rh[c->row+rs-1]+=height[i]-h;}
    ry[0]=y+gap;for(int r=0;r<rows;r++)ry[r+1]=ry[r]+rh[r]+gap;
    for(int i=0;i<b->cells;i++){const BrCell *c=&b->cell[i];if(c->table!=ti||l->ccol[i]==255)continue;int h=ry[br_min(rows,c->row+c->rows)]-ry[c->row]-gap;for(int k=first[i];k<last[i];k++)l->box[k].y+=ry[c->row];if(first[i]<last[i])l->box[first[i]].h=h;}
    int h=ry[rows]-y;if(outer>=0)l->box[outer].h=h;return h;
}
static int br_layout_range(const BrPage *b,BrLayout *l,int start,int end,int x,int y,int width,int depth)
{
    if(depth>4){l->clipped=1;return 16;}int origin=y,right=x+width,lx=x,xx=x,lh=16,first=l->count,align=0,bol=1;
    for(int p=start;p<end&&br_box_room(l);){
        int ri=br_run_at(b,p);BrStyle st=b->runs?b->run[ri].style:(BrStyle){0,15,0,0};
        if(bol){lx=x+br_indent(st,width);xx=lx;align=st.align;bol=0;}
        if(b->page.text[p]=='\n'){br_align_line(l,first,lx,right-lx,xx-lx,align);y+=lh;lh=16;first=l->count;bol=1;p++;continue;}
        if(b->page.text[p]==1){int oi=br_object_at(b,p);if(oi<0){p++;continue;}const BrObject *o=&b->object[oi];
            if(o->kind==BR_TABLE){if(xx>lx){br_align_line(l,first,lx,right-lx,xx-lx,align);y+=lh;}y+=br_layout_table(b,l,oi,lx,y,right-lx,depth)+4;lh=16;first=l->count;bol=1;p=br_max(p+1,(int)o->end);continue;}
            int w=br_min(right-lx,br_px(o->width,right-lx,o->kind==BR_RULE?right-lx:96)),h=o->height?o->height:o->kind==BR_RULE?8:48;
            if(o->kind==BR_IMAGE&&o->width>0&&o->height>0&&w<o->width)h=br_max(1,h*w/o->width);
            if(xx>lx&&xx+w>right){br_align_line(l,first,lx,right-lx,xx-lx,align);y+=lh;xx=lx;lh=16;first=l->count;}
            br_box(l,xx,y,w,h,p,p+1,oi);xx+=w+4;lh=br_max(lh,h+4);p++;continue;
        }
        if(b->page.text[p]==' '&&xx==lx&&!(st.flags&4)){p++;continue;}
        int limit=br_max(1,(right-xx)/8),q=p,run_end=ri+1<b->runs?(int)b->run[ri+1].start:end;
        if(!(st.flags&4)&&b->page.text[p]!=' '){int word=p;while(word<end&&b->page.text[word]>32&&b->page.text[word]!=1)word++;if((word-p)*8>right-xx&&xx>lx){br_align_line(l,first,lx,right-lx,xx-lx,align);y+=lh;xx=lx;lh=16;first=l->count;continue;}}
        while(q<end&&q<run_end&&q-p<limit&&b->page.text[q]!='\n'&&b->page.text[q]!=1){q++;if(!(st.flags&4)&&b->page.text[q-1]==' ')break;}
        if(q==p)q++;int prev=l->count-1;
        if(prev>=first&&l->box[prev].object==-1&&l->box[prev].y==y&&l->box[prev].x+l->box[prev].w==xx&&l->box[prev].end==(u32)p&&br_run_at(b,(int)l->box[prev].start)==ri){l->box[prev].end=(u32)q;l->box[prev].w+=(q-p)*8;}
        else br_box(l,xx,y,(q-p)*8,16,p,q,-1);xx+=(q-p)*8;p=q;
        if(xx+8>right){br_align_line(l,first,lx,right-lx,xx-lx,align);y+=lh;xx=lx;lh=16;first=l->count;}
    }
    br_align_line(l,first,lx,right-lx,xx-lx,align);return y-origin+(xx>lx?lh:0);
}
static void br_layout(const BrPage *b,BrLayout *l,int width)
{
    l->count=l->clipped=0;l->width=br_max(8,width);for(int ti=b->tables-1;ti>=0;ti--)br_measure_table(b,l,ti);
    l->height=br_layout_range(b,l,0,b->page.len,0,0,l->width,0)+4;
}
