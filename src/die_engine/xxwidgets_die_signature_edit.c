/* Copyright (c) 2026 hors<horsicq@gmail.com>. SPDX-License-Identifier: MIT */
#include "xxwidgets_internal.h"
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define DIE_EDIT_LIMIT (8u * 1024u * 1024u)
#define DIE_HISTORY_COUNT 64
#define DIE_HISTORY_BYTES (16u * 1024u * 1024u)
typedef struct die_snapshot { char *text; size_t caret, anchor, bytes; } die_snapshot;
struct xxwidgets_die_signature_edit_state {
    size_t length, *lines, line_count, max_columns;
    size_t *breakpoint_lines, breakpoint_count, execution_line;
    unsigned char *styles;
    size_t caret, anchor, first_line, first_column, page_rows, page_columns, desired_column;
    int read_only, vertical, modified;
    uint64_t revision;
    char *saved;
    die_snapshot undo[DIE_HISTORY_COUNT], redo[DIE_HISTORY_COUNT];
    size_t undo_count, redo_count, undo_bytes, redo_bytes;
};
typedef struct xxwidgets_die_signature_edit_state die_state;
static die_state *state(const xxwidgets_widget *w) { return w && w->kind == XXWIDGETS_DIE_SIGNATURE_EDIT ? w->die_signature_edit : NULL; }
static size_t next_byte(const char *s,size_t at) { if(!s[at])return at; do {++at;} while(((unsigned char)s[at]&0xc0)==0x80);return at; }
static size_t previous_byte(const char *s,size_t at) { if(!at)return 0;do {--at;}while(at&&((unsigned char)s[at]&0xc0)==0x80);return at; }
static int boundary(const char *s,size_t at) {return ((unsigned char)s[at]&0xc0)!=0x80;}
static size_t line_of(const die_state *s,size_t at) {size_t lo=0,hi=s->line_count;while(lo+1<hi){size_t m=lo+(hi-lo)/2;if(s->lines[m]<=at)lo=m;else hi=m;}return lo;}
static size_t line_end(const die_state *s,size_t line) {return line+1<s->line_count?s->lines[line+1]-1:s->length;}
static size_t column_at(const xxwidgets_widget *w,size_t at) {const die_state *s=state(w);size_t p=s->lines[line_of(s,at)],col=0;while(p<at){col+=w->text[p]=='\t'?4-col%4:1;p=next_byte(w->text,p);}return col;}
static size_t position_at(const xxwidgets_widget *w,size_t line,size_t column) {const die_state *s=state(w);size_t p,end,col=0;if(line>=s->line_count)line=s->line_count-1;p=s->lines[line];end=line_end(s,line);while(p<end){size_t width=w->text[p]=='\t'?4-col%4:1;if(col+width>column)break;col+=width;p=next_byte(w->text,p);}return p;}
static void clamp_view(die_state *s) {size_t rows=s->line_count>s->page_rows?s->line_count-s->page_rows:0,columns=s->max_columns+1>s->page_columns?s->max_columns+1-s->page_columns:0;if(s->first_line>rows)s->first_line=rows;if(s->first_column>columns)s->first_column=columns;}
static void reveal(xxwidgets_widget *w) {die_state *s=state(w);size_t line=line_of(s,s->caret),col=column_at(w,s->caret);if(line<s->first_line)s->first_line=line;else if(line-s->first_line>=s->page_rows)s->first_line=line-s->page_rows+1;if(col<s->first_column)s->first_column=col;else if(col-s->first_column>=s->page_columns)s->first_column=col-s->page_columns+1;clamp_view(s);}
static xxwidgets_status sync_edit(xxwidgets_widget *w) {xxwidgets_status result;++w->app->syncing;result=w->app->ops->sync(w);--w->app->syncing;return result;}
static int word(unsigned char c) {return isalnum(c)||c=='_'||c=='$'||c>=128;}
static int in_words(const char *text,size_t n,const char *list) {const char *p=list;while(*p){const char *end=strchr(p,' ');size_t m=end?(size_t)(end-p):strlen(p);if(m==n&&!memcmp(text,p,n))return 1;if(!end)break;p=end+1;}return 0;}
static void highlight(const char *t,size_t n,unsigned char *styles) {
    size_t i=0;int regex_allowed=1;
    while(i<n){size_t begin=i;unsigned char c=(unsigned char)t[i];int style=XXWIDGETS_DIE_STYLE_TEXT;
        if(c=='/'&&i+1<n&&t[i+1]=='/'){i+=2;while(i<n&&t[i]!='\n')++i;style=XXWIDGETS_DIE_STYLE_COMMENT;}
        else if(c=='/'&&i+1<n&&t[i+1]=='*'){i+=2;while(i+1<n&&!(t[i]=='*'&&t[i+1]=='/'))++i;i=i+1<n?i+2:n;style=XXWIDGETS_DIE_STYLE_COMMENT;}
        else if(c=='\''||c=='"'||c=='`'){++i;while(i<n){if(t[i]=='\\'){i+=i+1<n?2:1;continue;}if(t[i++]==(char)c)break;if(c!='`'&&t[i-1]=='\n')break;}style=XXWIDGETS_DIE_STYLE_STRING;regex_allowed=0;}
        else if(isdigit(c)||(c=='.'&&i+1<n&&isdigit((unsigned char)t[i+1]))){++i;while(i<n&&(isalnum((unsigned char)t[i])||t[i]=='.'||t[i]=='_'||((t[i]=='+'||t[i]=='-')&&(t[i-1]=='e'||t[i-1]=='E'))))++i;style=XXWIDGETS_DIE_STYLE_NUMBER;regex_allowed=0;}
        else if(word(c)){size_t after;++i;while(i<n&&word((unsigned char)t[i]))++i;after=i;while(after<n&&isspace((unsigned char)t[after]))++after;
            if(in_words(t+begin,i-begin,"break case catch class const continue debugger default delete do else export extends false finally for function if import in instanceof let new null return super switch this throw true try typeof undefined var void while with yield async await of"))style=XXWIDGETS_DIE_STYLE_KEYWORD;
            else if(in_words(t+begin,i-begin,"Binary PE ELF MachO MACH DOS MSDOS NE LE LX DEX APK ZIP Archive Image Document Text File Java SevenZip bShowType bShowVersion bShowInfo bShowOptions sVersion sOptions sName bDetected includeScript isStop _log _init _setResult _getResult"))style=XXWIDGETS_DIE_STYLE_BUILTIN;
            else if(after<n&&t[after]=='(')style=XXWIDGETS_DIE_STYLE_FUNCTION;
            regex_allowed=in_words(t+begin,i-begin,"return throw case delete typeof void new in of yield await");
        }else if(c=='/'&&regex_allowed){int in_class=0,closed=0;++i;while(i<n&&t[i]!='\n'){if(t[i]=='\\'){i+=i+1<n?2:1;continue;}if(t[i]=='[')in_class=1;else if(t[i]==']')in_class=0;else if(t[i]=='/'&&!in_class){++i;closed=1;break;}++i;}if(closed){while(i<n&&isalpha((unsigned char)t[i]))++i;style=XXWIDGETS_DIE_STYLE_REGEX;regex_allowed=0;}else{i=begin+1;regex_allowed=1;}}
        else{++i;if(!isspace(c))regex_allowed=strchr("(=:[,!&|?;{}+-*%~^<>\\",c)!=NULL;}
        memset(styles+begin,style,i-begin);
    }
}
static xxwidgets_status prepare(const char *input,char **text,unsigned char **styles,size_t **lines,size_t *length,size_t *count,size_t *max_columns) {
    size_t n=0,j=0,k=1,col=0,max=0;char *copy;unsigned char *colors;size_t *starts;
    if(!input)return XXWIDGETS_INVALID_ARGUMENT;
    while(n<=DIE_EDIT_LIMIT&&input[n])++n;
    if(n>DIE_EDIT_LIMIT||!xxwidgets_valid_utf8(input))return XXWIDGETS_INVALID_ARGUMENT;
    copy=malloc(n+1);if(!copy)return XXWIDGETS_OUT_OF_MEMORY;
    for(size_t i=0;i<n;++i){if(input[i]=='\r'){copy[j++]='\n';if(i+1<n&&input[i+1]=='\n')++i;}else copy[j++]=input[i];}copy[j]=0;n=j;
    for(size_t i=0;i<n;++i)if(copy[i]=='\n')++k;
    colors=malloc(n?n:1);starts=malloc(k*sizeof(*starts));if(!colors||!starts){free(colors);free(starts);free(copy);return XXWIDGETS_OUT_OF_MEMORY;}
    highlight(copy,n,colors);starts[0]=0;j=1;
    for(size_t i=0;i<n;){if(copy[i]=='\n'){if(col>max)max=col;col=0;starts[j++]=++i;}else{col+=copy[i]=='\t'?4-col%4:1;i=next_byte(copy,i);}}if(col>max)max=col;
    *text=copy;*styles=colors;*lines=starts;*length=n;*count=k;*max_columns=max;return XXWIDGETS_OK;
}
static void clear_stack(die_snapshot *stack,size_t *count,size_t *bytes) {for(size_t i=0;i<*count;++i)free(stack[i].text);*count=*bytes=0;}
static void push(die_snapshot *stack,size_t *count,size_t *bytes,char *text,size_t caret,size_t anchor) {
    size_t n=strlen(text)+1;
    while(*count&&(*count>=DIE_HISTORY_COUNT||n>DIE_HISTORY_BYTES-*bytes)){*bytes-=stack[0].bytes;free(stack[0].text);memmove(stack,stack+1,(--*count)*sizeof(*stack));}
    if(n>DIE_HISTORY_BYTES){free(text);return;}
    stack[*count]=(die_snapshot){text,caret,anchor,n};++*count;*bytes+=n;
}
/* All allocations and syntax/line-index work finish before publishing. Backend
 * sync failure restores the entire old document, caret, history and savepoint. */
static xxwidgets_status install(xxwidgets_widget *w,const char *text,size_t caret,size_t anchor,int clean,char **old_text) {
    die_state *old=state(w),*next;xxwidgets_status result;char *source=NULL,*saved=NULL;unsigned char *styles=NULL;size_t *lines=NULL,n=0,count=0,max=0;
    result=prepare(text,&source,&styles,&lines,&n,&count,&max);if(result!=XXWIDGETS_OK)return result;
    if(clean){saved=xxwidgets_strdup(source);if(!saved){free(source);free(styles);free(lines);return XXWIDGETS_OUT_OF_MEMORY;}}
    next=malloc(sizeof(*next));if(!next){free(source);free(styles);free(lines);free(saved);return XXWIDGETS_OUT_OF_MEMORY;}
    *next=*old;next->breakpoint_lines=NULL;next->breakpoint_count=next->execution_line=0;next->length=n;next->lines=lines;next->line_count=count;next->max_columns=max;next->styles=styles;
    next->caret=caret<=n?caret:n;next->anchor=anchor<=n?anchor:n;
    while(next->caret&&!boundary(source,next->caret))--next->caret;while(next->anchor&&!boundary(source,next->anchor))--next->anchor;
    next->vertical=0;if(clean){next->saved=saved;next->first_line=next->first_column=0;}
    next->modified=strcmp(source,next->saved)!=0;next->revision=old->revision+1;
    *old_text=w->text;w->text=source;w->die_signature_edit=next;reveal(w);result=sync_edit(w);
    if(result!=XXWIDGETS_OK){w->text=*old_text;w->die_signature_edit=old;(void)sync_edit(w);free(source);free(styles);free(lines);free(saved);free(next);*old_text=NULL;return result;}
    free(old->breakpoint_lines);free(old->styles);free(old->lines);if(clean){free(old->saved);clear_stack(next->undo,&next->undo_count,&next->undo_bytes);clear_stack(next->redo,&next->redo_count,&next->redo_bytes);}free(old);return XXWIDGETS_OK;
}
xxwidgets_status xxwidgets_die_edit_init(xxwidgets_widget *w) {
    die_state *s=calloc(1,sizeof(*s));xxwidgets_status result;char *text=NULL;if(!s)return XXWIDGETS_OUT_OF_MEMORY;
    result=prepare(w->text,&text,&s->styles,&s->lines,&s->length,&s->line_count,&s->max_columns);if(result!=XXWIDGETS_OK){free(s);return result;}
    s->saved=xxwidgets_strdup(text);if(!s->saved){free(text);free(s->styles);free(s->lines);free(s);return XXWIDGETS_OUT_OF_MEMORY;}
    s->page_rows=1;s->page_columns=1;free(w->text);w->text=text;w->die_signature_edit=s;return XXWIDGETS_OK;
}
void xxwidgets_die_edit_dispose(xxwidgets_widget *w) {die_state *s=state(w);if(!s)return;clear_stack(s->undo,&s->undo_count,&s->undo_bytes);clear_stack(s->redo,&s->redo_count,&s->redo_bytes);free(s->saved);free(s->styles);free(s->lines);free(s->breakpoint_lines);free(s);w->die_signature_edit=NULL;}
xxwidgets_status xxwidgets_die_signature_edit_set_source(xxwidgets_widget *w,const char *source) {char *old=NULL;xxwidgets_status result;if(!state(w))return XXWIDGETS_INVALID_ARGUMENT;result=install(w,source,0,0,1,&old);free(old);return result;}
const char *xxwidgets_die_signature_edit_source(const xxwidgets_widget *w) {return state(w)?w->text:NULL;}
xxwidgets_status xxwidgets_die_signature_edit_set_read_only(xxwidgets_widget *w,int value) {die_state *s=state(w);int old;xxwidgets_status result;if(!s)return XXWIDGETS_INVALID_ARGUMENT;old=s->read_only;s->read_only=value!=0;result=sync_edit(w);if(result!=XXWIDGETS_OK){s->read_only=old;(void)sync_edit(w);}return result;}
int xxwidgets_die_signature_edit_read_only(const xxwidgets_widget *w) {die_state *s=state(w);return s?s->read_only:1;}
int xxwidgets_die_signature_edit_modified(const xxwidgets_widget *w) {die_state *s=state(w);return s?s->modified:0;}
xxwidgets_status xxwidgets_die_signature_edit_mark_saved(xxwidgets_widget *w) {die_state *s=state(w);char *copy;if(!s)return XXWIDGETS_INVALID_ARGUMENT;copy=xxwidgets_strdup(w->text);if(!copy)return XXWIDGETS_OUT_OF_MEMORY;free(s->saved);s->saved=copy;s->modified=0;return XXWIDGETS_OK;}
size_t xxwidgets_die_signature_edit_cursor(const xxwidgets_widget *w) {die_state *s=state(w);return s?s->caret:SIZE_MAX;}
size_t xxwidgets_die_signature_edit_line_count(const xxwidgets_widget *w) {die_state *s=state(w);return s?s->line_count:0;}
xxwidgets_status xxwidgets_die_signature_edit_position(const xxwidgets_widget *w,size_t *line,size_t *column) {die_state *s=state(w);if(!s||!line||!column)return XXWIDGETS_INVALID_ARGUMENT;*line=line_of(s,s->caret)+1;*column=column_at(w,s->caret)+1;return XXWIDGETS_OK;}
xxwidgets_status xxwidgets_die_signature_edit_set_selection(xxwidgets_widget *w,size_t offset,size_t length) {die_state *s=state(w);size_t old,anchor,fl,fc;xxwidgets_status result;if(!s||offset>s->length||length>s->length-offset||!boundary(w->text,offset)||!boundary(w->text,offset+length))return XXWIDGETS_INVALID_ARGUMENT;old=s->caret;anchor=s->anchor;fl=s->first_line;fc=s->first_column;s->anchor=offset;s->caret=offset+length;s->vertical=0;reveal(w);result=sync_edit(w);if(result!=XXWIDGETS_OK){s->caret=old;s->anchor=anchor;s->first_line=fl;s->first_column=fc;(void)sync_edit(w);}return result;}
xxwidgets_status xxwidgets_die_signature_edit_set_cursor(xxwidgets_widget *w,size_t offset) {return xxwidgets_die_signature_edit_set_selection(w,offset,0);}
xxwidgets_status xxwidgets_die_signature_edit_get_selection(const xxwidgets_widget *w,size_t *offset,size_t *length) {die_state *s=state(w);if(!s||!offset||!length)return XXWIDGETS_INVALID_ARGUMENT;*offset=s->caret<s->anchor?s->caret:s->anchor;*length=s->caret<s->anchor?s->anchor-s->caret:s->caret-s->anchor;return XXWIDGETS_OK;}
xxwidgets_die_signature_style xxwidgets_die_signature_edit_style_at(const xxwidgets_widget *w,size_t at) {die_state *s=state(w);return s&&at<s->length?(xxwidgets_die_signature_style)s->styles[at]:XXWIDGETS_DIE_STYLE_TEXT;}
xxwidgets_status xxwidgets_die_signature_edit_insert(xxwidgets_widget *w,const char *text) {
    die_state *s=state(w);char *insert=NULL,*combined,*old=NULL;unsigned char *colors=NULL;size_t *lines=NULL,n=0,count=0,max=0,from,to;xxwidgets_status result;
    if(!s||!text)return XXWIDGETS_INVALID_ARGUMENT;if(s->read_only)return XXWIDGETS_UNAVAILABLE;
    result=prepare(text,&insert,&colors,&lines,&n,&count,&max);free(colors);free(lines);if(result!=XXWIDGETS_OK)return result;
    from=s->caret<s->anchor?s->caret:s->anchor;to=s->caret>s->anchor?s->caret:s->anchor;
    if(n>DIE_EDIT_LIMIT-(s->length-(to-from))){free(insert);return XXWIDGETS_INVALID_ARGUMENT;}
    if(n==to-from&&!memcmp(w->text+from,insert,n)){free(insert);return xxwidgets_die_signature_edit_set_cursor(w,from+n);}
    combined=malloc(s->length-(to-from)+n+1);if(!combined){free(insert);return XXWIDGETS_OUT_OF_MEMORY;}
    memcpy(combined,w->text,from);memcpy(combined+from,insert,n);memcpy(combined+from+n,w->text+to,s->length-to+1);free(insert);
    {size_t caret=s->caret,anchor=s->anchor;result=install(w,combined,from+n,from+n,0,&old);if(result==XXWIDGETS_OK){s=state(w);clear_stack(s->redo,&s->redo_count,&s->redo_bytes);push(s->undo,&s->undo_count,&s->undo_bytes,old,caret,anchor);}}
    free(combined);return result;
}
static xxwidgets_status replay(xxwidgets_widget *w,int redo) {die_state *s=state(w);die_snapshot item;size_t caret,anchor;char *old=NULL;xxwidgets_status result;if(!s)return XXWIDGETS_INVALID_ARGUMENT;if(s->read_only)return XXWIDGETS_UNAVAILABLE;if(!(redo?s->redo_count:s->undo_count))return XXWIDGETS_UNAVAILABLE;item=redo?s->redo[s->redo_count-1]:s->undo[s->undo_count-1];caret=s->caret;anchor=s->anchor;result=install(w,item.text,item.caret,item.anchor,0,&old);if(result!=XXWIDGETS_OK)return result;s=state(w);if(redo){--s->redo_count;s->redo_bytes-=item.bytes;push(s->undo,&s->undo_count,&s->undo_bytes,old,caret,anchor);}else{--s->undo_count;s->undo_bytes-=item.bytes;push(s->redo,&s->redo_count,&s->redo_bytes,old,caret,anchor);}free(item.text);return XXWIDGETS_OK;}
xxwidgets_status xxwidgets_die_signature_edit_undo(xxwidgets_widget *w) {return replay(w,0);}
xxwidgets_status xxwidgets_die_signature_edit_redo(xxwidgets_widget *w) {return replay(w,1);}
static unsigned char folded(unsigned char c) {return c>='A'&&c<='Z'?(unsigned char)(c+'a'-'A'):c;}
xxwidgets_status xxwidgets_die_signature_edit_find(xxwidgets_widget *w,const char *text,int match_case,int backwards) {
    die_state *s=state(w);size_t n,from,to,*prefix,j=0,first=SIZE_MAX,last=SIZE_MAX,preferred=SIZE_MAX;
    if(!s||!text||!xxwidgets_valid_utf8(text)||!(n=strlen(text)))return XXWIDGETS_INVALID_ARGUMENT;
    if(n>s->length)return XXWIDGETS_UNAVAILABLE;
    prefix=malloc(n*sizeof(*prefix));if(!prefix)return XXWIDGETS_OUT_OF_MEMORY;prefix[0]=0;
    /* Prefix matching keeps repeated-prefix searches linear in the document
     * and query sizes; no case-folded document allocation is needed. */
    for(size_t i=1;i<n;++i){unsigned char c=(unsigned char)text[i];if(!match_case)c=folded(c);
        while(j&&c!=(match_case?(unsigned char)text[j]:folded((unsigned char)text[j])))j=prefix[j-1];
        if(c==(match_case?(unsigned char)text[j]:folded((unsigned char)text[j])))++j;prefix[i]=j;
    }
    from=s->caret<s->anchor?s->caret:s->anchor;to=s->caret>s->anchor?s->caret:s->anchor;j=0;
    for(size_t i=0;i<s->length;++i){unsigned char c=(unsigned char)w->text[i];if(!match_case)c=folded(c);
        while(j&&c!=(match_case?(unsigned char)text[j]:folded((unsigned char)text[j])))j=prefix[j-1];
        if(c==(match_case?(unsigned char)text[j]:folded((unsigned char)text[j])))++j;
        if(j==n){size_t at=i+1-n;
            if(boundary(w->text,at)&&boundary(w->text,i+1)){
                if(first==SIZE_MAX)first=at;last=at;
                if(backwards){if(at<from)preferred=at;}
                else if(preferred==SIZE_MAX&&at>=to)preferred=at;
            }
            j=prefix[j-1];
        }
    }
    free(prefix);if(preferred==SIZE_MAX)preferred=backwards?last:first;
    return preferred==SIZE_MAX?XXWIDGETS_UNAVAILABLE:xxwidgets_die_signature_edit_set_selection(w,preferred,n);
}
void xxwidgets_die_edit_view_get(const xxwidgets_widget *w,xxwidgets_die_edit_view *view) {die_state *s=state(w);size_t n;if(!view)return;memset(view,0,sizeof(*view));if(!s)return;view->caret=s->caret;view->anchor=s->anchor;view->first_line=s->first_line;view->first_column=s->first_column;view->line_count=s->line_count;view->max_columns=s->max_columns;view->execution_line=s->execution_line;view->read_only=s->read_only;view->modified=xxwidgets_die_signature_edit_modified(w);n=s->line_count;view->gutter_columns=3;do{++view->gutter_columns;n/=10;}while(n);if(view->gutter_columns<6)view->gutter_columns=6;}
void xxwidgets_die_edit_page(xxwidgets_widget *w,size_t rows,size_t columns) {die_state *s=state(w);if(!s)return;if(!rows)rows=1;if(!columns)columns=1;if(s->page_rows!=rows||s->page_columns!=columns){s->page_rows=rows;s->page_columns=columns;reveal(w);}}
size_t xxwidgets_die_edit_caret_line(const xxwidgets_widget *w) {die_state *s=state(w);return s?line_of(s,s->caret):0;}
size_t xxwidgets_die_edit_encode(uint32_t cp,char out[5]) {size_t n;if(cp<128){out[0]=(char)cp;n=1;}else if(cp<2048){out[0]=(char)(0xc0|(cp>>6));out[1]=(char)(0x80|(cp&63));n=2;}else if(cp<65536){out[0]=(char)(0xe0|(cp>>12));out[1]=(char)(0x80|((cp>>6)&63));out[2]=(char)(0x80|(cp&63));n=3;}else{out[0]=(char)(0xf0|(cp>>18));out[1]=(char)(0x80|((cp>>12)&63));out[2]=(char)(0x80|((cp>>6)&63));out[3]=(char)(0x80|(cp&63));n=4;}out[n]=0;return n;}
static uint32_t decode(const char *t,size_t p,size_t end) {uint32_t cp=(unsigned char)t[p++];if(cp<128)return cp;if(cp<224)cp&=31;else if(cp<240)cp&=15;else cp&=7;while(p<end)cp=(cp<<6)|((unsigned char)t[p++]&63);return cp;}
void xxwidgets_die_edit_render_line(const xxwidgets_widget *w,size_t line,size_t first,xxwidgets_die_edit_cell *cells,size_t count) {
    die_state *s=state(w);size_t p,end,col=0,lo,hi;if(!cells)return;for(size_t i=0;i<count;++i)cells[i]=(xxwidgets_die_edit_cell){' ',XXWIDGETS_DIE_STYLE_TEXT,0,0};if(!s||line>=s->line_count)return;p=s->lines[line];end=line_end(s,line);lo=s->caret<s->anchor?s->caret:s->anchor;hi=s->caret>s->anchor?s->caret:s->anchor;
    while(p<end&&col<first+count){size_t next=next_byte(w->text,p),width=w->text[p]=='\t'?4-col%4:1;uint32_t cp=decode(w->text,p,next);if(cp<32||cp==127)cp=' ';for(size_t j=0;j<width;++j)if(col+j>=first&&col+j-first<count)cells[col+j-first]=(xxwidgets_die_edit_cell){cp,(xxwidgets_die_signature_style)s->styles[p],p>=lo&&p<hi,s->caret==p&&j==0};col+=width;p=next;}
    if(p==end&&col>=first&&col-first<count){cells[col-first].selected=end>=lo&&end<hi;cells[col-first].caret=s->caret==end;}
}
void xxwidgets_die_edit_scroll(xxwidgets_widget *w,int lines,int columns) {
    die_state *s=state(w);size_t max;if(!s)return;clamp_view(s);
    max=s->line_count>s->page_rows?s->line_count-s->page_rows:0;
    if(lines<0)s->first_line=(size_t)(-(int64_t)lines)>s->first_line?0:s->first_line-(size_t)(-(int64_t)lines);
    else s->first_line=(size_t)lines>max-s->first_line?max:s->first_line+(size_t)lines;
    max=s->max_columns+1>s->page_columns?s->max_columns+1-s->page_columns:0;
    if(columns<0)s->first_column=(size_t)(-(int64_t)columns)>s->first_column?0:s->first_column-(size_t)(-(int64_t)columns);
    else s->first_column=(size_t)columns>max-s->first_column?max:s->first_column+(size_t)columns;
    (void)sync_edit(w);
}
void xxwidgets_die_edit_point(xxwidgets_widget *w,size_t line,size_t column,int extend) {die_state *s=state(w);if(!s||!xxwidgets_focusable(w))return;s->caret=position_at(w,line,column);if(!extend)s->anchor=s->caret;s->vertical=0;reveal(w);if(sync_edit(w)==XXWIDGETS_OK)xxwidgets_emit(w,XXWIDGETS_EVENT_SELECT,0);}
xxwidgets_status xxwidgets_die_edit_input(xxwidgets_widget *w,const char *text) {xxwidgets_status r;die_state *s=state(w);uint64_t revision;if(!s||!xxwidgets_focusable(w))return XXWIDGETS_UNAVAILABLE;revision=s->revision;r=xxwidgets_die_signature_edit_insert(w,text);if(r==XXWIDGETS_OK&&state(w)->revision!=revision)xxwidgets_emit(w,XXWIDGETS_EVENT_CHANGE,0);return r;}
static int clipboard(xxwidgets_widget *w,int key) {die_state *s=state(w);size_t from,to,n=0;uint64_t revision=s->revision;size_t caret=s->caret,anchor=s->anchor;char *copy;xxwidgets_status result;from=s->caret<s->anchor?s->caret:s->anchor;to=s->caret>s->anchor?s->caret:s->anchor;
    if(key=='v'){if(s->read_only||!w->app->ops->paste_text)return 1;result=w->app->ops->paste_text(w->parent,NULL,0,&n);if((result!=XXWIDGETS_OK&&result!=XXWIDGETS_BUFFER_TOO_SMALL)||!n||n>DIE_EDIT_LIMIT+1)return 1;copy=malloc(n);if(!copy)return 1;result=w->app->ops->paste_text(w->parent,copy,n,&n);if(result==XXWIDGETS_OK)(void)xxwidgets_die_edit_input(w,copy);free(copy);return 1;}
    if(from==to||!w->app->ops->copy_text||(key=='x'&&s->read_only))return 1;copy=malloc(to-from+1);if(!copy)return 1;memcpy(copy,w->text+from,to-from);copy[to-from]=0;result=w->app->ops->copy_text(w->parent,copy);free(copy);if(result==XXWIDGETS_OK&&key=='x'&&state(w)->revision==revision&&state(w)->caret==caret&&state(w)->anchor==anchor)(void)xxwidgets_die_edit_input(w,"");return 1;
}
int xxwidgets_die_edit_key(xxwidgets_widget *w,int key,unsigned mods) {
    die_state *s=state(w);size_t at,line,old,anchor;int ctrl=(mods&(XXWIDGETS_MOD_CTRL|XXWIDGETS_MOD_META))!=0,shift=(mods&XXWIDGETS_MOD_SHIFT)!=0;xxwidgets_status result;
    if(!s||!xxwidgets_focusable(w))return 0;if(key==XXWIDGETS_KEY_ESCAPE||key==27)return 0;
    if(key==XXWIDGETS_KEY_TAB||key==9){if(ctrl)return 0;if(!s->read_only){char spaces[5]="    ";spaces[4-column_at(w,s->caret)%4]=0;(void)xxwidgets_die_edit_input(w,spaces);}return 1;}
    if(ctrl){int c=key>=1&&key<=26?key+'a'-1:key;if(c>='A'&&c<='Z')c+='a'-'A';if(c=='a'){if(xxwidgets_die_signature_edit_set_selection(w,0,s->length)==XXWIDGETS_OK)xxwidgets_emit(w,XXWIDGETS_EVENT_SELECT,0);return 1;}if(c=='c'||c=='x'||c=='v')return clipboard(w,c);if(c=='z'||c=='y'){result=(c=='y'||shift)?xxwidgets_die_signature_edit_redo(w):xxwidgets_die_signature_edit_undo(w);if(result==XXWIDGETS_OK)xxwidgets_emit(w,XXWIDGETS_EVENT_CHANGE,0);return 1;}if(key<XXWIDGETS_KEY_F1)return 0;}
    if(key==XXWIDGETS_KEY_ENTER||key==10||key==13){if(!s->read_only){size_t start=s->lines[line_of(s,s->caret)],n=0;char *indent;while(start+n<s->caret&&(w->text[start+n]==' '||w->text[start+n]=='\t'))++n;indent=malloc(n+2);if(indent){indent[0]='\n';memcpy(indent+1,w->text+start,n);indent[n+1]=0;(void)xxwidgets_die_edit_input(w,indent);free(indent);}}return 1;}
    if(key==XXWIDGETS_KEY_BACKSPACE||key==8||key==127||key==XXWIDGETS_KEY_DELETE){if(s->read_only)return 1;old=s->caret;anchor=s->anchor;if(s->caret==s->anchor){if(key==XXWIDGETS_KEY_DELETE)s->caret=next_byte(w->text,s->caret);else s->anchor=previous_byte(w->text,s->caret);}result=xxwidgets_die_edit_input(w,"");if(result!=XXWIDGETS_OK){s=state(w);s->caret=old;s->anchor=anchor;(void)sync_edit(w);}return 1;}
    old=s->caret;anchor=s->anchor;at=old;line=line_of(s,at);
    switch(key){
    case XXWIDGETS_KEY_LEFT:at=!shift&&s->anchor!=at?(s->anchor<at?s->anchor:at):previous_byte(w->text,at);break;
    case XXWIDGETS_KEY_RIGHT:at=!shift&&s->anchor!=at?(s->anchor>at?s->anchor:at):next_byte(w->text,at);break;
    case XXWIDGETS_KEY_HOME:at=ctrl?0:s->lines[line];break;
    case XXWIDGETS_KEY_END:at=ctrl?s->length:line_end(s,line);break;
    case XXWIDGETS_KEY_UP:case XXWIDGETS_KEY_DOWN:case XXWIDGETS_KEY_PAGEUP:case XXWIDGETS_KEY_PAGEDOWN:{size_t delta=key==XXWIDGETS_KEY_PAGEUP||key==XXWIDGETS_KEY_PAGEDOWN?s->page_rows:1;if(!s->vertical)s->desired_column=column_at(w,at);if(key==XXWIDGETS_KEY_UP||key==XXWIDGETS_KEY_PAGEUP)line=delta>line?0:line-delta;else line=delta>s->line_count-1-line?s->line_count-1:line+delta;at=position_at(w,line,s->desired_column);s->vertical=1;break;}
    default:if(!ctrl&&!(mods&XXWIDGETS_MOD_ALT)&&key>=32&&key<0x110000){char text[5];xxwidgets_die_edit_encode((uint32_t)key,text);(void)xxwidgets_die_edit_input(w,text);return 1;}return 0;
    }
    if(key!=XXWIDGETS_KEY_UP&&key!=XXWIDGETS_KEY_DOWN&&key!=XXWIDGETS_KEY_PAGEUP&&key!=XXWIDGETS_KEY_PAGEDOWN)s->vertical=0;s->caret=at;if(!shift)s->anchor=at;reveal(w);if(sync_edit(w)==XXWIDGETS_OK&&(old!=s->caret||anchor!=s->anchor))xxwidgets_emit(w,XXWIDGETS_EVENT_SELECT,0);return 1;
}
uint32_t xxwidgets_die_edit_color(xxwidgets_die_signature_style style) {static const uint32_t colors[]={0x202020,0x173e9c,0x842a91,0x196b36,0xb44338,0x975c11,0x245fbd,0x7b4e21};return style>=0&&style<=XXWIDGETS_DIE_STYLE_REGEX?colors[style]:colors[0];}

static int line_compare(const void *a,const void *b) {size_t x=*(const size_t*)a,y=*(const size_t*)b;return x<y?-1:x>y?1:0;}
xxwidgets_status xxwidgets_die_signature_edit_set_debug_markers(xxwidgets_widget *w,const size_t *lines,size_t count,size_t execution_line) {
    die_state *s=state(w);size_t *copy=NULL,*previous,previous_count,previous_execution;xxwidgets_status result;
    if(!s||count>4096||(count&&!lines)||execution_line>s->line_count)return XXWIDGETS_INVALID_ARGUMENT;
    if(count){copy=malloc(count*sizeof(*copy));if(!copy)return XXWIDGETS_OUT_OF_MEMORY;memcpy(copy,lines,count*sizeof(*copy));qsort(copy,count,sizeof(*copy),line_compare);for(size_t i=0;i<count;++i)if(!copy[i]||copy[i]>s->line_count||(i&&copy[i]==copy[i-1])){free(copy);return XXWIDGETS_INVALID_ARGUMENT;}}
    previous=s->breakpoint_lines;previous_count=s->breakpoint_count;previous_execution=s->execution_line;
    s->breakpoint_lines=copy;s->breakpoint_count=count;s->execution_line=execution_line;result=sync_edit(w);
    if(result!=XXWIDGETS_OK){s->breakpoint_lines=previous;s->breakpoint_count=previous_count;s->execution_line=previous_execution;(void)sync_edit(w);free(copy);}else free(previous);return result;
}
void xxwidgets_die_edit_line_marker(const xxwidgets_widget *w,size_t line,int *breakpoint,int *execution) {
    die_state *s=state(w);size_t lo=0,hi=s?s->breakpoint_count:0;if(breakpoint)*breakpoint=0;if(execution)*execution=s&&s->execution_line==line+1;
    if(!s)return;while(lo<hi){size_t mid=lo+(hi-lo)/2;if(s->breakpoint_lines[mid]<line+1)lo=mid+1;else hi=mid;}
    if(breakpoint)*breakpoint=lo<s->breakpoint_count&&s->breakpoint_lines[lo]==line+1;
}
