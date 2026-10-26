/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "../xxwidgets_internal.h"
#include "xxwidgets/die_engine/xxwidgets_die_signature_debugger.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUG_MAX_BREAKPOINTS 256
#define DEBUG_MAX_WATCHES 128
struct xxwidgets_die_signature_debugger {
    xxwidgets_widget *owner, *controls[XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT];
    xxwidgets_die_signature_debug_session *session;
    xxwidgets_die_signature_debug_source_fn resolver;
    void *resolver_user;
    char *primary_source, *primary_name, *displayed_name;
    xxwidgets_die_signature_debug_breakpoint *breakpoints;
    size_t breakpoint_count;
    char **watches;
    size_t watch_count;
    uint64_t revision;
    xxwidgets_die_signature_debug_state state;
    size_t last_line, last_column, last_frame;
    int pending, pending_value, refreshing, source_available;
};
static int outside(const xxwidgets_die_signature_debugger *view)
{ return !view->owner->app->dispatch_depth && !view->owner->app->polling && !view->owner->app->syncing; }
static int active(xxwidgets_die_signature_debug_state s)
{ return s==XXWIDGETS_DIE_DEBUG_RUNNING||s==XXWIDGETS_DIE_DEBUG_PAUSED||s==XXWIDGETS_DIE_DEBUG_STOPPING; }
static int valid_bounds(xxwidgets_rect b)
{ return b.x>=0&&b.y>=0&&b.width>=100&&b.height>=32&&b.width<=32767&&b.height<=32767&&b.x<=32767-b.width&&b.y<=32767-b.height; }
static void rects(xxwidgets_rect b,xxwidgets_rect *r)
{
    int x=b.x,y=b.y,w=b.width,h=b.height,left=(w-1)*63/100,right=w-left-1,body=h-9;
    int stack=body/4,locals=body/3,watch=body-stack-locals;
    static const int widths[]={13,7,7,7,7,7,11,10};int offset=0;
    for(int i=0;i<8;++i){r[i]=(xxwidgets_rect){x+offset,y,widths[i],2};offset+=widths[i]+1;}
    r[XXWIDGETS_DIE_DEBUGGER_SOURCE_NAME]=(xxwidgets_rect){x,y+2,w,1};
    r[XXWIDGETS_DIE_DEBUGGER_SOURCE]=(xxwidgets_rect){x,y+3,left,body};
    r[XXWIDGETS_DIE_DEBUGGER_STACK_LABEL]=(xxwidgets_rect){x+left+1,y+3,right,1};
    r[XXWIDGETS_DIE_DEBUGGER_STACK]=(xxwidgets_rect){x+left+1,y+4,right,stack-1};
    r[XXWIDGETS_DIE_DEBUGGER_LOCALS_LABEL]=(xxwidgets_rect){x+left+1,y+3+stack,right,1};
    r[XXWIDGETS_DIE_DEBUGGER_LOCALS]=(xxwidgets_rect){x+left+1,y+4+stack,right,locals-1};
    r[XXWIDGETS_DIE_DEBUGGER_WATCH_LABEL]=(xxwidgets_rect){x+left+1,y+3+stack+locals,right,1};
    r[XXWIDGETS_DIE_DEBUGGER_WATCHES]=(xxwidgets_rect){x+left+1,y+4+stack+locals,right,watch-3};
    r[XXWIDGETS_DIE_DEBUGGER_WATCH_INPUT]=(xxwidgets_rect){x+left+1,y+body+1,right-12,2};
    r[XXWIDGETS_DIE_DEBUGGER_ADD_WATCH]=(xxwidgets_rect){x+w-11,y+body+1,5,2};
    r[XXWIDGETS_DIE_DEBUGGER_REMOVE_WATCH]=(xxwidgets_rect){x+w-5,y+body+1,5,2};
    r[XXWIDGETS_DIE_DEBUGGER_BREAKPOINTS]=(xxwidgets_rect){x,y+body+3,left,4};
    r[XXWIDGETS_DIE_DEBUGGER_OUTPUT]=(xxwidgets_rect){x+left+1,y+body+3,right,4};
    r[XXWIDGETS_DIE_DEBUGGER_STATUS]=(xxwidgets_rect){x,y+h-1,w,1};
}
static char *label_escape(const char *text)
{
    static const char hex[]="0123456789ABCDEF";size_t n=strlen(text?text:"");char *out=malloc(n*4+1),*p=out;
    if(!out)return NULL;for(size_t i=0;i<n;++i){unsigned char c=(unsigned char)text[i];if(c<32||c>=127){*p++='\\';*p++='x';*p++=hex[c>>4];*p++=hex[c&15];}else *p++=(char)c;}*p=0;return out;
}
static const char *state_name(xxwidgets_die_signature_debug_state s)
{
    static const char *const names[]={"Ready","Running","Paused","Stopping","Finished","Error","Stopped"};
    return s>=XXWIDGETS_DIE_DEBUG_IDLE&&s<=XXWIDGETS_DIE_DEBUG_STOPPED?names[s]:"Unknown";
}
static xxwidgets_status notice(xxwidgets_die_signature_debugger *v,const char *text)
{ return xxwidgets_widget_set_text(v->controls[XXWIDGETS_DIE_DEBUGGER_STATUS],text); }
static xxwidgets_status update_enabled(xxwidgets_die_signature_debugger *v,xxwidgets_die_signature_debug_state s)
{
    xxwidgets_status r=XXWIDGETS_OK,t;int paused=s==XXWIDGETS_DIE_DEBUG_PAUSED,running=s==XXWIDGETS_DIE_DEBUG_RUNNING;
    for(int i=0;i<8;++i){int enabled=i==0?(!active(s)||paused):i==1?running:i==2?active(s):i<6?paused:i==6?v->source_available:1;
        t=xxwidgets_widget_set_enabled(v->controls[i],enabled);if(r==XXWIDGETS_OK)r=t;}
    t=xxwidgets_widget_set_enabled(v->controls[XXWIDGETS_DIE_DEBUGGER_STACK],paused);if(r==XXWIDGETS_OK)r=t;
    t=xxwidgets_widget_set_enabled(v->controls[XXWIDGETS_DIE_DEBUGGER_LOCALS],paused);if(r==XXWIDGETS_OK)r=t;
    return r;
}
static void free_breakpoints(xxwidgets_die_signature_debug_breakpoint *bp,size_t n)
{ for(size_t i=0;i<n;++i)free((char*)bp[i].source_name);free(bp); }
static void free_watches(char **w,size_t n)
{for(size_t i=0;i<n;++i)free(w[i]);free(w);}
static xxwidgets_status render_markers(xxwidgets_die_signature_debugger *v,size_t execution)
{
    size_t lines[DEBUG_MAX_BREAKPOINTS],n=0,max=xxwidgets_die_signature_edit_line_count(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE]);
    for(size_t i=0;i<v->breakpoint_count;++i){const char *name=v->breakpoints[i].source_name;
        if(!*name)name=v->primary_name;
        if(v->source_available&&!strcmp(name,v->displayed_name)&&v->breakpoints[i].line<=max){size_t j;for(j=0;j<n;++j)if(lines[j]==v->breakpoints[i].line)break;if(j==n)lines[n++]=v->breakpoints[i].line;}
    }
    return xxwidgets_die_signature_edit_set_debug_markers(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE],lines,n,v->source_available&&execution<=max?execution:0);
}
static xxwidgets_status render_breakpoints(xxwidgets_die_signature_debugger *v)
{
    xxwidgets_widget *list=v->controls[XXWIDGETS_DIE_DEBUGGER_BREAKPOINTS];xxwidgets_status r=xxwidgets_listbox_clear(list);
    for(size_t i=0;i<v->breakpoint_count&&r==XXWIDGETS_OK;++i){char line[1200];char *name=label_escape(*v->breakpoints[i].source_name?v->breakpoints[i].source_name:v->primary_name);if(!name)return XXWIDGETS_OUT_OF_MEMORY;
        snprintf(line,sizeof(line),"BP %s:%zu",name,v->breakpoints[i].line);free(name);r=xxwidgets_listbox_add(list,line);}
    return r;
}
static xxwidgets_status display_source(xxwidgets_die_signature_debugger *v,const char *name,int *changed)
{
    char *source=NULL,*copy;xxwidgets_status r;if(!name)name="";*changed=0;
    if(!strcmp(name,v->displayed_name)&&v->source_available)return XXWIDGETS_OK;
    copy=xxwidgets_strdup(name);if(!copy)return XXWIDGETS_OUT_OF_MEMORY;
    if(!strcmp(name,v->primary_name)){source=xxwidgets_strdup(v->primary_source);r=source?XXWIDGETS_OK:XXWIDGETS_OUT_OF_MEMORY;}
    else r=xxwidgets_die_signature_debug_session_source(v->session,name,&source);
    if(r==XXWIDGETS_UNAVAILABLE&&v->resolver)r=v->resolver(name,&source,v->resolver_user);
    if(r!=XXWIDGETS_OK&&r!=XXWIDGETS_UNAVAILABLE){free(source);free(copy);return r;}
    if(r==XXWIDGETS_OK&&!source){free(copy);return XXWIDGETS_INVALID_ARGUMENT;}
    {int available=r==XXWIDGETS_OK;r=xxwidgets_die_signature_edit_set_source(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE],available?source:"");
        if(r==XXWIDGETS_INVALID_ARGUMENT||r==XXWIDGETS_UNAVAILABLE){available=0;r=xxwidgets_die_signature_edit_set_source(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE],"");}
        if(r==XXWIDGETS_OK){free(v->displayed_name);v->displayed_name=copy;copy=NULL;v->source_available=available;*changed=1;}}
    free(source);free(copy);return r;
}
static void reveal_location(xxwidgets_die_signature_debugger *v,size_t line,size_t column)
{
    const char *source=xxwidgets_die_signature_edit_source(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE]);size_t at=0;
    if(!v->source_available||!source||!line)return;
    for(size_t n=1;n<line&&source[at];++n){while(source[at]&&source[at]!='\n')++at;if(source[at])++at;}
    for(size_t n=1;n<column&&source[at]&&source[at]!='\n';++n)++at;
    while(at&&((unsigned char)source[at]&0xc0)==0x80)--at;
    (void)xxwidgets_die_signature_edit_set_cursor(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE],at);
}
static xxwidgets_status render_values(xxwidgets_widget *list,const xxwidgets_die_signature_debug_value *values,size_t count)
{
    xxwidgets_status r=xxwidgets_listbox_clear(list);
    for(size_t i=0;i<count&&r==XXWIDGETS_OK;++i){char *name=label_escape(values[i].name),*value=label_escape(values[i].value);char *line;size_t n;
        if(!name||!value){free(name);free(value);return XXWIDGETS_OUT_OF_MEMORY;}n=strlen(name)+strlen(value)+32;line=malloc(n);
        if(!line){free(name);free(value);return XXWIDGETS_OUT_OF_MEMORY;}snprintf(line,n,"%s = %s%s",name,values[i].available?"":"<unavailable> ",value);
        r=xxwidgets_listbox_add(list,line);free(name);free(value);free(line);}
    return r;
}
static xxwidgets_status refresh(xxwidgets_die_signature_debugger *v,int force)
{
    xxwidgets_die_signature_debug_snapshot snap={0};xxwidgets_status r;const char *name;size_t line,column;int changed=0;xxwidgets_die_signature_debug_state previous=v->state;
    r=xxwidgets_die_signature_debug_session_snapshot(v->session,&snap);if(r!=XXWIDGETS_OK)return r;
    if(!force&&v->revision==snap.revision){xxwidgets_die_signature_debug_snapshot_free(&snap);return XXWIDGETS_OK;}
    v->refreshing=1;v->state=snap.state;r=update_enabled(v,snap.state);name=snap.source_name?snap.source_name:v->primary_name;line=snap.line;column=snap.column;
    if(snap.state==XXWIDGETS_DIE_DEBUG_PAUSED&&snap.selected_frame<snap.frame_count){name=snap.frames[snap.selected_frame].source_name;line=snap.frames[snap.selected_frame].line;column=snap.frames[snap.selected_frame].column;}
    if(!name||!*name)name=v->primary_name;
    if(r==XXWIDGETS_OK&&(snap.state==XXWIDGETS_DIE_DEBUG_PAUSED||force))r=display_source(v,name,&changed);
    if(r==XXWIDGETS_OK&&snap.state==XXWIDGETS_DIE_DEBUG_PAUSED&&(changed||previous!=snap.state||v->last_line!=line||v->last_column!=column||v->last_frame!=snap.selected_frame))reveal_location(v,line,column);
    if(r==XXWIDGETS_OK)r=render_markers(v,snap.state==XXWIDGETS_DIE_DEBUG_PAUSED?line:0);
    if(r==XXWIDGETS_OK){char *safe=label_escape(v->displayed_name);char title[1400];if(!safe)r=XXWIDGETS_OUT_OF_MEMORY;else{snprintf(title,sizeof(title),"Source: %s%s",safe,v->source_available?"":" (source unavailable)");r=xxwidgets_widget_set_text(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE_NAME],title);free(safe);}}
    if(r==XXWIDGETS_OK)r=xxwidgets_listbox_clear(v->controls[XXWIDGETS_DIE_DEBUGGER_STACK]);
    for(size_t i=0;snap.state==XXWIDGETS_DIE_DEBUG_PAUSED&&i<snap.frame_count&&r==XXWIDGETS_OK;++i){char text[1800];char *source=label_escape(snap.frames[i].source_name),*function=label_escape(snap.frames[i].function_name);
        if(!source||!function){free(source);free(function);r=XXWIDGETS_OUT_OF_MEMORY;break;}
        snprintf(text,sizeof(text),"#%zu %s at %s:%zu:%zu%s",i,function,source,snap.frames[i].line,snap.frames[i].column,snap.frames[i].native?" [native]":"");free(source);free(function);r=xxwidgets_listbox_add(v->controls[XXWIDGETS_DIE_DEBUGGER_STACK],text);}
    if(r==XXWIDGETS_OK&&snap.state==XXWIDGETS_DIE_DEBUG_PAUSED&&snap.selected_frame<snap.frame_count)r=xxwidgets_widget_set_value(v->controls[XXWIDGETS_DIE_DEBUGGER_STACK],(int)snap.selected_frame);
    if(r==XXWIDGETS_OK)r=render_values(v->controls[XXWIDGETS_DIE_DEBUGGER_LOCALS],snap.locals,snap.state==XXWIDGETS_DIE_DEBUG_PAUSED?snap.local_count:0);
    if(r==XXWIDGETS_OK){xxwidgets_die_signature_debug_value values[DEBUG_MAX_WATCHES];memset(values,0,sizeof(values));
        for(size_t i=0;i<v->watch_count;++i){values[i].name=v->watches[i];values[i].value="";
            if(snap.state==XXWIDGETS_DIE_DEBUG_PAUSED)for(size_t j=0;j<snap.watch_count;++j)if(!strcmp(v->watches[i],snap.watches[j].name)){values[i]=snap.watches[j];break;}}
        r=render_values(v->controls[XXWIDGETS_DIE_DEBUGGER_WATCHES],values,v->watch_count);}
    if(r==XXWIDGETS_OK){const char *log=snap.log?snap.log:"",*error=snap.error?snap.error:"",*result=snap.result?snap.result:"";size_t n=strlen(log)+strlen(error)+strlen(result)+80;char *text=malloc(n);
        if(!text)r=XXWIDGETS_OUT_OF_MEMORY;else{snprintf(text,n,"%s%s%s%s%s",log,*error?"\nError: ":"",error,*result?"\nResult: ":"",result);
            if(strcmp(text,xxwidgets_die_signature_edit_source(v->controls[XXWIDGETS_DIE_DEBUGGER_OUTPUT]))){r=xxwidgets_die_signature_edit_set_source(v->controls[XXWIDGETS_DIE_DEBUGGER_OUTPUT],text);if(r==XXWIDGETS_OK)r=xxwidgets_die_signature_edit_set_cursor(v->controls[XXWIDGETS_DIE_DEBUGGER_OUTPUT],strlen(text));}free(text);}}
    if(r==XXWIDGETS_OK)r=update_enabled(v,snap.state);
    if(r==XXWIDGETS_OK){char text[400];if(snap.state==XXWIDGETS_DIE_DEBUG_PAUSED)snprintf(text,sizeof(text),"%s%s | Line %zu, column %zu | %zu breakpoint(s)%s",state_name(snap.state),snap.exception?" on exception":"",line,column,v->breakpoint_count,snap.truncated?" | Display truncated":"");else snprintf(text,sizeof(text),"%s | %zu breakpoint(s)%s",state_name(snap.state),v->breakpoint_count,snap.truncated?" | Display truncated":"");r=notice(v,text);}
    if(r==XXWIDGETS_OK){v->revision=snap.revision;v->state=snap.state;v->last_line=line;v->last_column=column;v->last_frame=snap.selected_frame;}
    v->refreshing=0;xxwidgets_die_signature_debug_snapshot_free(&snap);return r;
}
xxwidgets_widget *xxwidgets_die_signature_debugger_control(const xxwidgets_die_signature_debugger *v,xxwidgets_die_signature_debugger_control_id id)
{ return v&&id>=0&&id<XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT?v->controls[id]:NULL; }
xxwidgets_status xxwidgets_die_signature_debugger_create(xxwidgets_widget *owner,xxwidgets_rect bounds,xxwidgets_die_signature_debug_session *session,xxwidgets_die_signature_debugger **out)
{
    static const xxwidgets_kind kinds[]={XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_LABEL,XXWIDGETS_DIE_SIGNATURE_EDIT,XXWIDGETS_LABEL,XXWIDGETS_LISTBOX,XXWIDGETS_LABEL,XXWIDGETS_LISTBOX,XXWIDGETS_LABEL,XXWIDGETS_LISTBOX,XXWIDGETS_EDIT,XXWIDGETS_BUTTON,XXWIDGETS_BUTTON,XXWIDGETS_LISTBOX,XXWIDGETS_DIE_SIGNATURE_EDIT,XXWIDGETS_LABEL};
    static const char *const labels[]={"Start/Continue","Pause","Stop","Into","Over","Out","Toggle BP","Clear BPs","Source","","Call stack","","Locals","","Watches","","","+","-","","","Ready"};
    xxwidgets_die_signature_debugger *v;xxwidgets_rect r[XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT];xxwidgets_status result;
    if(!out)return XXWIDGETS_INVALID_ARGUMENT;*out=NULL;if(!owner||owner->kind!=XXWIDGETS_WINDOW||!session||!valid_bounds(bounds))return XXWIDGETS_INVALID_ARGUMENT;
    if(owner->app->dispatch_depth||owner->app->polling||owner->app->syncing)return XXWIDGETS_BUSY;
    v=calloc(1,sizeof(*v));if(!v)return XXWIDGETS_OUT_OF_MEMORY;v->owner=owner;v->session=session;v->revision=UINT64_MAX;
    v->primary_source=xxwidgets_strdup("");v->primary_name=xxwidgets_strdup("");v->displayed_name=xxwidgets_strdup("");if(!v->primary_source||!v->primary_name||!v->displayed_name){result=XXWIDGETS_OUT_OF_MEMORY;goto failed;}
    rects(bounds,r);for(int i=0;i<XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT;++i){result=xxwidgets_widget_create(owner->app,owner,kinds[i],labels[i],r[i],&v->controls[i]);if(result!=XXWIDGETS_OK)goto failed;}
    result=xxwidgets_die_signature_edit_set_read_only(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE],1);if(result!=XXWIDGETS_OK)goto failed;
    result=xxwidgets_die_signature_edit_set_read_only(v->controls[XXWIDGETS_DIE_DEBUGGER_OUTPUT],1);if(result!=XXWIDGETS_OK)goto failed;
    result=refresh(v,1);if(result!=XXWIDGETS_OK)goto failed;*out=v;return XXWIDGETS_OK;
failed:(void)xxwidgets_die_signature_debugger_destroy(v);return result;
}
xxwidgets_status xxwidgets_die_signature_debugger_destroy(xxwidgets_die_signature_debugger *v)
{
    xxwidgets_status r;if(!v)return XXWIDGETS_INVALID_ARGUMENT;if(!outside(v))return XXWIDGETS_BUSY;
    for(int i=0;i<XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT;++i)if(v->controls[i]){r=xxwidgets_widget_destroy(v->controls[i]);if(r!=XXWIDGETS_OK)return r;v->controls[i]=NULL;}
    free_breakpoints(v->breakpoints,v->breakpoint_count);free_watches(v->watches,v->watch_count);free(v->primary_source);free(v->primary_name);free(v->displayed_name);free(v);return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debugger_set_rect(xxwidgets_die_signature_debugger *v,xxwidgets_rect bounds)
{
    xxwidgets_rect r[XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT],old[XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT];xxwidgets_status result;
    if(!v||!valid_bounds(bounds))return XXWIDGETS_INVALID_ARGUMENT;rects(bounds,r);for(int i=0;i<XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT;++i)old[i]=v->controls[i]->rect;
    for(int i=0;i<XXWIDGETS_DIE_DEBUGGER_CONTROL_COUNT;++i){result=xxwidgets_widget_set_rect(v->controls[i],r[i]);if(result!=XXWIDGETS_OK){for(int j=0;j<i;++j)(void)xxwidgets_widget_set_rect(v->controls[j],old[j]);return result;}}return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debugger_set_source(xxwidgets_die_signature_debugger *v,const char *source,const char *name)
{
    char *copy,*label;xxwidgets_status r;if(!v||!source||!name||!xxwidgets_valid_utf8(source)||!xxwidgets_valid_utf8(name))return XXWIDGETS_INVALID_ARGUMENT;if(!outside(v))return XXWIDGETS_BUSY;
    if(!*name)name="<signature>";
    copy=xxwidgets_strdup(source);label=xxwidgets_strdup(name);if(!copy||!label){free(copy);free(label);return XXWIDGETS_OUT_OF_MEMORY;}
    r=xxwidgets_die_signature_debug_session_set_source(v->session,source,name);if(r!=XXWIDGETS_OK){free(copy);free(label);return r;}
    free(v->primary_source);free(v->primary_name);v->primary_source=copy;v->primary_name=label;v->source_available=0;v->revision=UINT64_MAX;return refresh(v,1);
}
xxwidgets_status xxwidgets_die_signature_debugger_set_source_resolver(xxwidgets_die_signature_debugger *v,xxwidgets_die_signature_debug_source_fn resolver,void *user)
{if(!v)return XXWIDGETS_INVALID_ARGUMENT;v->resolver=resolver;v->resolver_user=resolver?user:NULL;v->revision=UINT64_MAX;return XXWIDGETS_OK;}
xxwidgets_status xxwidgets_die_signature_debugger_set_breakpoints(xxwidgets_die_signature_debugger *v,const xxwidgets_die_signature_debug_breakpoint *breakpoints,size_t count)
{
    xxwidgets_die_signature_debug_breakpoint *copy=NULL;xxwidgets_status r;if(!v||count>DEBUG_MAX_BREAKPOINTS||(count&&!breakpoints))return XXWIDGETS_INVALID_ARGUMENT;
    if(count){copy=calloc(count,sizeof(*copy));if(!copy)return XXWIDGETS_OUT_OF_MEMORY;for(size_t i=0;i<count;++i){if(!breakpoints[i].line||!xxwidgets_valid_utf8(breakpoints[i].source_name?breakpoints[i].source_name:"")){free_breakpoints(copy,count);return XXWIDGETS_INVALID_ARGUMENT;}copy[i].source_name=xxwidgets_strdup(breakpoints[i].source_name?breakpoints[i].source_name:"");copy[i].line=breakpoints[i].line;if(!copy[i].source_name){free_breakpoints(copy,count);return XXWIDGETS_OUT_OF_MEMORY;}for(size_t j=0;j<i;++j)if(copy[j].line==copy[i].line&&!strcmp(copy[j].source_name,copy[i].source_name)){free_breakpoints(copy,count);return XXWIDGETS_INVALID_ARGUMENT;}}}
    r=xxwidgets_die_signature_debug_session_set_breakpoints(v->session,copy,count);if(r!=XXWIDGETS_OK){free_breakpoints(copy,count);return r;}free_breakpoints(v->breakpoints,v->breakpoint_count);v->breakpoints=copy;v->breakpoint_count=count;r=render_breakpoints(v);v->revision=UINT64_MAX;if(r==XXWIDGETS_OK)r=render_markers(v,v->state==XXWIDGETS_DIE_DEBUG_PAUSED?v->last_line:0);return r;
}
xxwidgets_status xxwidgets_die_signature_debugger_toggle_breakpoint(xxwidgets_die_signature_debugger *v)
{
    xxwidgets_die_signature_debug_breakpoint copy[DEBUG_MAX_BREAKPOINTS];size_t line,column,n=0;int found=0;xxwidgets_status r;if(!v||!v->source_available)return XXWIDGETS_UNAVAILABLE;
    r=xxwidgets_die_signature_edit_position(v->controls[XXWIDGETS_DIE_DEBUGGER_SOURCE],&line,&column);if(r!=XXWIDGETS_OK)return r;(void)column;
    for(size_t i=0;i<v->breakpoint_count;++i){const char *name=*v->breakpoints[i].source_name?v->breakpoints[i].source_name:v->primary_name;if(line==v->breakpoints[i].line&&!strcmp(name,v->displayed_name))found=1;else copy[n++]=v->breakpoints[i];}
    if(!found){if(n==DEBUG_MAX_BREAKPOINTS)return XXWIDGETS_INVALID_ARGUMENT;copy[n++]=(xxwidgets_die_signature_debug_breakpoint){v->displayed_name,line};}
    return xxwidgets_die_signature_debugger_set_breakpoints(v,copy,n);
}
xxwidgets_status xxwidgets_die_signature_debugger_set_watches(xxwidgets_die_signature_debugger *v,const char *const *identifiers,size_t count)
{
    char **copy=NULL;xxwidgets_status r;if(!v||count>DEBUG_MAX_WATCHES||(count&&!identifiers))return XXWIDGETS_INVALID_ARGUMENT;
    if(count){copy=calloc(count,sizeof(*copy));if(!copy)return XXWIDGETS_OUT_OF_MEMORY;for(size_t i=0;i<count;++i){if(!identifiers[i]){free_watches(copy,count);return XXWIDGETS_INVALID_ARGUMENT;}copy[i]=xxwidgets_strdup(identifiers[i]);if(!copy[i]){free_watches(copy,count);return XXWIDGETS_OUT_OF_MEMORY;}}}
    r=xxwidgets_die_signature_debug_session_set_watches(v->session,(const char *const*)copy,count);if(r!=XXWIDGETS_OK){free_watches(copy,count);return r;}free_watches(v->watches,v->watch_count);v->watches=copy;v->watch_count=count;v->revision=UINT64_MAX;return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debugger_run(xxwidgets_die_signature_debugger *v,int pause_on_entry)
{if(!v)return XXWIDGETS_INVALID_ARGUMENT;if(!outside(v))return XXWIDGETS_BUSY;return xxwidgets_die_signature_debug_session_start(v->session,pause_on_entry);}
xxwidgets_status xxwidgets_die_signature_debugger_command(xxwidgets_die_signature_debugger *v,xxwidgets_die_signature_debug_command command)
{if(!v)return XXWIDGETS_INVALID_ARGUMENT;return xxwidgets_die_signature_debug_session_command(v->session,command);}
xxwidgets_status xxwidgets_die_signature_debugger_handle_event(xxwidgets_die_signature_debugger *v,const xxwidgets_event *event)
{
    if(!v||!event)return XXWIDGETS_INVALID_ARGUMENT;if(v->refreshing)return XXWIDGETS_OK;
    if(event->type==XXWIDGETS_EVENT_CLICK)for(int i=0;i<8;++i)if(event->widget==v->controls[i]){v->pending=i+1;return XXWIDGETS_OK;}
    if((event->type==XXWIDGETS_EVENT_SELECT||event->type==XXWIDGETS_EVENT_ACTIVATE)&&event->widget==v->controls[XXWIDGETS_DIE_DEBUGGER_STACK]){v->pending=100;v->pending_value=event->value;}
    else if(event->type==XXWIDGETS_EVENT_ACTIVATE&&event->widget==v->controls[XXWIDGETS_DIE_DEBUGGER_BREAKPOINTS]){v->pending=103;v->pending_value=event->value;}
    else if((event->type==XXWIDGETS_EVENT_CLICK&&event->widget==v->controls[XXWIDGETS_DIE_DEBUGGER_ADD_WATCH])||(event->type==XXWIDGETS_EVENT_ACTIVATE&&event->widget==v->controls[XXWIDGETS_DIE_DEBUGGER_WATCH_INPUT]))v->pending=101;
    else if(event->type==XXWIDGETS_EVENT_CLICK&&event->widget==v->controls[XXWIDGETS_DIE_DEBUGGER_REMOVE_WATCH])v->pending=102;
    return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_signature_debugger_process_pending(xxwidgets_die_signature_debugger *v)
{
    int action,value;xxwidgets_status r=XXWIDGETS_OK;if(!v)return XXWIDGETS_INVALID_ARGUMENT;if(!outside(v))return XXWIDGETS_BUSY;action=v->pending;value=v->pending_value;v->pending=0;
    if(action==1)r=v->state==XXWIDGETS_DIE_DEBUG_PAUSED?xxwidgets_die_signature_debugger_command(v,XXWIDGETS_DIE_DEBUG_CONTINUE):active(v->state)?XXWIDGETS_OK:xxwidgets_die_signature_debugger_run(v,1);
    else if(action>=2&&action<=6){static const xxwidgets_die_signature_debug_command commands[]={XXWIDGETS_DIE_DEBUG_PAUSE,XXWIDGETS_DIE_DEBUG_STOP,XXWIDGETS_DIE_DEBUG_STEP_INTO,XXWIDGETS_DIE_DEBUG_STEP_OVER,XXWIDGETS_DIE_DEBUG_STEP_OUT};r=xxwidgets_die_signature_debugger_command(v,commands[action-2]);}
    else if(action==7)r=xxwidgets_die_signature_debugger_toggle_breakpoint(v);
    else if(action==8)r=xxwidgets_die_signature_debugger_set_breakpoints(v,NULL,0);
    else if(action==100&&value>=0)r=xxwidgets_die_signature_debug_session_select_frame(v->session,(size_t)value);
    else if(action==101){const char *names[DEBUG_MAX_WATCHES];const char *name=v->controls[XXWIDGETS_DIE_DEBUGGER_WATCH_INPUT]->text;if(v->watch_count==DEBUG_MAX_WATCHES)r=XXWIDGETS_INVALID_ARGUMENT;else{for(size_t i=0;i<v->watch_count;++i)names[i]=v->watches[i];names[v->watch_count]=name;r=xxwidgets_die_signature_debugger_set_watches(v,names,v->watch_count+1);if(r==XXWIDGETS_OK)(void)xxwidgets_widget_set_text(v->controls[XXWIDGETS_DIE_DEBUGGER_WATCH_INPUT],"");}}
    else if(action==102){int selected=-1;(void)xxwidgets_widget_get_value(v->controls[XXWIDGETS_DIE_DEBUGGER_WATCHES],&selected);if(selected>=0&&(size_t)selected<v->watch_count){const char *names[DEBUG_MAX_WATCHES];size_t n=0;for(size_t i=0;i<v->watch_count;++i)if(i!=(size_t)selected)names[n++]=v->watches[i];r=xxwidgets_die_signature_debugger_set_watches(v,names,n);}}
    else if(action==103&&value>=0&&(size_t)value<v->breakpoint_count){xxwidgets_die_signature_debug_breakpoint bp[DEBUG_MAX_BREAKPOINTS];size_t n=0;for(size_t i=0;i<v->breakpoint_count;++i)if(i!=(size_t)value)bp[n++]=v->breakpoints[i];r=xxwidgets_die_signature_debugger_set_breakpoints(v,bp,n);}
    {xxwidgets_status display=refresh(v,0);if(r==XXWIDGETS_OK)r=display;}
    if(r!=XXWIDGETS_OK){char text[180];snprintf(text,sizeof(text),"Debugger: %s",xxwidgets_status_string(r));(void)notice(v,text);}return r;
}
