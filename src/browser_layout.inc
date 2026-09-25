#pragma once
#define BR_BOXES 2048
typedef struct {int x,y,w,h;u16 start,end;int object;} BrBox;
typedef struct {BrBox box[BR_BOXES];int count,width,height,clipped;} BrLayout;
static int br_min(int a,int b){return a<b?a:b;}
static int br_max(int a,int b){return a>b?a:b;}
static int br_px(int value,int width,int fallback){return value<0?br_max(1,width*(-value)/100):value?value:fallback;}
static int br_box(BrLayout *l,int x,int y,int w,int h,int start,int end,int obj)
{
    if(l->count==BR_BOXES){l->clipped=1;return -1;}int i=l->count++;BrBox *b=&l->box[i];b->x=x;b->y=y;b->w=w;b->h=h;b->start=(u16)start;b->end=(u16)end;b->object=obj;return i;
}
static int br_object_at(const BrPage *b,int off){for(int i=0;i<b->objects;i++)if(b->object[i].start==off)return i;return -1;}
static void br_align_line(BrLayout *l,int first,int x,int width,int used,int align)
{
    int shift=align==1?(width-used)/2:align==2?width-used:0;if(shift<0)shift=0;
    for(int i=first;i<l->count;i++)if(l->box[i].x>=x)l->box[i].x+=shift;
}
static int br_layout_range(const BrPage *b,BrLayout *l,int start,int end,int x,int y,int width,int depth);
static int br_layout_table(const BrPage *b,BrLayout *l,int oi,int x,int y,int width,int depth)
{
    const BrObject *obj=&b->object[oi];const BrTable *t=&b->table[obj->ref];int cols=br_min(8,br_max(1,t->cols)),rows=br_min(32,br_max(1,t->rows));
    int cw[8],rh[32],cx[9],ry[33],height[BR_CELLS];u8 used[32][8],column[BR_CELLS];u16 first[BR_CELLS],last[BR_CELLS];
    for(int i=0;i<256;i++)((volatile u8 *)used)[i]=0;
    int gap=t->spacing,pad=t->padding,avail=br_max(8,width-gap*(cols+1));
    for(int c=0;c<cols;c++)cw[c]=avail/cols;for(int r=0;r<rows;r++)rh[r]=24;
    if(t->frame&&!t->vertical&&t->sizes[0]){int p=0,total=0,stars=0,weights[8]={0};for(int c=0;c<cols;c++){char v[32];int a=p;while(t->sizes[p]&&t->sizes[p]!=',')p++;tw_copy(v,sizeof v,t->sizes+a,br_min(31,p-a));if(t->sizes[p])p++;if(tw_contains(v,"*")){weights[c]=br_number(v,1,100);stars+=weights[c];cw[c]=0;}else {cw[c]=br_px(br_dimension(v,0,2048),avail,avail/cols);total+=cw[c];}}if(stars){for(int c=0;c<cols;c++)if(weights[c])cw[c]=br_max(8,(avail-total)*weights[c]/stars);}}
    else for(int i=0;i<b->cells;i++){const BrCell *c=&b->cell[i];if(c->table!=obj->ref||c->row||c->span!=1||c->col>=cols||!c->width)continue;cw[c->col]=br_px(c->width,avail,cw[c->col]);}
    int sum=0;for(int c=0;c<cols;c++)sum+=cw[c];if(sum>avail)for(int c=0;c<cols;c++)cw[c]=br_max(8,cw[c]*avail/sum);
    cx[0]=x+gap;for(int c=0;c<cols;c++)cx[c+1]=cx[c]+cw[c]+gap;
    int outer=br_box(l,x,y,width,1,obj->start,obj->end,oi);
    for(int i=0;i<b->cells;i++){
        const BrCell *c=&b->cell[i];column[i]=255;if(c->table!=obj->ref||c->row>=rows)continue;int col=0;while(col<cols&&used[c->row][col])col++;int span=br_min(c->span,cols-col),rs=br_min(c->rows,rows-c->row);if(span<1){l->clipped=1;continue;}while(span>1){int ok=1;for(int k=0;k<span;k++)if(used[c->row][col+k])ok=0;if(ok)break;span--;}
        column[i]=col;for(int r=0;r<rs;r++)for(int k=0;k<span;k++)used[c->row+r][col+k]=1;
        int w=cx[col+span]-cx[col]-gap;first[i]=l->count;br_box(l,cx[col],0,w,1,c->start,c->end,-2-i);
        int h=br_layout_range(b,l,c->start,c->end,cx[col]+pad,pad,br_max(8,w-2*pad),depth+1)+2*pad;height[i]=br_max(24,h);last[i]=l->count;
        if(rs==1)rh[c->row]=br_max(rh[c->row],height[i]);
    }
    for(int i=0;i<b->cells;i++){const BrCell *c=&b->cell[i];if(column[i]==255)continue;int rs=br_min(c->rows,rows-c->row),h=gap*(rs-1);for(int r=0;r<rs;r++)h+=rh[c->row+r];if(h<height[i])rh[c->row+rs-1]+=height[i]-h;}
    ry[0]=y+gap;for(int r=0;r<rows;r++)ry[r+1]=ry[r]+rh[r]+gap;
    for(int i=0;i<b->cells;i++){const BrCell *c=&b->cell[i];if(column[i]==255)continue;int h=ry[br_min(rows,c->row+c->rows)]-ry[c->row]-gap;for(int k=first[i];k<last[i];k++)l->box[k].y+=ry[c->row];if(first[i]<last[i])l->box[first[i]].h=h;}
    int h=ry[rows]-y;if(outer>=0)l->box[outer].h=h;return h;
}
static int br_layout_range(const BrPage *b,BrLayout *l,int start,int end,int x,int y,int width,int depth)
{
    if(depth>4){l->clipped=1;return 16;}int origin=y,xx=x,lh=16,first=l->count,align=0;
    for(int p=start;p<end&&l->count<BR_BOXES;){
        int ri=br_run_at(b,p);BrStyle st=b->runs?b->run[ri].style:(BrStyle){0,15,0,0};if(xx==x)align=st.align;
        if(b->page.text[p]=='\n'){br_align_line(l,first,x,width,xx-x,align);y+=lh;xx=x;lh=16;first=l->count;p++;continue;}
        if(b->page.text[p]==1){int oi=br_object_at(b,p);if(oi<0){p++;continue;}const BrObject *o=&b->object[oi];
            if(o->kind==BR_TABLE){if(xx>x){br_align_line(l,first,x,width,xx-x,align);y+=lh;}int w=br_min(width,br_px(b->table[o->ref].width,width,width));y+=br_layout_table(b,l,oi,x,y,w,depth)+4;xx=x;lh=16;first=l->count;p=br_max(p+1,o->end);continue;}
            int w=br_min(width,br_px(o->width,width,o->kind==BR_RULE?width:96)),h=o->height?o->height:o->kind==BR_RULE?8:48;
            if(o->kind==BR_IMAGE&&o->width>0&&o->height>0&&w<o->width)h=br_max(1,h*w/o->width);
            if(xx>x&&xx+w>x+width){br_align_line(l,first,x,width,xx-x,align);y+=lh;xx=x;lh=16;first=l->count;}
            br_box(l,xx,y,w,h,p,p+1,oi);xx+=w+4;lh=br_max(lh,h+4);p++;continue;
        }
        if(b->page.text[p]==' '&&xx==x&&!(st.flags&4)){p++;continue;}
        int limit=br_max(1,(x+width-xx)/8),q=p,run_end=ri+1<b->runs?b->run[ri+1].start:end;
        if(!(st.flags&4)&&b->page.text[p]!=' '){int word=p;while(word<end&&b->page.text[word]>32&&b->page.text[word]!=1)word++;if((word-p)*8>x+width-xx&&xx>x){br_align_line(l,first,x,width,xx-x,align);y+=lh;xx=x;lh=16;first=l->count;continue;}}
        while(q<end&&q<run_end&&q-p<limit&&b->page.text[q]!='\n'&&b->page.text[q]!=1){q++;if(!(st.flags&4)&&b->page.text[q-1]==' ')break;}
        if(q==p)q++;int prev=l->count-1;
        if(prev>=first&&l->box[prev].object==-1&&l->box[prev].y==y&&l->box[prev].x+l->box[prev].w==xx&&l->box[prev].end==p&&br_run_at(b,l->box[prev].start)==ri){l->box[prev].end=(u16)q;l->box[prev].w+=(q-p)*8;}
        else br_box(l,xx,y,(q-p)*8,16,p,q,-1);xx+=(q-p)*8;p=q;
        if(xx+8>x+width){br_align_line(l,first,x,width,xx-x,align);y+=lh;xx=x;lh=16;first=l->count;}
    }
    br_align_line(l,first,x,width,xx-x,align);return y-origin+(xx>x?lh:0);
}
static void br_layout(const BrPage *b,BrLayout *l,int width)
{
    l->count=l->clipped=0;l->width=br_max(8,width);l->height=br_layout_range(b,l,0,b->page.len,0,0,l->width,0)+4;if(l->count==BR_BOXES)l->clipped=1;
}
