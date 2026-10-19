/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include "xxwidgets/xxwidgets_canvas.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static xxwidgets_status canvas_sync(xxwidgets_widget *w){++w->app->syncing;xxwidgets_status s=w->app->ops->sync(w);--w->app->syncing;return s;}
void xxwidgets_canvas_dispose(xxwidgets_widget *w){free(w->canvas_pixels);w->canvas_pixels=NULL;for(size_t i=0;i<w->canvas_label_count;++i)free((char*)w->canvas_labels[i].text);free(w->canvas_labels);w->canvas_labels=NULL;w->canvas_label_count=0;}
xxwidgets_status xxwidgets_canvas_set_pixels(xxwidgets_widget *w,unsigned width,unsigned height,const uint32_t *pixels){
 if(!w||w->kind!=XXWIDGETS_CANVAS||width>4096||height>4096||((width==0)!=(height==0))||((!width||!height)!=(pixels==NULL)))return XXWIDGETS_INVALID_ARGUMENT;
 size_t n=(size_t)width*height;uint32_t *copy=n?malloc(n*sizeof(*copy)):NULL;if(n&&!copy)return XXWIDGETS_OUT_OF_MEMORY;if(n)memcpy(copy,pixels,n*sizeof(*copy));
 uint32_t *old=w->canvas_pixels;unsigned ow=w->canvas_width,oh=w->canvas_height;int ov=w->value;w->canvas_pixels=copy;w->canvas_width=width;w->canvas_height=height;w->value=-1;++w->canvas_revision;
 xxwidgets_status s=canvas_sync(w);if(s!=XXWIDGETS_OK){w->canvas_pixels=old;w->canvas_width=ow;w->canvas_height=oh;w->value=ov;--w->canvas_revision;canvas_sync(w);free(copy);}else free(old);return s;
}
xxwidgets_status xxwidgets_canvas_set_labels(xxwidgets_widget *w,const xxwidgets_canvas_label *labels,size_t count){
 if(!w||w->kind!=XXWIDGETS_CANVAS||count>4096||(count&&!labels))return XXWIDGETS_INVALID_ARGUMENT;
 xxwidgets_canvas_label *copy=count?calloc(count,sizeof(*copy)):NULL;if(count&&!copy)return XXWIDGETS_OUT_OF_MEMORY;size_t i=0;xxwidgets_status s=XXWIDGETS_OK;
 for(;i<count;++i){if(!labels[i].text||strlen(labels[i].text)>4095||!xxwidgets_valid_utf8(labels[i].text)){s=XXWIDGETS_INVALID_ARGUMENT;break;}copy[i]=labels[i];copy[i].text=xxwidgets_strdup(labels[i].text);if(!copy[i].text){s=XXWIDGETS_OUT_OF_MEMORY;break;}}
 if(s==XXWIDGETS_OK){xxwidgets_canvas_label *old=w->canvas_labels;size_t n=w->canvas_label_count;w->canvas_labels=copy;w->canvas_label_count=count;s=canvas_sync(w);if(s==XXWIDGETS_OK){copy=old;count=n;}else {w->canvas_labels=old;w->canvas_label_count=n;canvas_sync(w);}}
 for(size_t j=0;j<count;++j)if(copy)free((char*)copy[j].text);free(copy);return s;
}
xxwidgets_status xxwidgets_canvas_get_pixels(const xxwidgets_widget *w,unsigned *width,unsigned *height,const uint32_t **pixels){if(!w||w->kind!=XXWIDGETS_CANVAS||!width||!height||!pixels)return XXWIDGETS_INVALID_ARGUMENT;*width=w->canvas_width;*height=w->canvas_height;*pixels=w->canvas_pixels;return XXWIDGETS_OK;}
xxwidgets_status xxwidgets_canvas_select(xxwidgets_widget *w,unsigned x,unsigned y){if(!w||w->kind!=XXWIDGETS_CANVAS||x>=w->canvas_width||y>=w->canvas_height)return XXWIDGETS_INVALID_ARGUMENT;int old=w->value;w->value=(int)(y*w->canvas_width+x);xxwidgets_status s=canvas_sync(w);if(s!=XXWIDGETS_OK){w->value=old;canvas_sync(w);}return s;}
xxwidgets_status xxwidgets_canvas_get_selection(const xxwidgets_widget *w,unsigned *x,unsigned *y){
 if(!w||w->kind!=XXWIDGETS_CANVAS||!x||!y)return XXWIDGETS_INVALID_ARGUMENT;
 if(!w->canvas_width||!w->canvas_height||w->value<0||(size_t)w->value>=(size_t)w->canvas_width*w->canvas_height)return XXWIDGETS_UNAVAILABLE;
 *x=(unsigned)w->value%w->canvas_width;*y=(unsigned)w->value/w->canvas_width;return XXWIDGETS_OK;
}
void xxwidgets_canvas_fit(const xxwidgets_widget *w,double width,double height,double *x,double *y,double *rw,double *rh){*x=*y=0;*rw=width;*rh=height;if(w->canvas_width&&w->canvas_height){double scale=fmin(width/w->canvas_width,height/w->canvas_height);*rw=scale*w->canvas_width;*rh=scale*w->canvas_height;*x=(width-*rw)/2;*y=(height-*rh)/2;}}
void xxwidgets_canvas_input(xxwidgets_widget *w,double x,double y,double width,double height){
 if(!xxwidgets_focusable(w)||!w->canvas_width||!w->canvas_height)return;double ox,oy,rw,rh;xxwidgets_canvas_fit(w,width,height,&ox,&oy,&rw,&rh);if(x<ox||y<oy||x>=ox+rw||y>=oy+rh||rw<=0||rh<=0)return;
 unsigned px=(unsigned)((x-ox)*w->canvas_width/rw),py=(unsigned)((y-oy)*w->canvas_height/rh);if(xxwidgets_canvas_select(w,px,py)==XXWIDGETS_OK)xxwidgets_emit_point(w,XXWIDGETS_EVENT_SELECT,w->value,(int)px,(int)py);
}
int xxwidgets_canvas_key(xxwidgets_widget *w,int key){
 if(!w||w->kind!=XXWIDGETS_CANVAS||!w->canvas_width||!w->canvas_height)return 0;int value=w->value<0?0:w->value,total=(int)(w->canvas_width*w->canvas_height);
 if(key==XXWIDGETS_KEY_LEFT)--value;else if(key==XXWIDGETS_KEY_RIGHT)++value;else if(key==XXWIDGETS_KEY_UP)value-=(int)w->canvas_width;else if(key==XXWIDGETS_KEY_DOWN)value+=(int)w->canvas_width;else if(key==XXWIDGETS_KEY_HOME)value=0;else if(key==XXWIDGETS_KEY_END)value=total-1;else if(key==XXWIDGETS_KEY_PAGEUP)value-=(int)w->canvas_width*8;else if(key==XXWIDGETS_KEY_PAGEDOWN)value+=(int)w->canvas_width*8;else if(key==13||key==XXWIDGETS_KEY_ENTER){xxwidgets_emit(w,XXWIDGETS_EVENT_ACTIVATE,w->value);return 1;}else return 0;
 if(value<0)value=0;if(value>=total)value=total-1;if(xxwidgets_canvas_select(w,(unsigned)value%w->canvas_width,(unsigned)value/w->canvas_width)==XXWIDGETS_OK)xxwidgets_emit_point(w,XXWIDGETS_EVENT_SELECT,value,value%(int)w->canvas_width,value/(int)w->canvas_width);return 1;
}
void xxwidgets_splitter_input(xxwidgets_widget *w,double x,double y,double width,double height){if(!w||w->kind!=XXWIDGETS_SPLITTER||!xxwidgets_focusable(w)||width<=0||height<=0)return;int px=w->rect.x+(int)floor(x*w->rect.width/width),py=w->rect.y+(int)floor(y*w->rect.height/height);w->value=w->rect.height>w->rect.width?px:py;xxwidgets_emit_point(w,XXWIDGETS_EVENT_DRAG,w->value,px,py);}
int xxwidgets_splitter_key(xxwidgets_widget *w,int key){if(!w||w->kind!=XXWIDGETS_SPLITTER)return 0;int v=w->rect.height>w->rect.width;int delta=key==XXWIDGETS_KEY_LEFT||key==XXWIDGETS_KEY_UP?-1:key==XXWIDGETS_KEY_RIGHT||key==XXWIDGETS_KEY_DOWN?1:0;if(!delta)return 0;int x=w->rect.x+(v?delta:0),y=w->rect.y+(v?0:delta);w->value=v?x:y;xxwidgets_emit_point(w,XXWIDGETS_EVENT_DRAG,w->value,x,y);return 1;}
