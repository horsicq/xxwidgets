#include "xxwidgets_internal.h"
#include "die_engine/xxwidgets_die_signature_edit_internal.h"

#import <AppKit/AppKit.h>
#include <math.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* This private bridge uses manual reference counting; the public ABI is C. */
@class XXWidgetsDelegate;
@class XXWidgetsApplicationDelegate;

typedef struct cocoa_app_state {
    CGFloat cell_width;
    CGFloat cell_height;
    id saved_delegate;
    NSMenu *saved_menu;
    NSMenu *base_menu;
    NSApplicationActivationPolicy saved_policy;
    XXWidgetsApplicationDelegate *delegate;
    NSFont *fonts[XXWIDGETS_FONT_ROLE_COUNT];
    NSFont *formatted_font;
} cocoa_app_state;

typedef struct cocoa_widget_state {
    NSView *root; /* The view attached to the parent, including list scrollbars. */
    NSScrollView *browser_scroll;
    NSTextField *browser_address;
    NSButton *browser_up;
    XXWidgetsDelegate *delegate;
    size_t rendered_items;
    uint64_t rendered_hex_revision;
    int hex_rows_initialized;
    CGFloat hex_row_width;
    xxwidgets_rect applied_rect;
    int has_rect;
    NSPopover *combo_popover;
    NSView *combo_checks;
    uint64_t combo_revision;
    int combo_initialized;
    NSArray *tree_items; /* Stable identities for NSOutlineView's source nodes. */
    uint64_t tree_content_revision;
    int tree_collapsing, tree_before_collapse;
    NSTextView *about_text; /* Owned by the window's content view. */
    NSFont *preview_font;
    NSFont *hex_font;
    NSFont *applied_font;
    NSTextView *field_editor;
    NSMenu *menu;
} cocoa_widget_state;

static xxwidgets_app *active_app;
static int finished_launching;

static void cocoa_size_table(xxwidgets_widget *widget);

/* Returned fonts are retained. Build every role before changing any views so
 * a font allocation failure leaves the preceding application fonts intact. */
static NSFont *cocoa_font_create(const xxwidgets_font *settings, int formatted)
{
    CGFloat size = settings->point_size ? settings->point_size : [NSFont systemFontSize];
    int generic = settings->family[0] && xxwidgets_font_is_monospace(settings);
    NSFont *base = formatted || generic ? [NSFont userFixedPitchFontOfSize:size] : [NSFont systemFontOfSize:size];
    NSFontManager *manager = [NSFontManager sharedFontManager];
    if (!base) base = [NSFont systemFontOfSize:size];
    if (!base) return nil;
    if (settings->family[0] && !generic) {
        NSString *family = [NSString stringWithUTF8String:settings->family];
        NSFont *named = [NSFont fontWithName:family size:size];
        if (!named) named = [manager convertFont:base toFamily:family];
        if (named) base = named;
    }
    NSFont *font = [manager convertFont:base toNotHaveTrait:NSBoldFontMask | NSItalicFontMask];
    if (!font) font = base;
    NSFontTraitMask traits = (settings->bold ? NSBoldFontMask : 0) | (settings->italic ? NSItalicFontMask : 0);
    if (traits) {
        NSFont *styled = [manager convertFont:font toHaveTrait:traits];
        if (styled) font = styled;
    }
    return [font retain];
}

static NSFont *cocoa_role_font(xxwidgets_widget *widget, xxwidgets_font_role role)
{
    cocoa_app_state *app = widget->app->platform;
    NSFont *font = app->fonts[role];
    return font ? font : [NSFont systemFontOfSize:[NSFont systemFontSize]];
}

/* Cell coordinates describe the visible control, not AppKit's extra bezel /
 * focus-ring margins. A one-row button otherwise gets a 20 pt frame although
 * its standard 20 pt bezel needs a 32 pt view. Keep those margins outside the
 * grid and never trim a single-line field below its font's natural height. */
static void cocoa_fit_control(xxwidgets_widget *widget)
{
    if (widget->kind != XXWIDGETS_BUTTON && widget->kind != XXWIDGETS_CHECKBOX &&
        widget->kind != XXWIDGETS_EDIT && widget->kind != XXWIDGETS_LABEL && !xxwidgets_combo_kind(widget)) return;
    cocoa_app_state *app = widget->app->platform;
    NSControl *control = widget->native;
    NSRect slot = NSMakeRect(widget->rect.x * app->cell_width, widget->rect.y * app->cell_height,
                             widget->rect.width * app->cell_width, widget->rect.height * app->cell_height);
    NSRect frame = [control frameForAlignmentRect:slot];
    CGFloat minimum = ceil([[control cell] cellSize].height);
    if (widget->kind == XXWIDGETS_EDIT || widget->kind == XXWIDGETS_LABEL) {
        /* Single-line text stays vertically aligned with the native buttons
         * even when a shared TUI layout reserves two rows for the control. */
        frame.size.height = widget->kind == XXWIDGETS_LABEL ? MIN(minimum, slot.size.height) : minimum;
        frame.origin.y = floor(NSMidY(slot) - frame.size.height / 2);
    } else if (frame.size.height < minimum) {
        frame.origin.y = floor(frame.origin.y - (minimum - frame.size.height) / 2);
        frame.size.height = minimum;
    }
    if (!NSEqualRects([control frame], frame)) [control setFrame:frame];
}

static void cocoa_widget_fonts(xxwidgets_widget *widget)
{
    cocoa_app_state *app = widget->app->platform;
    cocoa_widget_state *state = widget->platform;
    id native = widget->native;
    if (!state || !native) return;
    if (widget->kind == XXWIDGETS_WINDOW) {
        [state->about_text setFont:cocoa_role_font(widget, XXWIDGETS_FONT_TEXT_EDITS)];
        return;
    }
    NSFont *font = state->preview_font ? state->preview_font :
        cocoa_role_font(widget, xxwidgets_widget_font_role(widget));
    if (!state->preview_font && (widget->kind == XXWIDGETS_HEXVIEW || widget->kind == XXWIDGETS_ARCHIVEVIEW || widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT || xxwidgets_debugger_table_kind(widget))) {
        font = widget->kind == XXWIDGETS_HEXVIEW && state->hex_font ? state->hex_font :
            app->formatted_font ? app->formatted_font : [NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]];
        if (!font) font = cocoa_role_font(widget, XXWIDGETS_FONT_TABLE_VIEWS);
    }
    int font_changed = state->applied_font != font;
    if (xxwidgets_list_kind(widget)) {
        NSTableView *table = native;
        for (NSTableColumn *column in [table tableColumns]) {
            [[column dataCell] setFont:font];
            [[column headerCell] setFont:font];
        }
        [table setRowHeight:MAX(app->cell_height, ceil([font ascender] - [font descender] + [font leading]) + 4)];
        if (font_changed && widget->kind == XXWIDGETS_HEXVIEW) {
            CGFloat width = MAX(1, [@"0" sizeWithAttributes:@{NSFontAttributeName:font}].width);
            for (size_t i = 0; i < 3; ++i) {
                NSTableColumn *column = [[table tableColumns] objectAtIndex:i];
                [column setWidth:xxwidgets_hexview_column_width(widget, i) * width];
                NSAttributedString *title = [[[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:xxwidgets_hexview_title(widget, i)]
                    attributes:@{NSFontAttributeName:font}] autorelease];
                [[column headerCell] setAttributedStringValue:title];
            }
        }
        if (font_changed && xxwidgets_formatted_rows(widget) && widget->kind != XXWIDGETS_SCANRESULTS && widget->kind != XXWIDGET_SYSTEMPROCESSESVIEWER && !xxwidgets_binaryviewer_kind(widget) && !xxwidgets_debugger_table_kind(widget) &&
            !xxwidgets_entropy_table(widget) && widget->kind != XXWIDGETS_HEXVIEW && widget->kind != XXWIDGETS_DATA_STRUCT_TABLE &&
            widget->kind != XXWIDGETS_ARCHIVEBROWSER) {
            NSDictionary *attributes = [NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName];
            state->hex_row_width = 0;
            for (size_t row = 0; row < widget->item_count; ++row) {
                NSString *text;
                CGFloat extra = 12;
                if (widget->kind == XXWIDGETS_TREEVIEW) {
                    size_t node = xxwidgets_treeview_node_at_row(widget, row);
                    text = [NSString stringWithUTF8String:xxwidgets_treeview_display_text(widget, node)];
                    extra = (xxwidgets_treeview_depth(widget, node) + 1) * [(NSOutlineView *)table indentationPerLevel] + 24;
                } else text = [NSString stringWithUTF8String:widget->items[row]];
                CGFloat width = ceil([text sizeWithAttributes:attributes].width) + extra;
                if (width > state->hex_row_width) state->hex_row_width = width;
            }
        }
        cocoa_size_table(widget);
        [table setNeedsDisplay:YES];
    } else if ([native respondsToSelector:@selector(setFont:)]) {
        [native setFont:font];
        if (widget->kind == XXWIDGETS_EDIT) [[(NSTextField *)native currentEditor] setFont:font];
        cocoa_fit_control(widget);
    }
    if (widget->kind == XXWIDGETS_COMBOBOX) [[(NSPopUpButton *)native menu] setFont:font];
    for (NSButton *check in [state->combo_checks subviews]) [check setFont:font];
    [state->browser_address setFont:cocoa_role_font(widget, XXWIDGETS_FONT_TEXT_EDITS)];
    [state->browser_up setFont:cocoa_role_font(widget, XXWIDGETS_FONT_CONTROLS)];
    if (font_changed) {
        [font retain]; [state->applied_font release]; state->applied_font = font;
    }
}

static xxwidgets_status cocoa_apply_fonts(xxwidgets_app *app, const xxwidgets_font_options *options)
{
    @autoreleasepool {
        cocoa_app_state *state = app->platform;
        NSFont *fonts[XXWIDGETS_FONT_ROLE_COUNT] = {nil};
        NSFont *formatted;
        size_t role;
        for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
            fonts[role] = cocoa_font_create(&options->fonts[role], 0);
            if (!fonts[role]) {
                for (size_t i = 0; i < role; ++i) [fonts[i] release];
                return XXWIDGETS_OUT_OF_MEMORY;
            }
        }
        formatted = cocoa_font_create(&options->fonts[XXWIDGETS_FONT_TABLE_VIEWS], 1);
        if (!formatted) {
            for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) [fonts[role] release];
            return XXWIDGETS_OUT_OF_MEMORY;
        }
        for (role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) {
            [state->fonts[role] release];
            state->fonts[role] = fonts[role];
        }
        [state->formatted_font release]; state->formatted_font = formatted;
        for (xxwidgets_widget *widget = app->widgets; widget; widget = widget->next)
            cocoa_widget_fonts(widget);
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_preview_font(xxwidgets_widget *widget, xxwidgets_font_role role,
    const xxwidgets_font *settings)
{
    @autoreleasepool {
        cocoa_widget_state *state = widget->platform;
        NSFont *font = cocoa_font_create(settings, 0);
        (void)role;
        if (!font) return XXWIDGETS_OUT_OF_MEMORY;
        [state->preview_font release]; state->preview_font = font;
        cocoa_widget_fonts(widget);
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_hexview_font(xxwidgets_widget *widget, const xxwidgets_font *settings)
{
    @autoreleasepool {
        cocoa_widget_state *state = widget->platform;
        NSFont *font = cocoa_font_create(settings, 1);
        if (!font) return XXWIDGETS_OUT_OF_MEMORY;
        [state->hex_font release]; state->hex_font = font;
        cocoa_widget_fonts(widget);
        return XXWIDGETS_OK;
    }
}

static int rect_equal(xxwidgets_rect a, xxwidgets_rect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static NSString *edit_text(NSTextField *field)
{
    NSText *editor = [field currentEditor];
    return editor ? [editor string] : [field stringValue];
}

static NSArray *cocoa_drop_urls(xxwidgets_widget *window, id<NSDraggingInfo> sender)
{
    if (!xxwidgets_can_drop_files(window) || !([sender draggingSourceOperationMask] & NSDragOperationCopy)) return nil;
    NSArray *urls = [[sender draggingPasteboard] readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    for (NSURL *url in urls)
        if (![[url path] length] || ([[url host] length] && ![[url host] isEqualToString:@"localhost"])) return nil;
    return [urls count] ? urls : nil;
}

static BOOL cocoa_drop_files(xxwidgets_widget *window, id<NSDraggingInfo> sender)
{
    NSArray *urls = cocoa_drop_urls(window, sender);
    if (!urls) return NO;
    const char **paths = calloc([urls count], sizeof(*paths));
    if (!paths) return NO;
    for (NSUInteger i = 0; i < [urls count]; ++i) paths[i] = [[urls[i] path] UTF8String];
    int accepted = xxwidgets_emit_drop_files(window, paths, [urls count]);
    free(paths);
    return accepted != 0;
}

static void cocoa_plot_draw(void *user, int kind, double x, double y, double x2, double y2, uint32_t rgb, const char *text)
{
    (void)user;
    NSColor *color = [NSColor colorWithCalibratedRed:((rgb >> 16) & 255) / 255.0 green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1];
    [color set];
    if (kind == 0) { NSBezierPath *path = [NSBezierPath bezierPath]; [path moveToPoint:NSMakePoint(x,y)]; [path lineToPoint:NSMakePoint(x2,y2)]; [path stroke]; }
    else if (kind == 1) NSRectFill(NSMakeRect(x,y,x2-x,y2-y));
    else [[NSString stringWithUTF8String:text] drawAtPoint:NSMakePoint(x,y) withAttributes:@{NSFontAttributeName:[NSFont userFixedPitchFontOfSize:11], NSForegroundColorAttributeName:color}];
}
@interface XXWidgetsCanvas : NSView {
@public xxwidgets_widget *widget;
    NSImage *cachedImage; uint64_t imageRevision;
}
@end
@implementation XXWidgetsCanvas
- (BOOL)isFlipped {return YES;}
- (BOOL)acceptsFirstResponder {return YES;}
- (void)dealloc {[cachedImage release];[super dealloc];}
- (void)resetCursorRects {[super resetCursorRects];if(widget->kind==XXWIDGETS_SPLITTER)[self addCursorRect:[self bounds] cursor:widget->rect.height>widget->rect.width?[NSCursor resizeLeftRightCursor]:[NSCursor resizeUpDownCursor]];}
- (void)drawRect:(NSRect)dirty {
    (void)dirty;NSRect bounds=[self bounds];[[NSColor colorWithCalibratedWhite:0.95 alpha:1] setFill];NSRectFill(bounds);
    if(widget->kind==XXWIDGETS_SPLITTER){[[NSColor separatorColor] setFill];NSRectFill(bounds);return;}
    if(!widget->canvas_pixels||!widget->canvas_width||!widget->canvas_height)return;
    if(!cachedImage||imageRevision!=widget->canvas_revision){[cachedImage release];cachedImage=nil;
        NSBitmapImageRep *rep=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:widget->canvas_width pixelsHigh:widget->canvas_height bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:widget->canvas_width*4 bitsPerPixel:32];
        if(rep){unsigned char *data=[rep bitmapData];for(size_t i=0;i<(size_t)widget->canvas_width*widget->canvas_height;++i){uint32_t p=widget->canvas_pixels[i];data[i*4]=(unsigned char)(p>>24);data[i*4+1]=(unsigned char)(p>>16);data[i*4+2]=(unsigned char)(p>>8);data[i*4+3]=(unsigned char)p;}
            cachedImage=[[NSImage alloc] initWithSize:NSMakeSize(widget->canvas_width,widget->canvas_height)];[cachedImage addRepresentation:rep];[rep release];imageRevision=widget->canvas_revision;}}
    double x,y,w,h;xxwidgets_canvas_fit(widget,bounds.size.width,bounds.size.height,&x,&y,&w,&h);
    [[NSGraphicsContext currentContext] setImageInterpolation:NSImageInterpolationNone];[cachedImage drawInRect:NSMakeRect(x,y,w,h) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    for(size_t i=0;i<widget->canvas_label_count;++i){xxwidgets_canvas_label *label=widget->canvas_labels+i;uint32_t c=label->color;NSColor *color=[NSColor colorWithCalibratedRed:(c>>24)/255.0 green:((c>>16)&255)/255.0 blue:((c>>8)&255)/255.0 alpha:1];
        [[NSString stringWithUTF8String:label->text] drawAtPoint:NSMakePoint(x+label->x*w/widget->canvas_width,y+label->y*h/widget->canvas_height) withAttributes:@{NSFontAttributeName:[NSFont userFixedPitchFontOfSize:11],NSForegroundColorAttributeName:color}];}
    if(widget->value>=0){double px=x+(widget->value%widget->canvas_width)*w/widget->canvas_width,py=y+(widget->value/widget->canvas_width)*h/widget->canvas_height;[[NSColor systemRedColor]setStroke];NSFrameRectWithWidth(NSMakeRect(px,py,MAX(2,w/widget->canvas_width),MAX(2,h/widget->canvas_height)),1.5);}
}
- (void)mouseDown:(NSEvent *)event {[[self window]makeFirstResponder:self];NSPoint p=[self convertPoint:[event locationInWindow]fromView:nil];NSSize size=[self bounds].size;if(widget->kind==XXWIDGETS_SPLITTER)xxwidgets_splitter_input(widget,p.x,p.y,size.width,size.height);else xxwidgets_canvas_input(widget,p.x,p.y,size.width,size.height);}
- (void)mouseDragged:(NSEvent *)event {[self mouseDown:event];}
- (void)keyDown:(NSEvent *)event {int key=0;switch([event keyCode]){case 123:key=XXWIDGETS_KEY_LEFT;break;case 124:key=XXWIDGETS_KEY_RIGHT;break;case 125:key=XXWIDGETS_KEY_DOWN;break;case 126:key=XXWIDGETS_KEY_UP;break;case 115:key=XXWIDGETS_KEY_HOME;break;case 119:key=XXWIDGETS_KEY_END;break;case 116:key=XXWIDGETS_KEY_PAGEUP;break;case 121:key=XXWIDGETS_KEY_PAGEDOWN;break;case 36:case 76:key=13;break;default:break;}if(widget->kind==XXWIDGETS_SPLITTER?xxwidgets_splitter_key(widget,key):xxwidgets_canvas_key(widget,key))return;[super keyDown:event];}
@end

@interface XXWidgetsEntropyPlot : NSView {
@public
    xxwidgets_widget *widget;
}
@end
@implementation XXWidgetsEntropyPlot
- (BOOL)isFlipped { return YES; }
- (void)drawRect:(NSRect)rect
{
    (void)rect;
    xxwidgets_entropy_draw(widget, [self bounds].size.width, [self bounds].size.height, 7, 14, cocoa_plot_draw, NULL);
}
- (void)mouseDown:(NSEvent *)event
{
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    xxwidgets_entropy_plot_input(widget, point.x, [self bounds].size.width, 7);
}
@end

@interface XXWidgetsContentView : NSView {
@public
    xxwidgets_widget *drop_window;
}
@end

@implementation XXWidgetsContentView
- (BOOL)isFlipped { return YES; }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{ return cocoa_drop_urls(drop_window, sender) ? NSDragOperationCopy : NSDragOperationNone; }
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender
{ return [self draggingEntered:sender]; }
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender
{ return cocoa_drop_urls(drop_window, sender) != nil; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{ return cocoa_drop_files(drop_window, sender); }
@end

/* AppKit's shared text editor is itself a drop destination. Route file drops
 * on an active path edit to the window too; retain normal text dragging. */
@interface XXWidgetsFieldEditor : NSTextView {
@public
    xxwidgets_widget *drop_window;
}
@end
@implementation XXWidgetsFieldEditor
- (BOOL)isFileDrop:(id<NSDraggingInfo>)sender
{
    return drop_window && drop_window->accept_file_drops &&
        [[sender draggingPasteboard] availableTypeFromArray:@[NSPasteboardTypeFileURL]] != nil;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
    if ([self isFileDrop:sender]) return cocoa_drop_urls(drop_window, sender) ? NSDragOperationCopy : NSDragOperationNone;
    return [super draggingEntered:sender];
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender
{
    if ([self isFileDrop:sender]) return [self draggingEntered:sender];
    return [super draggingUpdated:sender];
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender
{
    if ([self isFileDrop:sender]) return cocoa_drop_urls(drop_window, sender) != nil;
    return [super prepareForDragOperation:sender];
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
    if ([self isFileDrop:sender]) return cocoa_drop_files(drop_window, sender);
    return [super performDragOperation:sender];
}
@end

static int cocoa_key_input(NSEvent *event, unsigned int *out_key, unsigned int *out_modifiers)
{
    NSString *characters;
    unsigned int key, modifiers = 0;
    if ([event type] != NSEventTypeKeyDown) return 0;
    characters = ([event modifierFlags] & (NSEventModifierFlagControl | NSEventModifierFlagCommand))
        ? [event charactersIgnoringModifiers] : [event characters];
    if (![characters length]) return 0;
    key = [characters characterAtIndex:0];
    if (key >= 0xd800 && key <= 0xdbff && [characters length] >= 2) {
        unsigned int low = [characters characterAtIndex:1];
        if (low >= 0xdc00 && low <= 0xdfff) key = 0x10000 + ((key - 0xd800) << 10) + low - 0xdc00;
    }
    if (key >= NSF1FunctionKey && key <= NSF24FunctionKey) key = XXWIDGETS_KEY_F1 + key - NSF1FunctionKey;
    else switch (key) {
    case 27: key = XXWIDGETS_KEY_ESCAPE; break;
    case '\r': case NSEnterCharacter: key = XXWIDGETS_KEY_ENTER; break;
    case '\t': case NSBackTabCharacter: key = XXWIDGETS_KEY_TAB; break;
    case 127: case NSBackspaceCharacter: key = XXWIDGETS_KEY_BACKSPACE; break;
    case NSDeleteFunctionKey: key = XXWIDGETS_KEY_DELETE; break;
    case NSInsertFunctionKey: key = XXWIDGETS_KEY_INSERT; break;
    case NSHomeFunctionKey: key = XXWIDGETS_KEY_HOME; break;
    case NSEndFunctionKey: key = XXWIDGETS_KEY_END; break;
    case NSPageUpFunctionKey: key = XXWIDGETS_KEY_PAGEUP; break;
    case NSPageDownFunctionKey: key = XXWIDGETS_KEY_PAGEDOWN; break;
    case NSUpArrowFunctionKey: key = XXWIDGETS_KEY_UP; break;
    case NSDownArrowFunctionKey: key = XXWIDGETS_KEY_DOWN; break;
    case NSLeftArrowFunctionKey: key = XXWIDGETS_KEY_LEFT; break;
    case NSRightArrowFunctionKey: key = XXWIDGETS_KEY_RIGHT; break;
    default: break;
    }
    if ([event modifierFlags] & NSEventModifierFlagControl) modifiers |= XXWIDGETS_MOD_CTRL;
    if ([event modifierFlags] & NSEventModifierFlagOption) modifiers |= XXWIDGETS_MOD_ALT;
    if ([event modifierFlags] & NSEventModifierFlagShift) modifiers |= XXWIDGETS_MOD_SHIFT;
    if ([event modifierFlags] & NSEventModifierFlagCommand) modifiers |= XXWIDGETS_MOD_META;
    *out_key = key;
    *out_modifiers = modifiers;
    return 1;
}

/* The signature model owns source, selection, undo, and lexical styles. Native
 * text storage would duplicate those states, so this view only draws the
 * visible cells and uses AppKit's text input service for Unicode composition. */
@interface XXWidgetsDieSignatureView : NSView <NSTextInputClient, NSMenuItemValidation> {
@public
    xxwidgets_widget *signature_widget;
@private
    NSFont *signature_font;
    NSScroller *vertical_scroller, *horizontal_scroller;
    NSString *marked_text;
    NSRange marked_selection;
    CGFloat cell_width, line_height, gutter_width, scroller_width;
    size_t page_rows, page_columns;
    NSPoint caret_point;
    CGFloat wheel_lines, wheel_columns;
}
- (void)setFont:(NSFont *)font;
- (void)refresh;
- (void)scrollChanged:(NSScroller *)sender;
- (void)editKey:(int)key modifiers:(unsigned int)modifiers;
- (BOOL)handleKey:(int)key modifiers:(unsigned int)modifiers;
@end

static NSColor *cocoa_signature_color(xxwidgets_die_signature_style style)
{
    uint32_t rgb = xxwidgets_die_edit_color(style);
    NSColor *color = [NSColor colorWithCalibratedRed:((rgb >> 16) & 255) / 255.0
        green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1];
    if (@available(macOS 10.14, *)) {
        NSAppearance *drawing = [NSApp effectiveAppearance];
        if (@available(macOS 11.0, *)) drawing = [NSAppearance currentDrawingAppearance];
        NSString *appearance = [drawing bestMatchFromAppearancesWithNames:
            @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
        if ([appearance isEqualToString:NSAppearanceNameDarkAqua]) {
            CGFloat hue, saturation, brightness, alpha;
            [color getHue:&hue saturation:&saturation brightness:&brightness alpha:&alpha];
            color = [NSColor colorWithCalibratedHue:hue saturation:saturation * 0.65 brightness:MAX(0.85, brightness) alpha:alpha];
        }
    }
    return color;
}

/* Public selection offsets are UTF-8 bytes. AppKit text services use UTF-16;
 * reject a replacement that would split either representation's scalar. */
static int cocoa_signature_utf16_bytes(NSString *source, NSRange range, size_t *start, size_t *length)
{
    NSUInteger count = [source length], end;
    if (range.location == NSNotFound || range.location > count || range.length > count - range.location) return 0;
    end = range.location + range.length;
    if ((range.location && range.location < count && [source characterAtIndex:range.location] >= 0xdc00 && [source characterAtIndex:range.location] <= 0xdfff) ||
        (end && end < count && [source characterAtIndex:end] >= 0xdc00 && [source characterAtIndex:end] <= 0xdfff)) return 0;
    *start = [[source substringToIndex:range.location] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    *length = [[source substringWithRange:range] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    return 1;
}

@implementation XXWidgetsDieSignatureView
- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (!self) return nil;
    signature_font = [[NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]] retain];
    scroller_width = [NSScroller scrollerWidthForControlSize:NSControlSizeRegular scrollerStyle:NSScrollerStyleLegacy];
    vertical_scroller = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, scroller_width, 100)];
    horizontal_scroller = [[NSScroller alloc] initWithFrame:NSMakeRect(0, 0, 100, scroller_width)];
    if (!signature_font || !vertical_scroller || !horizontal_scroller) { [self release]; return nil; }
    for (NSScroller *scroller in @[vertical_scroller, horizontal_scroller]) {
        [scroller setScrollerStyle:NSScrollerStyleLegacy];
        [scroller setTarget:self]; [scroller setAction:@selector(scrollChanged:)];
        [scroller setContinuous:YES]; [self addSubview:scroller];
    }
    marked_selection = NSMakeRange(0, 0);
    return self;
}
- (void)dealloc
{
    [vertical_scroller setTarget:nil]; [horizontal_scroller setTarget:nil];
    [vertical_scroller release]; [horizontal_scroller release];
    [signature_font release]; [marked_text release];
    [super dealloc];
}
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstResponder { return signature_widget && xxwidgets_focusable(signature_widget); }
- (BOOL)becomeFirstResponder { [self setNeedsDisplay:YES]; return [self acceptsFirstResponder]; }
- (BOOL)resignFirstResponder { [self unmarkText]; [self setNeedsDisplay:YES]; return YES; }
- (void)resetCursorRects
{
    [super resetCursorRects];
    NSRect area = [self bounds];
    area.size.width = MAX(0, area.size.width - scroller_width);
    area.size.height = MAX(0, area.size.height - scroller_width);
    [self addCursorRect:area cursor:[NSCursor IBeamCursor]];
}
- (void)setFont:(NSFont *)font
{
    if (![font isFixedPitch]) {
        NSFont *fixed = [NSFont userFixedPitchFontOfSize:[font pointSize]];
        NSFont *styled = [[NSFontManager sharedFontManager] convertFont:fixed toHaveTrait:
            [[NSFontManager sharedFontManager] traitsOfFont:font] & (NSBoldFontMask | NSItalicFontMask)];
        font = styled ? styled : fixed;
    }
    if (signature_font != font) { [font retain]; [signature_font release]; signature_font = font; }
    [self refresh];
}
- (void)setFrameSize:(NSSize)size { [super setFrameSize:size]; [self refresh]; }
- (void)refresh
{
    if (!signature_widget) return;
    xxwidgets_die_edit_view view;
    xxwidgets_die_edit_view_get(signature_widget, &view);
    cell_width = MAX(1, ceil([@"0" sizeWithAttributes:@{NSFontAttributeName:signature_font}].width));
    line_height = MAX(1, ceil([signature_font ascender] - [signature_font descender] + [signature_font leading]) + 3);
    gutter_width = (view.gutter_columns + 1) * cell_width;
    NSSize size = [self bounds].size;
    CGFloat width = MAX(0, size.width - scroller_width), height = MAX(0, size.height - scroller_width);
    [vertical_scroller setFrame:NSMakeRect(width, 0, scroller_width, height)];
    [horizontal_scroller setFrame:NSMakeRect(gutter_width, height, MAX(0, width - gutter_width), scroller_width)];
    page_rows = MAX((size_t)1, (size_t)floor(height / line_height));
    page_columns = MAX((size_t)1, (size_t)floor(MAX(0, width - gutter_width - 4) / cell_width));
    xxwidgets_die_edit_page(signature_widget, page_rows, page_columns);
    xxwidgets_die_edit_view_get(signature_widget, &view);
    size_t vertical_range = view.line_count > page_rows ? view.line_count - page_rows : 0;
    size_t total_columns = view.max_columns + 1; /* Include the end-of-line caret cell. */
    size_t horizontal_range = total_columns > page_columns ? total_columns - page_columns : 0;
    [vertical_scroller setEnabled:vertical_range && xxwidgets_focusable(signature_widget)];
    [vertical_scroller setKnobProportion:MIN(1.0, (double)page_rows / MAX((size_t)1, view.line_count))];
    [vertical_scroller setDoubleValue:vertical_range ? (double)view.first_line / vertical_range : 0];
    [horizontal_scroller setEnabled:horizontal_range && xxwidgets_focusable(signature_widget)];
    [horizontal_scroller setKnobProportion:MIN(1.0, (double)page_columns / total_columns)];
    [horizontal_scroller setDoubleValue:horizontal_range ? (double)view.first_column / horizontal_range : 0];
    [self setNeedsDisplay:YES];
    [[self window] invalidateCursorRectsForView:self];
    [[self inputContext] invalidateCharacterCoordinates];
}
- (void)scrollChanged:(NSScroller *)sender
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget)) return;
    xxwidgets_die_edit_view view;
    xxwidgets_die_edit_view_get(signature_widget, &view);
    BOOL vertical = sender == vertical_scroller;
    size_t page = vertical ? page_rows : page_columns;
    size_t total = vertical ? view.line_count : view.max_columns + 1;
    size_t first = vertical ? view.first_line : view.first_column;
    size_t range = total > page ? total - page : 0;
    int delta = 0;
    switch ([sender hitPart]) {
    case NSScrollerDecrementPage: delta = -(int)MIN(page, (size_t)INT_MAX); break;
    case NSScrollerIncrementPage: delta = (int)MIN(page, (size_t)INT_MAX); break;
    case NSScrollerKnob: case NSScrollerKnobSlot:
        delta = (int)llround([sender doubleValue] * range) - (int)first; break;
    default: break;
    }
    xxwidgets_die_edit_scroll(signature_widget, vertical ? delta : 0, vertical ? 0 : delta);
    [self refresh];
}
- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    NSRect bounds = [self bounds];
    [[NSColor textBackgroundColor] setFill]; NSRectFill(bounds);
    if (!signature_widget || !signature_font) return;
    xxwidgets_die_edit_view view;
    xxwidgets_die_edit_view_get(signature_widget, &view);
    CGFloat width = MAX(0, bounds.size.width - scroller_width);
    CGFloat height = MAX(0, bounds.size.height - scroller_width);
    [[NSColor controlBackgroundColor] setFill]; NSRectFill(NSMakeRect(0, 0, MIN(gutter_width, width), height));
    [[NSColor separatorColor] setFill]; NSRectFill(NSMakeRect(gutter_width - 1, 0, 1, height));
    NSRect text_rect = NSMakeRect(gutter_width, 0, MAX(0, width - gutter_width), height);
    size_t cells_count = page_columns + 1;
    xxwidgets_die_edit_cell *cells = calloc(cells_count, sizeof(*cells));
    if (!cells) return;
    BOOL focused = [[self window] firstResponder] == self && [[self window] isKeyWindow];
    size_t current = xxwidgets_die_edit_caret_line(signature_widget);
    caret_point = NSMakePoint(gutter_width + 2, 0);
    NSColor *selection = focused ? [NSColor selectedTextBackgroundColor] : [NSColor unemphasizedSelectedTextBackgroundColor];
    NSDictionary *number_attributes = @{NSFontAttributeName:signature_font, NSForegroundColorAttributeName:[NSColor secondaryLabelColor]};
    for (size_t row = 0; row <= page_rows && view.first_line + row < view.line_count; ++row) {
        size_t line = view.first_line + row;
        CGFloat y = row * line_height;
        int breakpoint, execution;
        xxwidgets_die_edit_line_marker(signature_widget, line, &breakpoint, &execution);
        NSString *number = [NSString stringWithFormat:@"%zu", line + 1];
        [number drawAtPoint:NSMakePoint(MAX(1, gutter_width - cell_width - [number sizeWithAttributes:number_attributes].width), y + 1)
            withAttributes:number_attributes];
        if (breakpoint) {
            CGFloat diameter = MAX(3, MIN(cell_width, line_height) * 0.65);
            [[NSColor systemRedColor] setFill];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect((cell_width - diameter) / 2,
                y + (line_height - diameter) / 2, diameter, diameter)] fill];
        }
        if (execution) {
            CGFloat left = cell_width + 1, right = cell_width * 2 - 1, middle = y + line_height / 2;
            NSBezierPath *arrow = [NSBezierPath bezierPath];
            [arrow moveToPoint:NSMakePoint(left, middle - line_height * 0.20)];
            [arrow lineToPoint:NSMakePoint(right - cell_width * 0.35, middle - line_height * 0.20)];
            [arrow lineToPoint:NSMakePoint(right - cell_width * 0.35, middle - line_height * 0.36)];
            [arrow lineToPoint:NSMakePoint(right, middle)];
            [arrow lineToPoint:NSMakePoint(right - cell_width * 0.35, middle + line_height * 0.36)];
            [arrow lineToPoint:NSMakePoint(right - cell_width * 0.35, middle + line_height * 0.20)];
            [arrow lineToPoint:NSMakePoint(left, middle + line_height * 0.20)];
            [arrow closePath]; [[NSColor systemOrangeColor] setFill]; [arrow fill];
            [[[NSColor systemYellowColor] colorWithAlphaComponent:0.20] setFill];
            NSRectFill(NSIntersectionRect(text_rect, NSMakeRect(gutter_width, y, text_rect.size.width, line_height)));
        } else if (!view.read_only && line == current) {
            [[NSColor colorWithCalibratedWhite:0.5 alpha:0.08] setFill];
            NSRectFill(NSIntersectionRect(text_rect, NSMakeRect(gutter_width, y, text_rect.size.width, line_height)));
        }
        xxwidgets_die_edit_render_line(signature_widget, line, view.first_column, cells, cells_count);
        [NSGraphicsContext saveGraphicsState]; NSRectClip(text_rect);
        for (size_t column = 0; column < cells_count; ++column) {
            xxwidgets_die_edit_cell *cell = cells + column;
            NSRect rect = NSMakeRect(gutter_width + 2 + column * cell_width, y, cell_width, line_height);
            if (cell->selected) { [selection setFill]; NSRectFill(rect); }
            if (cell->scalar && cell->scalar != ' ') {
                char utf8[5]; xxwidgets_die_edit_encode(cell->scalar, utf8);
                NSString *text = [NSString stringWithUTF8String:utf8];
                NSColor *color = cell->selected && focused ? [NSColor selectedTextColor] :
                    cell->style == XXWIDGETS_DIE_STYLE_TEXT ? [NSColor textColor] : cocoa_signature_color(cell->style);
                [NSGraphicsContext saveGraphicsState]; NSRectClip(rect);
                [text drawAtPoint:NSMakePoint(rect.origin.x, y + 1)
                    withAttributes:@{NSFontAttributeName:signature_font, NSForegroundColorAttributeName:color}];
                [NSGraphicsContext restoreGraphicsState];
            }
            if (cell->caret) {
                caret_point = rect.origin;
                if (focused) { [[NSColor textColor] setFill]; NSRectFill(NSMakeRect(rect.origin.x, y + 1, 1, line_height - 2)); }
            }
        }
        [NSGraphicsContext restoreGraphicsState];
    }
    if ([marked_text length]) {
        [NSGraphicsContext saveGraphicsState]; NSRectClip(text_rect);
        [marked_text drawAtPoint:NSMakePoint(caret_point.x, caret_point.y + 1)
            withAttributes:@{NSFontAttributeName:signature_font, NSForegroundColorAttributeName:[NSColor textColor],
                NSBackgroundColorAttributeName:[NSColor textBackgroundColor], NSUnderlineStyleAttributeName:@(NSUnderlineStyleSingle)}];
        [NSGraphicsContext restoreGraphicsState];
    }
    free(cells);
    [[NSColor separatorColor] setStroke]; NSFrameRect(bounds);
    if (focused) { [[NSColor keyboardFocusIndicatorColor] setStroke]; NSFrameRectWithWidth(NSInsetRect(bounds, 1, 1), 1); }
}
- (void)mousePoint:(NSEvent *)event extend:(BOOL)extend
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget)) return;
    [[self window] makeFirstResponder:self]; [self unmarkText];
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    xxwidgets_die_edit_view view; xxwidgets_die_edit_view_get(signature_widget, &view);
    int lines = point.y < 0 ? -1 : point.y >= page_rows * line_height ? 1 : 0;
    int columns = point.x < gutter_width ? -1 : point.x >= gutter_width + page_columns * cell_width ? 1 : 0;
    if (extend && (lines || columns)) {
        xxwidgets_die_edit_scroll(signature_widget, lines, columns);
        xxwidgets_die_edit_view_get(signature_widget, &view);
    }
    size_t row = (size_t)MAX(0, floor(point.y / line_height));
    size_t column = (size_t)MAX(0, floor((point.x - gutter_width - 2) / cell_width + 0.5));
    [self retain];
    xxwidgets_die_edit_point(signature_widget, view.first_line + MIN(row, page_rows), view.first_column + column, extend);
    [self refresh]; [self release];
}
- (void)mouseDown:(NSEvent *)event { [self mousePoint:event extend:([event modifierFlags] & NSEventModifierFlagShift) != 0]; }
- (void)mouseDragged:(NSEvent *)event { [self mousePoint:event extend:YES]; }
- (void)scrollWheel:(NSEvent *)event
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget)) return;
    CGFloat scale_y = [event hasPreciseScrollingDeltas] ? line_height : 1;
    CGFloat scale_x = [event hasPreciseScrollingDeltas] ? cell_width : 1;
    wheel_lines -= [event scrollingDeltaY] / MAX(1, scale_y);
    wheel_columns -= [event scrollingDeltaX] / MAX(1, scale_x);
    int lines = (int)wheel_lines, columns = (int)wheel_columns;
    wheel_lines -= lines; wheel_columns -= columns;
    if (lines || columns) { xxwidgets_die_edit_scroll(signature_widget, lines, columns); [self refresh]; }
}
- (void)editKey:(int)key modifiers:(unsigned int)modifiers
{
    (void)[self handleKey:key modifiers:modifiers];
}
- (BOOL)handleKey:(int)key modifiers:(unsigned int)modifiers
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget)) return NO;
    /* CHANGE/SELECT handlers may destroy this control synchronously. */
    [self retain]; [self unmarkText];
    BOOL handled = xxwidgets_die_edit_key(signature_widget, key, modifiers) != 0;
    [self refresh]; [self release];
    return handled;
}
- (void)keyDown:(NSEvent *)event
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget)) { [super keyDown:event]; return; }
    if ([self hasMarkedText] && !([event modifierFlags] & (NSEventModifierFlagControl | NSEventModifierFlagCommand))) {
        [self interpretKeyEvents:@[event]];
        return;
    }
    unsigned int key, modifiers;
    if (cocoa_key_input(event, &key, &modifiers)) {
        if (key == XXWIDGETS_KEY_TAB && (modifiers & XXWIDGETS_MOD_CTRL)) {
            if (modifiers & XXWIDGETS_MOD_SHIFT) [[self window] selectPreviousKeyView:self];
            else [[self window] selectNextKeyView:self];
            return;
        }
        if (key >= XXWIDGETS_KEY_F1 || (modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META))) {
            if ([self handleKey:(int)key modifiers:modifiers]) return;
            [super keyDown:event]; return;
        }
    }
    [self interpretKeyEvents:@[event]];
}
- (void)doCommandBySelector:(SEL)selector
{
    unsigned int modifiers = ([[NSApp currentEvent] modifierFlags] & NSEventModifierFlagShift) ? XXWIDGETS_MOD_SHIFT : 0;
    int key = 0;
    if (selector == @selector(insertNewline:)) key = XXWIDGETS_KEY_ENTER;
    else if (selector == @selector(insertTab:) || selector == @selector(insertBacktab:)) key = XXWIDGETS_KEY_TAB;
    else if (selector == @selector(deleteBackward:)) key = XXWIDGETS_KEY_BACKSPACE;
    else if (selector == @selector(deleteForward:)) key = XXWIDGETS_KEY_DELETE;
    else if (selector == @selector(moveLeft:) || selector == @selector(moveLeftAndModifySelection:)) key = XXWIDGETS_KEY_LEFT;
    else if (selector == @selector(moveRight:) || selector == @selector(moveRightAndModifySelection:)) key = XXWIDGETS_KEY_RIGHT;
    else if (selector == @selector(moveUp:) || selector == @selector(moveUpAndModifySelection:)) key = XXWIDGETS_KEY_UP;
    else if (selector == @selector(moveDown:) || selector == @selector(moveDownAndModifySelection:)) key = XXWIDGETS_KEY_DOWN;
    else if (selector == @selector(moveToBeginningOfLine:)) key = XXWIDGETS_KEY_HOME;
    else if (selector == @selector(moveToEndOfLine:)) key = XXWIDGETS_KEY_END;
    else if (selector == @selector(pageUp:)) key = XXWIDGETS_KEY_PAGEUP;
    else if (selector == @selector(pageDown:)) key = XXWIDGETS_KEY_PAGEDOWN;
    else if (selector == @selector(cancelOperation:)) { [self unmarkText]; return; }
    if (key) [self editKey:key modifiers:modifiers];
    else if ([self respondsToSelector:selector]) [NSApp sendAction:selector to:self from:self];
    else [super doCommandBySelector:selector];
}
- (void)copy:(id)sender { (void)sender; [self editKey:'c' modifiers:XXWIDGETS_MOD_META]; }
- (void)cut:(id)sender { (void)sender; [self editKey:'x' modifiers:XXWIDGETS_MOD_META]; }
- (void)paste:(id)sender { (void)sender; [self editKey:'v' modifiers:XXWIDGETS_MOD_META]; }
- (void)selectAll:(id)sender { (void)sender; [self editKey:'a' modifiers:XXWIDGETS_MOD_META]; }
- (void)undo:(id)sender { (void)sender; [self editKey:'z' modifiers:XXWIDGETS_MOD_META]; }
- (void)redo:(id)sender { (void)sender; [self editKey:'y' modifiers:XXWIDGETS_MOD_META]; }
- (NSMenu *)menuForEvent:(NSEvent *)event
{
    (void)event;
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@"Signature"] autorelease];
    NSArray *titles = @[@"Undo", @"Redo", @"Cut", @"Copy", @"Paste", @"Select All"];
    SEL selectors[] = {@selector(undo:), @selector(redo:), @selector(cut:), @selector(copy:), @selector(paste:), @selector(selectAll:)};
    for (NSUInteger i = 0; i < [titles count]; ++i) {
        if (i == 2) [menu addItem:[NSMenuItem separatorItem]];
        NSMenuItem *item = [menu addItemWithTitle:titles[i] action:selectors[i] keyEquivalent:@""];
        [item setTarget:self];
    }
    return menu;
}
- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget)) return NO;
    SEL action = [item action];
    if (action == @selector(copy:) || action == @selector(cut:)) {
        size_t offset, length;
        xxwidgets_die_signature_edit_get_selection(signature_widget, &offset, &length);
        return length && (action != @selector(cut:) || !xxwidgets_die_signature_edit_read_only(signature_widget));
    }
    if (action == @selector(selectAll:)) return YES;
    return !xxwidgets_die_signature_edit_read_only(signature_widget);
}
- (BOOL)hasMarkedText { return [marked_text length] != 0; }
- (NSRange)markedRange
{
    NSRange selected = [self selectedRange];
    return [marked_text length] ? NSMakeRange(selected.location - marked_selection.location, [marked_text length]) : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange
{
    if (!signature_widget) return NSMakeRange(NSNotFound, 0);
    size_t offset = 0, length = 0;
    xxwidgets_die_signature_edit_get_selection(signature_widget, &offset, &length);
    const char *source = xxwidgets_die_signature_edit_source(signature_widget);
    NSString *prefix = [[[NSString alloc] initWithBytes:source length:offset encoding:NSUTF8StringEncoding] autorelease];
    NSString *selected = [[[NSString alloc] initWithBytes:source + offset length:length encoding:NSUTF8StringEncoding] autorelease];
    return [marked_text length] ? NSMakeRange([prefix length] + marked_selection.location, marked_selection.length) :
        NSMakeRange([prefix length], [selected length]);
}
- (void)setMarkedText:(id)text selectedRange:(NSRange)selection replacementRange:(NSRange)replacement
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget) || xxwidgets_die_signature_edit_read_only(signature_widget)) return;
    NSString *string = [text isKindOfClass:[NSAttributedString class]] ? [text string] : text;
    if (replacement.location != NSNotFound && ![self hasMarkedText]) {
        size_t offset, length;
        NSString *source = [NSString stringWithUTF8String:xxwidgets_die_signature_edit_source(signature_widget)];
        if (!cocoa_signature_utf16_bytes(source, replacement, &offset, &length)) return;
        xxwidgets_die_signature_edit_set_selection(signature_widget, offset, length);
    }
    NSString *copy = [string copy]; [marked_text release]; marked_text = copy;
    selection.location = MIN(selection.location, [string length]);
    selection.length = MIN(selection.length, [string length] - selection.location);
    marked_selection = selection; [self setNeedsDisplay:YES];
}
- (void)unmarkText { [marked_text release]; marked_text = nil; marked_selection = NSMakeRange(0, 0); [self setNeedsDisplay:YES]; }
- (void)insertText:(id)text replacementRange:(NSRange)replacement
{
    if (!signature_widget || !xxwidgets_focusable(signature_widget) || xxwidgets_die_signature_edit_read_only(signature_widget)) return;
    NSString *string = [text isKindOfClass:[NSAttributedString class]] ? [text string] : text;
    if (replacement.location != NSNotFound && ![self hasMarkedText]) {
        size_t offset, length;
        NSString *source = [NSString stringWithUTF8String:xxwidgets_die_signature_edit_source(signature_widget)];
        if (!cocoa_signature_utf16_bytes(source, replacement, &offset, &length)) return;
        xxwidgets_die_signature_edit_set_selection(signature_widget, offset, length);
    }
    [self retain];
    [string retain];
    [self unmarkText];
    const char *utf8 = [string UTF8String];
    if (utf8) xxwidgets_die_edit_input(signature_widget, utf8);
    [string release]; [self refresh]; [self release];
}
- (NSArray *)validAttributesForMarkedText { return @[NSUnderlineStyleAttributeName, NSForegroundColorAttributeName, NSBackgroundColorAttributeName]; }
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    if (!signature_widget) return nil;
    NSString *source = [NSString stringWithUTF8String:xxwidgets_die_signature_edit_source(signature_widget)];
    if (range.location == NSNotFound || range.location > [source length]) return nil;
    range.length = MIN(range.length, [source length] - range.location);
    if (actual) *actual = range;
    return [[[NSAttributedString alloc] initWithString:[source substringWithRange:range]
        attributes:@{NSFontAttributeName:signature_font}] autorelease];
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    (void)range;
    if (actual) *actual = [self selectedRange];
    size_t line = 1, column = 1;
    xxwidgets_die_edit_view view;
    xxwidgets_die_edit_view_get(signature_widget, &view);
    xxwidgets_die_signature_edit_position(signature_widget, &line, &column);
    CGFloat x = gutter_width + 2 + ((double)column - 1 - (double)view.first_column) * cell_width;
    CGFloat y = ((double)line - 1 - (double)view.first_line) * line_height;
    NSRect rect = [self convertRect:NSMakeRect(x, y, cell_width, line_height) toView:nil];
    return [[self window] convertRectToScreen:rect];
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    if (!signature_widget) return NSNotFound;
    point = [self convertPoint:[[self window] convertPointFromScreen:point] fromView:nil];
    xxwidgets_die_edit_view view;
    xxwidgets_die_edit_view_get(signature_widget, &view);
    size_t target_line = view.first_line + (size_t)MAX(0, floor(point.y / MAX(1, line_height)));
    size_t target_column = view.first_column + (size_t)MAX(0, floor((point.x - gutter_width - 2) / MAX(1, cell_width)));
    const unsigned char *p = (const unsigned char *)xxwidgets_die_signature_edit_source(signature_widget);
    size_t line = 0, column = 0;
    NSUInteger index = 0;
    while (*p) {
        if (line == target_line && (column >= target_column || *p == '\n')) break;
        if (*p == '\n') { ++line; column = 0; }
        else column += *p == '\t' ? 4 - column % 4 : 1;
        index += *p >= 0xf0 ? 2 : 1;
        do { ++p; } while ((*p & 0xc0) == 0x80);
    }
    return index;
}
@end

@interface XXWidgetsDelegate : NSObject <NSWindowDelegate, NSTextFieldDelegate,
    NSTableViewDataSource, NSTableViewDelegate, NSOutlineViewDataSource, NSOutlineViewDelegate> {
@public
    xxwidgets_widget *widget;
}
- (void)clicked:(id)sender;
- (void)menuActivated:(id)sender;
- (BOOL)validateMenuItem:(NSMenuItem *)item;
- (void)editActivated:(id)sender;
- (void)comboSelected:(id)sender;
- (void)comboOpen:(id)sender;
- (void)comboChecked:(id)sender;
- (void)browserUp:(id)sender;
- (void)browserActivated:(id)sender;
- (void)treeActivated:(id)sender;
- (void)treeClicked:(id)sender;
- (void)tableActivated:(id)sender;
@end

@interface XXWidgetsTree : NSOutlineView {
@public
    xxwidgets_widget *tree_widget;
}
@end

@interface XXWidgetsHexHeader : NSTableHeaderView
@end
@interface XXWidgetsHexSymbolCell : NSTextFieldCell {
@public
    xxwidgets_widget *hex_widget;
    size_t hex_row;
}
@end
@interface XXWidgetsHexTable : NSTableView {
@public
    xxwidgets_widget *hex_widget;
}
- (NSMenu *)hexMenu:(NSInteger)column;
- (void)hexMenuAction:(id)sender;
@end

/* Keep the full-file map outside the table's scrolling document. Its narrow
 * gutter is reserved by tile rather than covering the last symbol column. */
@interface XXWidgetsHexOverview : NSView {
@public
    xxwidgets_widget *hex_widget;
    BOOL tracking;
}
@end
@interface XXWidgetsHexScroll : NSScrollView {
@public
    xxwidgets_widget *hex_widget;
    XXWidgetsHexOverview *overview;
    BOOL tiling_overview;
    BOOL overview_reserved;
    NSRect content_unreserved, content_reserved;
    NSRect header_unreserved, header_reserved;
    NSRect horizontal_unreserved, horizontal_reserved;
}
- (void)refreshOverview;
@end

@implementation XXWidgetsHexOverview
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (void)drawRect:(NSRect)dirty
{
    NSRect bounds = [self bounds];
    [[NSColor controlBackgroundColor] setFill];
    NSRectFill(dirty);
    if (!hex_widget || !xxwidgets_hexview_overview_size(hex_widget) || bounds.size.height < 1) return;
    size_t height = (size_t)ceil(bounds.size.height);
    size_t first = (size_t)MAX(0, floor(NSMinY(dirty)));
    size_t last = MIN(height, (size_t)MAX(0, ceil(NSMaxY(dirty))));
    for (size_t row = first; row < last; ++row) {
        uint32_t rgb;
        if (!xxwidgets_hexview_overview_color(hex_widget, row, height, &rgb)) continue;
        [[NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0
            green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:1] setFill];
        NSRectFill(NSMakeRect(3, row, MAX(0, bounds.size.width - 3), 1));
    }
    /* A separate left rail shows the displayed page and cursor without
     * changing any application's marker color. No source reads are needed. */
    NSTableView *table = hex_widget->native;
    NSRect visible = [table visibleRect];
    CGFloat row_height = [table rowHeight] + [table intercellSpacing].height;
    uint64_t size = xxwidgets_hexview_overview_size(hex_widget);
    uint64_t origin = hex_widget->hex_source_read ? 0 : hex_widget->hex_base;
    uint64_t page_first = hex_widget->hex_base - origin;
    uint64_t page_last = page_first + hex_widget->hex_size;
    if (row_height > 0 && hex_widget->hex_columns) {
        uint64_t skip = (uint64_t)MAX(0, floor(NSMinY(visible) / row_height)) * hex_widget->hex_columns;
        uint64_t end = (uint64_t)MAX(0, ceil(NSMaxY(visible) / row_height)) * hex_widget->hex_columns;
        page_last = page_first + MIN((uint64_t)hex_widget->hex_size, end);
        page_first += MIN((uint64_t)hex_widget->hex_size, skip);
    }
    CGFloat top = floor((long double)MIN(page_first, size) * bounds.size.height / size);
    CGFloat bottom = ceil((long double)MIN(page_last, size) * bounds.size.height / size);
    [[NSColor secondaryLabelColor] setFill];
    NSRectFill(NSMakeRect(0, top, 2, MAX(2, bottom - top)));
    if (hex_widget->hex_cursor < hex_widget->hex_size) {
        uint64_t cursor = hex_widget->hex_base - origin + hex_widget->hex_cursor;
        CGFloat cursor_y = size > 1 ? floor((long double)MIN(cursor, size - 1) * (height - 1) / (size - 1)) : 0;
        [[NSColor labelColor] setFill];
        NSRectFill(NSMakeRect(0, cursor_y, 3, 1));
    }
}
- (void)jump:(NSEvent *)event
{
    if (!hex_widget || !xxwidgets_focusable(hex_widget) || !xxwidgets_hexview_overview_size(hex_widget)) return;
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    size_t height = (size_t)ceil([self bounds].size.height);
    if (!height) return;
    size_t row = (size_t)MAX(0, MIN((CGFloat)height - 1, floor(point.y)));
    [[self window] makeFirstResponder:hex_widget->native];
    (void)xxwidgets_hexview_overview_activate(hex_widget, row, height);
}
- (void)mouseDown:(NSEvent *)event { tracking = YES; [self jump:event]; }
- (void)mouseDragged:(NSEvent *)event { if (tracking) [self jump:event]; }
- (void)mouseUp:(NSEvent *)event { (void)event; tracking = NO; }
- (void)resetCursorRects { [self addCursorRect:[self bounds] cursor:[NSCursor pointingHandCursor]]; }
@end

@implementation XXWidgetsHexScroll
- (id)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        overview = [[XXWidgetsHexOverview alloc] initWithFrame:NSZeroRect];
        [overview setAccessibilityElement:YES];
        [overview setAccessibilityRole:NSAccessibilitySliderRole];
        [overview setAccessibilityLabel:@"File overview"];
        [overview setToolTip:@"File overview — click or drag to navigate"];
        [self addSubview:overview];
        [overview release];
        [[self contentView] setPostsBoundsChangedNotifications:YES];
        [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(overviewBoundsChanged:)
            name:NSViewBoundsDidChangeNotification object:[self contentView]];
    }
    return self;
}
- (void)dealloc
{
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [super dealloc];
}
- (void)overviewBoundsChanged:(NSNotification *)notification
{ (void)notification; [overview setNeedsDisplay:YES]; }
- (void)tile
{
    if (tiling_overview) return;
    tiling_overview = YES;
    /* AppKit can skip recomputing an unchanged tile. Restore our preceding
     * reservation first so frequent selection syncs never shrink it again. */
    NSClipView *clip = [self contentView];
    NSTableView *table = (NSTableView *)[self documentView];
    NSView *header_clip = [[table headerView] superview];
    NSScroller *horizontal = [self horizontalScroller];
    if (overview_reserved) {
        if (NSEqualRects([clip frame], content_reserved)) [clip setFrame:content_unreserved];
        if (header_clip && [header_clip superview] == self && NSEqualRects([header_clip frame], header_reserved))
            [header_clip setFrame:header_unreserved];
        if (horizontal && NSEqualRects([horizontal frame], horizontal_reserved)) [horizontal setFrame:horizontal_unreserved];
    }
    overview_reserved = NO;
    [super tile];
    if (overview) {
        BOOL present = hex_widget && xxwidgets_hexview_overview_size(hex_widget) != 0;
        [overview setHidden:!present];
        if (present) {
            NSRect content = [clip frame];
            content_unreserved = content;
            CGFloat right = NSMaxX(content);
            NSScroller *vertical = [self verticalScroller];
            /* Overlay scrollers occupy the content's trailing edge. Reserve
             * their lane too so the map remains clickable while they show. */
            if ([self hasVerticalScroller] && [self scrollerStyle] == NSScrollerStyleOverlay && vertical &&
                NSWidth([vertical frame]) > 0 && NSHeight([vertical frame]) > 0 &&
                NSMinX([vertical frame]) > NSMinX(content))
                right = MIN(right, NSMinX([vertical frame]));
            CGFloat width = MIN(12, MAX(0, right - NSMinX(content)));
            NSRect track = NSMakeRect(right - width, NSMinY(content), width, NSHeight(content));
            content.size.width = MAX(0, NSMinX(track) - NSMinX(content));
            [clip setFrame:content];
            content_reserved = content;
            if ([self hasHorizontalScroller] && horizontal) {
                NSRect lane = [horizontal frame];
                horizontal_unreserved = lane;
                lane.size.width = MAX(0, MIN(NSMaxX(lane), NSMinX(track)) - NSMinX(lane));
                [horizontal setFrame:lane];
                horizontal_reserved = lane;
            }
            if (header_clip && [header_clip superview] == self) {
                NSRect header = [header_clip frame];
                header_unreserved = header;
                header.size.width = MAX(0, NSMinX(track) - NSMinX(header));
                [header_clip setFrame:header];
                header_reserved = header;
            }
            overview_reserved = YES;
            [overview setFrame:track];
            [overview setNeedsDisplay:YES];
        }
    }
    tiling_overview = NO;
}
- (void)refreshOverview
{
    overview->hex_widget = hex_widget;
    [self tile];
    [overview setNeedsDisplay:YES];
}
@end

static CGFloat cocoa_hex_cell_width(xxwidgets_widget *widget)
{
    NSTableView *table = widget->native;
    NSFont *font = table && [[table tableColumns] count] ? [[[[table tableColumns] objectAtIndex:0] dataCell] font] :
        [NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]];
    return MAX(1, [@"0" sizeWithAttributes:@{NSFontAttributeName:font}].width);
}

static void cocoa_size_table(xxwidgets_widget *widget);

@implementation XXWidgetsHexSymbolCell
- (void)drawInteriorWithFrame:(NSRect)cellFrame inView:(NSView *)view
{
    (void)view;
    if (!hex_widget || hex_row >= hex_widget->item_count)
        return;
    NSRect frame = [self drawingRectForBounds:cellFrame];
    CGFloat width = cocoa_hex_cell_width(hex_widget);
    const char *text = xxwidgets_hexview_cell(hex_widget, hex_row, 2);
    xxwidgets_hex_span spans[64];
    size_t count = xxwidgets_hexview_spans(hex_widget, hex_row, 2, spans);
    [NSGraphicsContext saveGraphicsState];
    NSRectClip(cellFrame);
    for (size_t i = 0; i < count; ++i) {
        xxwidgets_hex_span span = spans[i];
        size_t cell, cells;
        xxwidgets_hexview_symbol_layout(hex_widget, hex_row, &span, &cell, &cells);
        NSRect slot = NSMakeRect(frame.origin.x + cell * width,
            cellFrame.origin.y, cells * width, cellFrame.size.height);
        BOOL selected = xxwidgets_hexview_contains(hex_widget, span.offset, span.bytes);
        uint32_t tint = 0;
        if (!selected && xxwidgets_hexview_highlight_span(hex_widget, span.offset, span.bytes, &tint)) {
            [[NSColor colorWithSRGBRed:((tint >> 16) & 255) / 255.0 green:((tint >> 8) & 255) / 255.0 blue:(tint & 255) / 255.0 alpha:0.45] setFill];
            NSRectFill(slot);
        }
        if (selected) {
            [[NSColor selectedTextBackgroundColor] setFill];
            NSRectFill(slot);
        }
        if (span.length) {
            NSString *symbol = [[[NSString alloc] initWithBytes:text + span.start length:span.length
                encoding:NSUTF8StringEncoding] autorelease];
            NSDictionary *attributes = @{NSFontAttributeName : [self font],
                NSForegroundColorAttributeName : selected ? [NSColor selectedTextColor] : [NSColor textColor]};
            NSSize size = [symbol sizeWithAttributes:attributes];
            NSPoint point = NSMakePoint(slot.origin.x,
                frame.origin.y + (frame.size.height - size.height) / 2);
            if (size.width > width) {
                [NSGraphicsContext saveGraphicsState];
                NSAffineTransform *transform = [NSAffineTransform transform];
                [transform translateXBy:point.x yBy:point.y];
                [transform scaleXBy:width / size.width yBy:1];
                [transform concat];
                [symbol drawAtPoint:NSZeroPoint withAttributes:attributes];
                [NSGraphicsContext restoreGraphicsState];
            } else {
                [symbol drawAtPoint:point withAttributes:attributes];
            }
        }
        NSDictionary *continuationAttributes = @{NSFontAttributeName : [self font],
            NSForegroundColorAttributeName : [NSColor disabledControlTextColor]};
        NSSize continuationSize = [@"·" sizeWithAttributes:continuationAttributes];
        for (size_t continuation = span.length ? 1 : 0; continuation < cells; ++continuation)
            [@"·" drawAtPoint:NSMakePoint(slot.origin.x + continuation * width + (width - continuationSize.width) / 2,
                frame.origin.y + (frame.size.height - continuationSize.height) / 2) withAttributes:continuationAttributes];
    }
    [NSGraphicsContext restoreGraphicsState];
}
@end

@implementation XXWidgetsHexHeader
- (void)resetCursorRects
{
    [super resetCursorRects];
    for (NSInteger i = 0; i < (NSInteger)[[[self tableView] tableColumns] count]; ++i) {
        NSRect edge = [self headerRectOfColumn:i];
        edge.origin.x = NSMaxX(edge) - 6; edge.size.width = 12;
        [self addCursorRect:edge cursor:[NSCursor resizeLeftRightCursor]];
    }
}
- (void)mouseDown:(NSEvent *)event
{
    XXWidgetsHexTable *table = (XXWidgetsHexTable *)[self tableView];
    xxwidgets_widget *widget = table->hex_widget;
    if (!widget || !xxwidgets_focusable(widget)) return;
    NSPoint start = [self convertPoint:[event locationInWindow] fromView:nil];
    for (NSInteger i = 0; i < (NSInteger)[[table tableColumns] count]; ++i) {
        if (fabs(start.x - NSMaxX([self headerRectOfColumn:i])) > 10) continue;
        NSTableColumn *column = [[table tableColumns] objectAtIndex:i];
        CGFloat initial = [column width];
        for (;;) {
            NSEvent *next = [[self window] nextEventMatchingMask:NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp];
            if (!next) break;
            NSPoint point = [self convertPoint:[next locationInWindow] fromView:nil];
            [column setWidth:MAX([column minWidth], MIN([column maxWidth], initial + point.x - start.x))];
            widget->hex_widths[i] = MAX(4, lround([column width] / cocoa_hex_cell_width(widget)));
            cocoa_size_table(widget);
            if ([next type] == NSEventTypeLeftMouseUp) break;
        }
        [[self window] invalidateCursorRectsForView:self];
        return;
    }
    [super mouseDown:event];
}
- (NSMenu *)menuForEvent:(NSEvent *)event
{
    NSInteger column = [self columnAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]];
    return [(XXWidgetsHexTable *)[self tableView] hexMenu:column];
}
@end

@implementation XXWidgetsHexTable
- (NSMenu *)hexMenu:(NSInteger)column
{
    if (!hex_widget || !xxwidgets_focusable(hex_widget) || column < 0 || column > 3)
        return nil;
    if (column < 3)
        xxwidgets_hexview_activate_column(hex_widget, (unsigned)column);
    NSMenu *menu = [[[NSMenu alloc] initWithTitle:@""] autorelease];
    [menu setAutoenablesItems:NO];
    NSMenu *groupMenu = menu;
    const char *lastGroup = "";
    for (size_t i = 0; i < xxwidgets_hexview_menu_count((unsigned)column); ++i) {
        xxwidgets_hex_menu_item entry = xxwidgets_hexview_menu_item(hex_widget, (unsigned)column, i);
        if (strcmp(lastGroup, entry.group)) {
            NSMenuItem *group = [[[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:entry.group] action:NULL keyEquivalent:@""] autorelease];
            groupMenu = [[[NSMenu alloc] initWithTitle:[group title]] autorelease];
            [groupMenu setAutoenablesItems:NO];
            [group setSubmenu:groupMenu]; [menu addItem:group]; lastGroup = entry.group;
        }
        NSMenuItem *item = [[[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:entry.title]
            action:@selector(hexMenuAction:) keyEquivalent:@""] autorelease];
        [item setTarget:self]; [item setTag:column * 1000 + (NSInteger)i];
        [item setEnabled:!entry.disabled];
        [item setState:entry.checked ? NSControlStateValueOn : NSControlStateValueOff];
        [groupMenu addItem:item];
    }
    return menu;
}
- (void)hexMenuAction:(id)sender
{
    if (hex_widget && xxwidgets_focusable(hex_widget))
        (void)xxwidgets_hexview_menu_apply(hex_widget, (unsigned)([sender tag] / 1000), (size_t)([sender tag] % 1000));
}
- (size_t)byteAtPoint:(NSPoint)point length:(size_t *)length
{
    NSInteger row = [self rowAtPoint:point], col = [self columnAtPoint:point];
    if (row < 0 || col < 0 || !hex_widget)
        return SIZE_MAX;
    NSTableColumn *column = [[self tableColumns] objectAtIndex:col];
    NSCell *cell = [column dataCell];
    NSRect frame = [cell drawingRectForBounds:[self frameOfCellAtColumn:col row:row]];
    CGFloat x = point.x - frame.origin.x;
    if (col == 2) {
        xxwidgets_hex_span span;
        size_t slot = x > 0 ? (size_t)floor(x / cocoa_hex_cell_width(hex_widget)) : 0;
        if (!xxwidgets_hexview_hit_span(hex_widget, (size_t)row, 2, slot, &span))
            return SIZE_MAX;
        xxwidgets_hexview_activate_column(hex_widget, (unsigned)col);
        *length = span.bytes;
        return span.offset;
    }
    NSString *text = [NSString stringWithUTF8String:xxwidgets_hexview_cell(hex_widget, (size_t)row, (size_t)col)];
    NSDictionary *attributes = @{NSFontAttributeName : [cell font]};
    NSUInteger at = 0;
    while (at < [text length]) {
        NSRange range = [text rangeOfComposedCharacterSequenceAtIndex:at];
        CGFloat end = [[text substringToIndex:NSMaxRange(range)] sizeWithAttributes:attributes].width;
        if (x < end)
            break;
        at = NSMaxRange(range);
    }
    size_t index = [[text substringToIndex:at] lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
    *length = 1;
    size_t offset = xxwidgets_hexview_hit(hex_widget, (size_t)row, (size_t)col, index);
    if (offset != SIZE_MAX)
        xxwidgets_hexview_activate_column(hex_widget, (unsigned)col);
    return offset;
}
- (void)mouseDown:(NSEvent *)event
{
    if (!hex_widget || !xxwidgets_focusable(hex_widget))
        return;
    if ([event modifierFlags] & NSEventModifierFlagControl) {
        [NSMenu popUpContextMenu:[self menuForEvent:event] withEvent:event forView:self];
        return;
    }
    [[self window] makeFirstResponder:self];
    size_t length = 1;
    size_t offset = [self byteAtPoint:[self convertPoint:[event locationInWindow] fromView:nil] length:&length];
    hex_widget->hex_dragging = offset != SIZE_MAX;
    if (offset != SIZE_MAX)
        (void)xxwidgets_hexview_pointer_span(hex_widget, offset, length, ([event modifierFlags] & NSEventModifierFlagShift) != 0);
}
- (void)mouseDragged:(NSEvent *)event
{
    if (!hex_widget || !hex_widget->hex_dragging || !xxwidgets_focusable(hex_widget))
        return;
    [self autoscroll:event];
    NSPoint point = [self convertPoint:[event locationInWindow] fromView:nil];
    point.y = MAX(0, MIN(NSMaxY([self bounds]) - 1, point.y));
    point.x = MAX(0, MIN(NSMaxX([self bounds]) - 1, point.x));
    size_t length = 1;
    size_t offset = [self byteAtPoint:point length:&length];
    if (offset != SIZE_MAX)
        (void)xxwidgets_hexview_pointer_span(hex_widget, offset, length, 1);
}
- (void)mouseUp:(NSEvent *)event
{
    (void)event;
    if (hex_widget)
        hex_widget->hex_dragging = 0;
}
- (NSMenu *)menuForEvent:(NSEvent *)event
{
    if (!hex_widget || !xxwidgets_focusable(hex_widget))
        return nil;
    [[self window] makeFirstResponder:self];
    size_t length = 1;
    size_t offset = [self byteAtPoint:[self convertPoint:[event locationInWindow] fromView:nil] length:&length];
    if (offset != SIZE_MAX && !xxwidgets_hexview_contains(hex_widget, offset, length))
        (void)xxwidgets_hexview_pointer_span(hex_widget, offset, length, 0);
    return [self hexMenu:3];
}
- (void)selectAll:(id)sender
{
    (void)sender;
    if (hex_widget)
        (void)xxwidgets_hexview_body_action(hex_widget, 5);
}
- (void)copy:(id)sender
{
    (void)sender;
    if (hex_widget)
        (void)xxwidgets_hexview_body_action(hex_widget, hex_widget->hex_header == 2 ? 9 : 8);
}
- (BOOL)performKeyEquivalent:(NSEvent *)event
{
    unsigned int key, modifiers;
    int handled = 0;
    if (hex_widget && [[self window] firstResponder] == self && xxwidgets_focusable(hex_widget) && cocoa_key_input(event, &key, &modifiers)) {
        (void)xxwidgets_hexview_key_input(hex_widget, key, modifiers, &handled);
        if (handled)
            return YES;
    }
    return [super performKeyEquivalent:event];
}
- (void)keyDown:(NSEvent *)event
{
    if (hex_widget && !hex_widget->app->syncing && xxwidgets_focusable(hex_widget)) {
        unsigned int key, modifiers;
        int handled = 0;
        if (cocoa_key_input(event, &key, &modifiers)) {
            if (key == XXWIDGETS_KEY_F1 + 5) { xxwidgets_hexview_activate_column(hex_widget, (hex_widget->hex_header + 1) % 3); return; }
            if (key == XXWIDGETS_KEY_F1 + 9) {
                NSMenu *menu = [self hexMenu:(modifiers & XXWIDGETS_MOD_SHIFT) ? 3 : hex_widget->hex_header];
                [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, [[self headerView] bounds].size.height) inView:[self headerView]];
                return;
            }
            (void)xxwidgets_hexview_key_input(hex_widget, key, modifiers, &handled);
            if (handled) return;
        }
    }
    [super keyDown:event];
}
@end

@implementation XXWidgetsTree
- (void)keyDown:(NSEvent *)event
{
    if (tree_widget && !tree_widget->app->syncing &&
        xxwidgets_focusable(tree_widget) && tree_widget->value >= 0) {
        unsigned short code = [event keyCode];
        if (code == 36 || code == 76) {
            xxwidgets_emit(tree_widget, XXWIDGETS_EVENT_ACTIVATE, tree_widget->value);
            return;
        }
        if (code == 49) {
            int expanded;
            if (xxwidgets_treeview_has_children(tree_widget, (size_t)tree_widget->value) &&
                xxwidgets_treeview_get_expanded(tree_widget, (size_t)tree_widget->value, &expanded) == XXWIDGETS_OK)
                xxwidgets_treeview_user_expand(tree_widget, (size_t)tree_widget->value, !expanded);
            else xxwidgets_emit(tree_widget, XXWIDGETS_EVENT_SELECT, tree_widget->value);
            return;
        }
        /* A mark's first character presses it on the selected row (S, ?). */
        NSString *characters = [event characters];
        if (!([event modifierFlags] & (NSEventModifierFlagCommand | NSEventModifierFlagControl | NSEventModifierFlagOption)) &&
            [characters length] == 1 && xxwidgets_treeview_mark_key(tree_widget, (unsigned int)[characters characterAtIndex:0]))
            return;
    }
    [super keyDown:event];
}
@end

/* Mark columns follow the outline column; identifiers "mark0".."mark3". */
static NSInteger cocoa_tree_mark_column(NSTableColumn *column)
{
    NSString *identifier = [column identifier];
    return [identifier hasPrefix:@"mark"] ? [[identifier substringFromIndex:4] integerValue] : -1;
}

static void cocoa_tree_selection_apply(xxwidgets_widget *widget)
{
    cocoa_widget_state *state = widget->platform;
    NSOutlineView *tree = widget->native;
    NSInteger row = widget->value < 0 || (NSUInteger)widget->value >= [state->tree_items count] ? -1 :
        [tree rowForItem:[state->tree_items objectAtIndex:(NSUInteger)widget->value]];
    if ([tree selectedRow] != row) {
        ++widget->app->syncing;
        if (row < 0) [tree deselectAll:nil];
        else {
            [tree selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row] byExtendingSelection:NO];
            [tree scrollRowToVisible:row];
        }
        --widget->app->syncing;
    }
}

static void cocoa_tree_expand_visible(xxwidgets_widget *widget)
{
    cocoa_widget_state *state = widget->platform;
    NSOutlineView *tree = widget->native;
    ++widget->app->syncing;
    for (size_t node = 0; node < [state->tree_items count]; ++node) {
        int expanded;
        if (xxwidgets_treeview_row_of_node(widget, node) != SIZE_MAX &&
            xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK && expanded)
            [tree expandItem:[state->tree_items objectAtIndex:(NSUInteger)node] expandChildren:NO];
    }
    --widget->app->syncing;
}

static void cocoa_tree_expansion(xxwidgets_widget *widget, NSNotification *notification, int expanded)
{
    cocoa_widget_state *state;
    NSOutlineView *tree;
    id item;
    size_t node;
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    state = widget->platform;
    tree = widget->native;
    item = [[notification userInfo] objectForKey:@"NSObject"];
    if (!item) return;
    node = (size_t)[item unsignedLongLongValue];
    if (!expanded && state->tree_collapsing) widget->value = state->tree_before_collapse;
    state->tree_collapsing = 0;
    if (xxwidgets_treeview_expansion_input(widget, node, expanded) != XXWIDGETS_OK) {
        ++widget->app->syncing;
        if (expanded) [tree collapseItem:item];
        else [tree expandItem:item];
        --widget->app->syncing;
        if (!expanded) cocoa_tree_expand_visible(widget);
        cocoa_tree_selection_apply(widget);
        return;
    }
    if (expanded) cocoa_tree_expand_visible(widget);
    state->rendered_hex_revision = widget->tree_revision;
    cocoa_tree_selection_apply(widget);
    xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, (int)node);
}

@interface XXWidgetsArchiveTable : NSTableView {
@public
    xxwidgets_widget *archive_widget;
}
- (void)archiveContextAtPoint:(NSPoint)point selectRow:(BOOL)selectRow;
@end

@implementation XXWidgetsArchiveTable
- (void)archiveContextAtPoint:(NSPoint)point selectRow:(BOOL)selectRow
{
    if (!archive_widget || archive_widget->app->syncing ||
        !xxwidgets_focusable(archive_widget)) return;
    if (selectRow) {
        NSInteger row = [self rowAtPoint:point];
        ++archive_widget->app->syncing;
        if (row >= 0 && (size_t)row < archive_widget->item_count &&
            ![[self selectedRowIndexes] containsIndex:(NSUInteger)row])
            [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
                byExtendingSelection:NO];
        else if (row < 0) [self deselectAll:nil];
        xxwidgets_archivebrowser_selection_clear(archive_widget);
        NSIndexSet *indexes = [self selectedRowIndexes];
        for (NSUInteger index = [indexes firstIndex]; index != NSNotFound; index = [indexes indexGreaterThanIndex:index])
            xxwidgets_archivebrowser_selection_input(archive_widget, (size_t)index, 1);
        archive_widget->value = row >= 0 ? (int)row : -1;
        --archive_widget->app->syncing;
        xxwidgets_emit(archive_widget, XXWIDGETS_EVENT_SELECT, archive_widget->value);
    }
    cocoa_widget_state *state = archive_widget->platform;
    NSPoint local = [self convertPoint:point toView:state->root];
    xxwidgets_emit_context(archive_widget, archive_widget->value,
        (int)floor(local.x), (int)floor(local.y));
}

- (void)rightMouseDown:(NSEvent *)event
{
    if (!archive_widget || !xxwidgets_focusable(archive_widget)) return;
    [[self window] makeFirstResponder:self];
    [self archiveContextAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]
                     selectRow:YES];
}

- (void)mouseDown:(NSEvent *)event
{
    if ([event modifierFlags] & NSEventModifierFlagControl) [self rightMouseDown:event];
    else [super mouseDown:event];
}

- (void)keyDown:(NSEvent *)event
{
    if (archive_widget && !archive_widget->app->syncing &&
        xxwidgets_focusable(archive_widget)) {
        unsigned short code = [event keyCode];
        NSString *characters = [event charactersIgnoringModifiers];
        if (([event modifierFlags] & NSEventModifierFlagShift) &&
            [characters length] && [characters characterAtIndex:0] == NSF10FunctionKey) {
            NSRect visible = [self visibleRect];
            NSPoint point = NSMakePoint(NSMinX(visible) + 16, NSMinY(visible) + 8);
            NSInteger row = [self selectedRow];
            if (row >= 0 && (size_t)row < archive_widget->item_count) {
                [self scrollRowToVisible:row];
                point.y = NSMidY([self rectOfRow:row]);
            }
            [self archiveContextAtPoint:point selectRow:NO];
            return;
        }
        if (code == 36 || code == 76) {
            NSInteger row = [self selectedRow];
            if (row >= 0) xxwidgets_archivebrowser_user_activate(archive_widget, (size_t)row);
            return;
        }
        if (code == 51 || code == 117) {
            xxwidgets_archivebrowser_user_up(archive_widget);
            return;
        }
    }
    [super keyDown:event];
}
@end

@interface XXWidgetsProcessTable : NSTableView {
@public
    xxwidgets_widget *process_widget;
}
- (void)processContextAtPoint:(NSPoint)point selectRow:(BOOL)selectRow;
@end

@implementation XXWidgetsProcessTable
- (void)processContextAtPoint:(NSPoint)point selectRow:(BOOL)selectRow
{
    if (!process_widget || process_widget->app->syncing ||
        !xxwidgets_focusable(process_widget)) return;
    if (selectRow) {
        NSInteger row = [self rowAtPoint:point];
        ++process_widget->app->syncing;
        if (row >= 0 && (size_t)row < process_widget->item_count)
            [self selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)row]
                byExtendingSelection:NO];
        else [self deselectAll:nil];
        process_widget->value = (int)[self selectedRow];
        --process_widget->app->syncing;
        xxwidgets_emit(process_widget, XXWIDGETS_EVENT_SELECT, process_widget->value);
    }
    cocoa_widget_state *state = process_widget->platform;
    NSPoint local = [self convertPoint:point toView:state->root];
    xxwidgets_emit_context(process_widget, process_widget->value,
        (int)floor(local.x), (int)floor(local.y));
}

- (void)rightMouseDown:(NSEvent *)event
{
    if (!process_widget || !xxwidgets_focusable(process_widget)) return;
    [[self window] makeFirstResponder:self];
    [self processContextAtPoint:[self convertPoint:[event locationInWindow] fromView:nil]
                    selectRow:YES];
}

- (void)mouseDown:(NSEvent *)event
{
    if ([event modifierFlags] & NSEventModifierFlagControl) [self rightMouseDown:event];
    else [super mouseDown:event];
}

- (void)keyDown:(NSEvent *)event
{
    if (process_widget && !process_widget->app->syncing &&
        xxwidgets_focusable(process_widget)) {
        NSString *characters = [event charactersIgnoringModifiers];
        unichar character = [characters length] ? [characters characterAtIndex:0] : 0;
        if (character == NSMenuFunctionKey ||
            (character == NSF10FunctionKey && ([event modifierFlags] & NSEventModifierFlagShift))) {
            NSRect visible = [self visibleRect];
            NSPoint point = NSMakePoint(NSMinX(visible) + 16, NSMinY(visible) + 8);
            NSInteger row = [self selectedRow];
            if (row >= 0 && (size_t)row < process_widget->item_count) {
                [self scrollRowToVisible:row];
                point.y = NSMidY([self rectOfRow:row]);
            }
            [self processContextAtPoint:point selectRow:NO];
            return;
        }
        if ([event keyCode] == 36 || [event keyCode] == 76) {
            NSInteger row = [self selectedRow];
            if (row >= 0 && (size_t)row < process_widget->item_count)
                xxwidgets_emit(process_widget, XXWIDGETS_EVENT_ACTIVATE, (int)row);
            return;
        }
    }
    [super keyDown:event];
}
@end

@interface XXWidgetsBinaryTable : NSTableView {
@public
    xxwidgets_widget *binary_widget;
}
@end

@implementation XXWidgetsBinaryTable
- (void)keyDown:(NSEvent *)event
{
    if (binary_widget && !binary_widget->app->syncing &&
        xxwidgets_focusable(binary_widget) && ([event keyCode] == 36 || [event keyCode] == 76)) {
        NSInteger row = [self selectedRow];
        if (row >= 0 && (size_t)row < binary_widget->item_count)
            xxwidgets_emit(binary_widget, XXWIDGETS_EVENT_ACTIVATE, (int)row);
        return;
    }
    [super keyDown:event];
}
@end

@implementation XXWidgetsDelegate
- (void)menuActivated:(id)sender
{
    if (widget) xxwidgets_menu_activate(widget, (size_t)[sender tag]);
}
- (BOOL)validateMenuItem:(NSMenuItem *)item
{
    if ([item action] == @selector(menuActivated:))
        return widget && xxwidgets_menu_can_activate(widget, (size_t)[item tag]);
    return YES;
}
- (void)windowDidBecomeMain:(NSNotification *)notification
{
    (void)notification;
    if (!widget || widget->app->modal_window) return;
    cocoa_widget_state *state = widget->platform;
    cocoa_app_state *app = widget->app->platform;
    [NSApp setMainMenu:state->menu ? state->menu : app->base_menu];
}
- (void)editActivated:(id)sender
{
    if (widget && xxwidgets_focusable(widget) && xxwidgets_store_text(widget, [edit_text(sender) UTF8String]) == XXWIDGETS_OK)
        xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, 0);
}
- (id)windowWillReturnFieldEditor:(NSWindow *)sender toObject:(id)client
{
    (void)sender; (void)client;
    if (!widget) return nil;
    cocoa_widget_state *state = widget->platform;
    if (!state->field_editor) {
        XXWidgetsFieldEditor *editor = [[XXWidgetsFieldEditor alloc] initWithFrame:NSZeroRect];
        if (!editor) return nil;
        editor->drop_window = widget;
        [editor setFieldEditor:YES];
        [editor registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
        state->field_editor = editor;
    }
    return state->field_editor;
}
- (BOOL)windowShouldClose:(id)sender
{
    (void)sender;
    if (widget && !widget->app->syncing) xxwidgets_emit(widget, XXWIDGETS_EVENT_CLOSE, 0);
    return NO;
}

- (void)windowDidResize:(NSNotification *)notification
{
    if (!widget || widget->app->syncing) return;
    cocoa_app_state *app_state = widget->app->platform;
    cocoa_widget_state *state = widget->platform;
    NSSize size = [[(NSWindow *)[notification object] contentView] frame].size;
    int width = (int)floor(size.width / app_state->cell_width + 0.5);
    int height = (int)floor(size.height / app_state->cell_height + 0.5);
    if (width > 0 && height > 0 &&
        (width != widget->rect.width || height != widget->rect.height)) {
        widget->rect.width = width;
        widget->rect.height = height;
        state->applied_rect = widget->rect;
        xxwidgets_emit(widget, XXWIDGETS_EVENT_RESIZE, 0);
    }
}

- (void)clicked:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    if (widget->kind == XXWIDGETS_CHECKBOX) {
        widget->value = [(NSButton *)sender state] == NSControlStateValueOn ? 1 : 0;
        xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, widget->value);
    } else xxwidgets_emit(widget, XXWIDGETS_EVENT_CLICK, 0);
}

- (void)comboSelected:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    widget->value = (int)[(NSPopUpButton *)sender indexOfSelectedItem];
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

- (void)comboOpen:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget) || !widget->item_count) return;
    cocoa_widget_state *state = widget->platform;
    if ([state->combo_popover isShown]) [state->combo_popover close];
    else [state->combo_popover showRelativeToRect:[sender bounds] ofView:sender preferredEdge:NSRectEdgeMaxY];
}

- (void)comboChecked:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    size_t index = (size_t)[sender tag];
    if (index >= widget->item_count) return;
    widget->value = (int)index;
    if (([(NSButton *)sender state] == NSControlStateValueOn) != xxwidgets_checkcombobox_checked(widget, index))
        xxwidgets_checkcombobox_user_toggle(widget, index);
}

- (void)controlTextDidChange:(NSNotification *)notification
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    const char *text = [edit_text((NSTextField *)[notification object]) UTF8String];
    if (text && xxwidgets_store_text(widget, text) == XXWIDGETS_OK)
        xxwidgets_emit(widget, XXWIDGETS_EVENT_CHANGE, 0);
}

- (void)browserUp:(id)sender
{
    (void)sender;
    if (widget && !widget->app->syncing && xxwidgets_focusable(widget))
        xxwidgets_archivebrowser_user_up(widget);
}

- (void)tableActivated:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    NSInteger row = [(NSTableView *)sender clickedRow];
    if (row < 0 || (size_t)row >= widget->item_count) return;
    widget->value = (int)row;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, widget->value);
}

- (void)browserActivated:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    NSInteger row = [(NSTableView *)sender clickedRow];
    if (row >= 0) xxwidgets_archivebrowser_user_activate(widget, (size_t)row);
}

- (void)treeClicked:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    NSOutlineView *tree = sender;
    NSInteger row = [tree clickedRow], column = [tree clickedColumn];
    if (row < 0 || column < 0) return;
    NSInteger mark = cocoa_tree_mark_column([[tree tableColumns] objectAtIndex:(NSUInteger)column]);
    id item = [tree itemAtRow:row];
    /* The click already selected the row; report the button under it. */
    if (mark >= 0 && item) xxwidgets_treeview_mark_click(widget, (size_t)[item unsignedLongLongValue], (size_t)mark);
}

- (void)treeActivated:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    NSOutlineView *tree = sender;
    NSInteger row = [tree clickedRow];
    if (row < 0) return;
    id item = [tree itemAtRow:row];
    if (item) {
        size_t node = (size_t)[item unsignedLongLongValue];
        int expanded;
        if (xxwidgets_treeview_has_children(widget, node) &&
            xxwidgets_treeview_get_expanded(widget, node, &expanded) == XXWIDGETS_OK)
            xxwidgets_treeview_user_expand(widget, node, !expanded);
        else xxwidgets_emit(widget, XXWIDGETS_EVENT_ACTIVATE, (int)node);
    }
}

- (NSInteger)outlineView:(NSOutlineView *)outline numberOfChildrenOfItem:(id)item
{
    (void)outline;
    if (!widget) return 0;
    size_t parent = item ? (size_t)[item unsignedLongLongValue] : SIZE_MAX, count = 0;
    for (size_t node = 0; node < xxwidgets_treeview_count(widget); ++node) {
        xxwidgets_tree_node entry;
        xxwidgets_treeview_get_node(widget, node, &entry);
        if (entry.parent == parent) ++count;
    }
    return (NSInteger)count;
}

- (id)outlineView:(NSOutlineView *)outline child:(NSInteger)index ofItem:(id)item
{
    (void)outline;
    if (!widget || index < 0) return nil;
    cocoa_widget_state *state = widget->platform;
    size_t parent = item ? (size_t)[item unsignedLongLongValue] : SIZE_MAX;
    for (size_t node = 0; node < xxwidgets_treeview_count(widget); ++node) {
        xxwidgets_tree_node entry;
        xxwidgets_treeview_get_node(widget, node, &entry);
        if (entry.parent == parent && index-- == 0)
            return node < [state->tree_items count] ? [state->tree_items objectAtIndex:(NSUInteger)node] : nil;
    }
    return nil;
}

- (BOOL)outlineView:(NSOutlineView *)outline isItemExpandable:(id)item
{
    (void)outline;
    return widget && item && xxwidgets_treeview_has_children(widget, (size_t)[item unsignedLongLongValue]);
}

- (id)outlineView:(NSOutlineView *)outline objectValueForTableColumn:(NSTableColumn *)column byItem:(id)item
{
    (void)outline;
    if (!widget || !item) return @"";
    NSInteger mark = cocoa_tree_mark_column(column);
    if (mark >= 0) {
        const char *label = xxwidgets_treeview_node_marks(widget, (size_t)[item unsignedLongLongValue]) >> mark & 1u
            ? xxwidgets_treeview_mark_label(widget, (size_t)mark) : NULL;
        NSString *result = label ? [NSString stringWithUTF8String:label] : nil;
        return result ? result : @"";
    }
    const char *value = xxwidgets_treeview_display_text(widget, (size_t)[item unsignedLongLongValue]);
    NSString *result = [NSString stringWithUTF8String:value ? value : ""];
    return result ? result : @"";
}

- (void)outlineView:(NSOutlineView *)outline willDisplayCell:(id)cell forTableColumn:(NSTableColumn *)column item:(id)item
{
    if (cocoa_tree_mark_column(column) >= 0) {
        NSInteger row = [outline rowForItem:item];
        BOOL selected = row >= 0 && [[outline selectedRowIndexes] containsIndex:(NSUInteger)row];
        [cell setTextColor:selected ? [NSColor selectedControlTextColor] : [NSColor linkColor]];
        [cell setDrawsBackground:NO];
        return;
    }
    xxwidgets_tree_color colors = xxwidgets_treeview_node_color(widget, (size_t)[item unsignedLongLongValue]);
    BOOL dark = [[[outline effectiveAppearance] bestMatchFromAppearancesWithNames:
        @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]] isEqualToString:NSAppearanceNameDarkAqua];
    uint32_t color = dark ? colors.dark_foreground : colors.foreground;
    uint32_t background = dark ? colors.dark_background : colors.background;
    NSInteger row = [outline rowForItem:item];
    NSColor *foreground = [NSColor labelColor];
    BOOL selected = row >= 0 && [[outline selectedRowIndexes] containsIndex:(NSUInteger)row];
    if (selected)
        foreground = [NSColor selectedControlTextColor];
    else if (color != UINT32_MAX)
        foreground = [NSColor colorWithSRGBRed:((color >> 16) & 255) / 255.0
            green:((color >> 8) & 255) / 255.0 blue:(color & 255) / 255.0 alpha:1];
    [cell setTextColor:foreground];
    [cell setDrawsBackground:!selected && background != UINT32_MAX];
    if (!selected && background != UINT32_MAX)
        [cell setBackgroundColor:[NSColor colorWithSRGBRed:((background >> 16) & 255) / 255.0
            green:((background >> 8) & 255) / 255.0 blue:(background & 255) / 255.0 alpha:1]];
}

- (BOOL)outlineView:(NSOutlineView *)outline shouldSelectItem:(id)item
{
    (void)outline; (void)item;
    return widget && (widget->app->syncing || xxwidgets_focusable(widget));
}

- (BOOL)outlineView:(NSOutlineView *)outline shouldExpandItem:(id)item
{
    (void)outline; (void)item;
    return widget && (widget->app->syncing || xxwidgets_focusable(widget));
}

- (BOOL)outlineView:(NSOutlineView *)outline shouldCollapseItem:(id)item
{
    (void)outline; (void)item;
    if (!widget || (!widget->app->syncing && !xxwidgets_focusable(widget))) return NO;
    if (!widget->app->syncing) {
        cocoa_widget_state *state = widget->platform;
        state->tree_collapsing = 1;
        state->tree_before_collapse = widget->value;
    }
    return YES;
}

- (void)outlineViewSelectionDidChange:(NSNotification *)notification
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    if (((cocoa_widget_state *)widget->platform)->tree_collapsing) return;
    NSOutlineView *tree = [notification object];
    NSInteger row = [tree selectedRow];
    id item = row < 0 ? nil : [tree itemAtRow:row];
    widget->value = item ? (int)[item unsignedLongLongValue] : -1;
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}

- (void)outlineViewItemDidExpand:(NSNotification *)notification
{
    cocoa_tree_expansion(widget, notification, 1);
}

- (void)outlineViewItemDidCollapse:(NSNotification *)notification
{
    cocoa_tree_expansion(widget, notification, 0);
}

- (void)tableView:(NSTableView *)table didClickTableColumn:(NSTableColumn *)column
{
    if (widget && widget->kind == XXWIDGETS_HEXVIEW && !widget->app->syncing) {
        NSMenu *menu = [(XXWidgetsHexTable *)table hexMenu:[[column identifier] integerValue]];
        [NSMenu popUpContextMenu:menu withEvent:[NSApp currentEvent] forView:[table headerView]];
    }
    (void)table;
    if (widget && widget->kind == XXWIDGETS_ARCHIVEBROWSER &&
        !widget->app->syncing && xxwidgets_focusable(widget))
        xxwidgets_archivebrowser_user_sort(widget,
            (xxwidgets_archive_column)[[column identifier] intValue]);
}

- (NSInteger)numberOfRowsInTableView:(NSTableView *)table
{
    (void)table;
    return widget ? (NSInteger)widget->item_count : 0;
}

- (id)tableView:(NSTableView *)table objectValueForTableColumn:(NSTableColumn *)column
             row:(NSInteger)row
{
    (void)table;
    if (!widget || row < 0 || (size_t)row >= widget->item_count) return @"";
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
        const char *value = xxwidgets_archivebrowser_cell(widget, (size_t)row,
            (xxwidgets_archive_column)[[column identifier] intValue]);
        NSString *result = [NSString stringWithUTF8String:value];
        return result ? result : @"";
    }
    if (xxwidgets_tabular_kind(widget)) {
        const char *value = xxwidgets_tabular_cell(widget, (size_t)row,
            (size_t)[[column identifier] intValue]);
        NSString *result = [NSString stringWithUTF8String:value];
        return result ? result : @"";
    }
    NSString *text = [NSString stringWithUTF8String:widget->items[row]];
    return text ? text : @"";
}

- (BOOL)tableView:(NSTableView *)table shouldSelectRow:(NSInteger)row
{
    (void)table; (void)row;
    return widget && (widget->app->syncing || xxwidgets_focusable(widget));
}

- (void)tableView:(NSTableView *)table willDisplayCell:(id)cell
   forTableColumn:(NSTableColumn *)column row:(NSInteger)row
{
    if (widget && widget->kind == XXWIDGETS_HEXVIEW && row >= 0) {
        xxwidgets_hex_span spans[64];
        size_t col = (size_t)[[column identifier] intValue];
        if (col == 2) {
            XXWidgetsHexSymbolCell *symbolCell = (XXWidgetsHexSymbolCell *)cell;
            symbolCell->hex_widget = widget;
            symbolCell->hex_row = (size_t)row;
        }
        const char *text = xxwidgets_hexview_cell(widget, (size_t)row, col);
        NSMutableAttributedString *value = [[[NSMutableAttributedString alloc]
            initWithString:[NSString stringWithUTF8String:text]
                attributes:@{NSFontAttributeName : [cell font], NSForegroundColorAttributeName : [NSColor textColor]}] autorelease];
        int bold_nonzero = 0;
        if (col == 1 && xxwidgets_hexview_get_bold_nonzero(widget, &bold_nonzero) == XXWIDGETS_OK && bold_nonzero) {
            NSFontManager *manager = [NSFontManager sharedFontManager];
            NSFont *regular = [manager convertFont:[cell font] toNotHaveTrait:NSBoldFontMask];
            if (!regular) regular = [cell font];
            NSFont *bold = [manager convertFont:regular toHaveTrait:NSBoldFontMask];
            if (!bold) bold = regular;
            [value addAttribute:NSFontAttributeName value:regular range:NSMakeRange(0, [value length])];
            size_t bold_count = xxwidgets_hexview_bold_spans(widget, (size_t)row, col, spans);
            for (size_t i = 0; i < bold_count; ++i) {
                /* The values column is ASCII, so byte and UTF-16 indices agree.
                 * Keep the cell font unchanged for column sizing and hit tests. */
                [value addAttribute:NSFontAttributeName value:bold range:NSMakeRange(spans[i].start, spans[i].length)];
            }
        }
        size_t all_count = xxwidgets_hexview_spans(widget, (size_t)row, col, spans);
        for (size_t i = 0; i < all_count; ++i) {
            uint32_t rgb;
            if (!xxwidgets_hexview_highlight_span(widget, spans[i].offset, spans[i].bytes, &rgb)) continue;
            NSString *prefix = [[[NSString alloc] initWithBytes:text length:spans[i].start encoding:NSUTF8StringEncoding] autorelease];
            NSString *part = [[[NSString alloc] initWithBytes:text + spans[i].start length:spans[i].length encoding:NSUTF8StringEncoding] autorelease];
            [value addAttribute:NSBackgroundColorAttributeName value:[NSColor colorWithSRGBRed:((rgb >> 16) & 255) / 255.0 green:((rgb >> 8) & 255) / 255.0 blue:(rgb & 255) / 255.0 alpha:0.45] range:NSMakeRange([prefix length], [part length])];
        }
        size_t count = xxwidgets_hexview_selection_spans(widget, (size_t)row, col, spans);
        for (size_t i = 0; i < count; ++i) {
            NSString *prefix = [[[NSString alloc] initWithBytes:text length:spans[i].start encoding:NSUTF8StringEncoding] autorelease];
            NSString *part = [[[NSString alloc] initWithBytes:text + spans[i].start length:spans[i].length
                                                     encoding:NSUTF8StringEncoding] autorelease];
            [value addAttributes:@{
                NSBackgroundColorAttributeName : [NSColor selectedTextBackgroundColor],
                NSForegroundColorAttributeName : [NSColor selectedTextColor]
            }
                           range:NSMakeRange([prefix length], [part length])];
        }
        [cell setAttributedStringValue:value];
        return;
    }
    (void)table;
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEBROWSER ||
        [[column identifier] intValue] != 0 || row < 0) return;
    xxwidgets_archive_browser_entry entry;
    size_t source;
    if (xxwidgets_archivebrowser_get_entry(widget, (size_t)row, &source, &entry) != XXWIDGETS_OK) return;
    [(NSBrowserCell *)cell setLeaf:!entry.is_directory];
    /* Named AppKit images are shared and commonly 32 pt; the list uses
     * compact rows. Resize a copy so icons cannot paint over adjacent rows. */
    NSImage *icon = [[NSImage imageNamed:entry.is_directory ? NSImageNameFolder : NSImageNameMultipleDocuments] copy];
    CGFloat side = MIN(16, MAX(1, [table rowHeight] - 4));
    [icon setSize:NSMakeSize(side, side)];
    [(NSBrowserCell *)cell setImage:icon];
    [icon release];
}

- (void)tableViewColumnDidResize:(NSNotification *)notification
{
    if (!widget || widget->kind != XXWIDGETS_HEXVIEW || widget->app->syncing) return;
    NSTableColumn *column = [[notification userInfo] objectForKey:@"NSTableColumn"];
    NSInteger index = [[column identifier] integerValue];
    if (index >= 0 && index < 3) widget->hex_widths[index] = MAX(4, lround([column width] / cocoa_hex_cell_width(widget)));
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    int value = (int)[(NSTableView *)[notification object] selectedRow];
    if (widget->kind == XXWIDGETS_HEXVIEW) {
        if (xxwidgets_hexview_selection_input(widget, value) != XXWIDGETS_OK) return;
    } else widget->value = value;
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
        NSIndexSet *indexes = [(NSTableView *)[notification object] selectedRowIndexes];
        xxwidgets_archivebrowser_selection_clear(widget);
        for (NSUInteger index = [indexes firstIndex]; index != NSNotFound; index = [indexes indexGreaterThanIndex:index])
            xxwidgets_archivebrowser_selection_input(widget, (size_t)index, 1);
    }
    xxwidgets_emit(widget, XXWIDGETS_EVENT_SELECT, widget->value);
}
@end

@interface XXWidgetsApplicationDelegate : NSObject <NSApplicationDelegate> {
@public
    xxwidgets_app *app;
}
@end

@implementation XXWidgetsApplicationDelegate
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    (void)sender;
    if (app && !app->syncing) {
        xxwidgets_widget *window = app->widgets;
        while (window && (window->kind != XXWIDGETS_WINDOW || !window->visible))
            window = window->next;
        if (window) xxwidgets_emit(window, XXWIDGETS_EVENT_CLOSE, 0);
        else xxwidgets_app_quit(app, 0);
    }
    /* Return control to xxwidgets_app_run instead of terminating the process. */
    return NSTerminateCancel;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
    (void)sender;
    return NO;
}
@end

static void install_menu(void)
{
    NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
    NSMenuItem *app_item = [[NSMenuItem alloc] initWithTitle:@"" action:NULL keyEquivalent:@""];
    NSMenu *app_menu = [[NSMenu alloc] initWithTitle:@"Application"];
    NSString *name = [[NSProcessInfo processInfo] processName];
    NSMenuItem *quit = [[NSMenuItem alloc] initWithTitle:[@"Quit " stringByAppendingString:name]
        action:@selector(terminate:) keyEquivalent:@"q"];
    [quit setTarget:NSApp];
    [app_menu addItem:quit];
    [app_item setSubmenu:app_menu];
    [menu addItem:app_item];
    [quit release]; [app_menu release]; [app_item release];

    NSMenuItem *edit_item = [[NSMenuItem alloc] initWithTitle:@"Edit" action:NULL keyEquivalent:@""];
    NSMenu *edit_menu = [[NSMenu alloc] initWithTitle:@"Edit"];
    [edit_menu addItemWithTitle:@"Undo" action:@selector(undo:) keyEquivalent:@"z"];
    [edit_menu addItemWithTitle:@"Redo" action:@selector(redo:) keyEquivalent:@"Z"];
    [edit_menu addItem:[NSMenuItem separatorItem]];
    [edit_menu addItemWithTitle:@"Cut" action:@selector(cut:) keyEquivalent:@"x"];
    [edit_menu addItemWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"];
    [edit_menu addItemWithTitle:@"Paste" action:@selector(paste:) keyEquivalent:@"v"];
    [edit_menu addItemWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"];
    [edit_item setSubmenu:edit_menu];
    [menu addItem:edit_item];
    [NSApp setMainMenu:menu];
    [edit_menu release]; [edit_item release]; [menu release];
}

static xxwidgets_status cocoa_init_backend(xxwidgets_app *app)
{
    if (![NSThread isMainThread]) return XXWIDGETS_UNAVAILABLE;
    if (active_app) return XXWIDGETS_BUSY;
    @autoreleasepool {
        [NSApplication sharedApplication];
        if (!NSApp || ![NSScreen mainScreen]) return XXWIDGETS_UNAVAILABLE;
        cocoa_app_state *state = calloc(1, sizeof(*state));
        if (!state) return XXWIDGETS_OUT_OF_MEMORY;
        app->platform = state;
        state->cell_width = 8;
        state->cell_height = 20;
        NSFont *font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
        if (font) {
            CGFloat width = ceil([@"M" sizeWithAttributes:
                [NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName]].width);
            CGFloat height = ceil([font ascender] - [font descender] + [font leading]) + 4;
            if (width > 0) state->cell_width = width;
            if (height > 0) state->cell_height = height;
        }
        state->saved_delegate = [[NSApp delegate] retain];
        state->saved_menu = [[NSApp mainMenu] retain];
        state->saved_policy = [NSApp activationPolicy];
        state->delegate = [[XXWidgetsApplicationDelegate alloc] init];
        if (!state->delegate) return XXWIDGETS_OUT_OF_MEMORY;
        state->delegate->app = app;
        active_app = app;
        [NSApp setDelegate:state->delegate];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        install_menu();
        state->base_menu = [[NSApp mainMenu] retain];
        if (!finished_launching && ![NSApp isRunning]) {
            [NSApp finishLaunching];
            finished_launching = 1;
        }
        return XXWIDGETS_OK;
    }
}

static void cocoa_shutdown_backend(xxwidgets_app *app)
{
    cocoa_app_state *state = app->platform;
    if (!state) return;
    @autoreleasepool {
        if (active_app == app) {
            [NSApp setDelegate:state->saved_delegate];
            [NSApp setMainMenu:state->saved_menu];
            [NSApp setActivationPolicy:state->saved_policy];
            active_app = NULL;
        }
        if (state->delegate) state->delegate->app = NULL;
        [state->delegate release];
        [state->saved_delegate release];
        [state->saved_menu release];
        [state->base_menu release];
        for (size_t role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) [state->fonts[role] release];
        [state->formatted_font release];
        free(state);
        app->platform = NULL;
    }
}

static int cocoa_shortcut(xxwidgets_app *app, NSEvent *event)
{
    xxwidgets_widget *window;
    unsigned int key, modifiers;
    for (window = app->widgets; window; window = window->next)
        if (window->kind == XXWIDGETS_WINDOW && window->native == [event window]) break;
    if (!window) return 0;
    if (!cocoa_key_input(event, &key, &modifiers)) return 0;
    /* Keep the editor's document commands bound to its shared history and
     * selection, including when the host window also binds those letters. */
    if ((modifiers & (XXWIDGETS_MOD_CTRL | XXWIDGETS_MOD_META)) &&
        !(modifiers & XXWIDGETS_MOD_ALT) &&
        (key == 'a' || key == 'A' || key == 'c' || key == 'C' || key == 'x' || key == 'X' ||
         key == 'v' || key == 'V' || key == 'z' || key == 'Z' || key == 'y' || key == 'Y')) {
        for (xxwidgets_widget *edit = app->widgets; edit; edit = edit->next)
            if (edit->parent == window && edit->kind == XXWIDGETS_DIE_SIGNATURE_EDIT &&
                xxwidgets_widget_has_focus(edit) == 1 && xxwidgets_focusable(edit)) {
                [(XXWidgetsDieSignatureView *)edit->native editKey:(int)key modifiers:modifiers];
                return 1;
            }
    }
    /* Text controls retain ordinary paste even when an application binds V
     * elsewhere. Control+V follows the portable widget shortcut convention;
     * Command+V remains the native equivalent. */
    if ((key == 'v' || key == 'V') && (modifiers == XXWIDGETS_MOD_CTRL || modifiers == XXWIDGETS_MOD_META)) {
        for (xxwidgets_widget *edit = app->widgets; edit; edit = edit->next) if (edit->parent == window && edit->kind == XXWIDGETS_EDIT && xxwidgets_widget_has_focus(edit) == 1) {
            id editor = [(NSTextField *)edit->native currentEditor];
            if ([editor respondsToSelector:@selector(paste:)]) { [editor paste:nil]; return 1; }
        }
    }
    return xxwidgets_shortcut_dispatch(window, key, modifiers);
}

static int cocoa_modal_button_eligible(const xxwidgets_widget *button, const xxwidgets_widget *dialog)
{
    return button && button->parent == dialog && button->kind == XXWIDGETS_BUTTON &&
        button->visible && button->enabled && button->native &&
        [(NSButton *)button->native isEnabled] && ![(NSButton *)button->native isHiddenOrHasHiddenAncestor];
}

static xxwidgets_status cocoa_poll_backend(xxwidgets_app *app, int timeout_ms)
{
    @autoreleasepool {
        NSDate *deadline = [NSDate dateWithTimeIntervalSinceNow:timeout_ms / 1000.0];
        unsigned processed = 0;
        while (!app->quit && processed < 256) {
            NSDate *until = processed ? [NSDate distantPast] : deadline;
            NSEvent *event = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:until
                inMode:NSDefaultRunLoopMode dequeue:YES];
            if (!event) break;
            if (app->modal_window && [event type] == NSEventTypeKeyDown &&
                [event window] == app->modal_window->native && [event keyCode] == 53)
                xxwidgets_emit(app->modal_window, XXWIDGETS_EVENT_CLOSE, 0);
            else if (app->modal_window && app->modal_window->visible && app->modal_window->enabled &&
                [event type] == NSEventTypeKeyDown &&
                [event window] == app->modal_window->native &&
                ![[[event window] firstResponder] isKindOfClass:[XXWidgetsDieSignatureView class]] &&
                ([event keyCode] == 36 || [event keyCode] == 76)) {
                xxwidgets_widget *button = app->modal_default, *candidate;
                /* Enter activates a focused dialog button before the default
                 * action. Fields and other controls retain the default. */
                for (candidate = app->widgets; candidate; candidate = candidate->next)
                    if (cocoa_modal_button_eligible(candidate, app->modal_window) &&
                        xxwidgets_widget_has_focus(candidate) == 1) {
                        button = candidate;
                        break;
                    }
                if (cocoa_modal_button_eligible(button, app->modal_window))
                    xxwidgets_emit(button, XXWIDGETS_EVENT_CLICK, 0);
                else if (!cocoa_shortcut(app, event)) [NSApp sendEvent:event];
            }
            else if (!cocoa_shortcut(app, event)) [NSApp sendEvent:event];
            ++processed;
            if (timeout_ms > 0 && [deadline timeIntervalSinceNow] <= 0) break;
        }
        [NSApp updateWindows];
        return XXWIDGETS_OK;
    }
}

static void cocoa_enable_control(xxwidgets_widget *widget)
{
    cocoa_widget_state *state = widget->platform;
    id native = widget->native;
    if (!state || !native) return;
    BOOL enabled = widget->enabled && (!widget->parent || widget->parent->enabled);
    if ([native respondsToSelector:@selector(setEnabled:)]) [native setEnabled:enabled];
    if (widget->kind == XXWIDGETS_CHECKCOMBOBOX && !enabled) [state->combo_popover close];
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER)
        [state->browser_up setEnabled:enabled && xxwidgets_archivebrowser_directory(widget)[0] != '\0'];
    if (xxwidgets_list_kind(widget) || widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT ||
        widget->kind == XXWIDGETS_PROGRESS)
        [state->root setAlphaValue:enabled ? 1.0 : 0.5];
    if (widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT) [(XXWidgetsDieSignatureView *)native refresh];
}

static void cocoa_size_table(xxwidgets_widget *widget)
{
    cocoa_widget_state *state = widget->platform;
    NSTableView *table = widget->native;
    NSScrollView *scroll = widget->kind == XXWIDGETS_ARCHIVEBROWSER ?
        state->browser_scroll : (NSScrollView *)state->root;
    NSSize content = [scroll contentSize];
    NSRect frame = [table frame];
    CGFloat rows_height = widget->item_count *
        ([table rowHeight] + [table intercellSpacing].height);
    frame.size.height = MAX(content.height, rows_height);
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER || xxwidgets_tabular_kind(widget)) {
        CGFloat column_width = 0;
        for (NSTableColumn *column in [table tableColumns]) column_width += [column width];
        frame.size.width = MAX(content.width, column_width);
        [table setFrame:frame];
        return;
    }
    /* Tree mark columns stay at the right: the text column takes the rest. */
    CGFloat marks = 0;
    if (widget->kind == XXWIDGETS_TREEVIEW)
        for (NSTableColumn *column in [table tableColumns])
            if (cocoa_tree_mark_column(column) >= 0 && ![column isHidden]) marks += [column width] + [table intercellSpacing].width;
    frame.size.width = xxwidgets_formatted_rows(widget) ?
        MAX(content.width, state->hex_row_width + marks) : content.width;
    [table setFrame:frame];
    if (xxwidgets_formatted_rows(widget))
        [[[table tableColumns] objectAtIndex:0] setWidth:MAX(1, frame.size.width - marks)];
    else [table sizeLastColumnToFit];
}

static xxwidgets_status cocoa_tree_sync(xxwidgets_widget *widget)
{
    cocoa_widget_state *state = widget->platform;
    NSOutlineView *tree = widget->native;
    size_t count = xxwidgets_treeview_count(widget);
    BOOL refreshed = state->tree_content_revision != widget->tree_content_revision;
    if (refreshed) {
        NSMutableArray *items = [[NSMutableArray alloc] initWithCapacity:count];
        if (!items) return XXWIDGETS_OUT_OF_MEMORY;
        for (size_t node = 0; node < count; ++node) {
            NSNumber *item = [[NSNumber alloc] initWithUnsignedLongLong:(unsigned long long)node];
            if (!item) { [items release]; return XXWIDGETS_OUT_OF_MEMORY; }
            [items addObject:item]; [item release];
        }
        NSArray *previous = state->tree_items;
        state->tree_items = items;
        [tree reloadData];
        [previous release];
        state->tree_content_revision = widget->tree_content_revision;
    }
    if (refreshed || state->rendered_hex_revision != widget->tree_revision) {
        [tree collapseItem:nil collapseChildren:YES];
        cocoa_tree_expand_visible(widget);
        state->rendered_hex_revision = widget->tree_revision;
        NSFont *font = [[[[tree tableColumns] objectAtIndex:0] dataCell] font];
        NSDictionary *attributes = [NSDictionary dictionaryWithObject:font forKey:NSFontAttributeName];
        state->hex_row_width = 0;
        for (size_t row = 0; row < widget->item_count; ++row) {
            size_t node = xxwidgets_treeview_node_at_row(widget, row);
            NSString *text = [NSString stringWithUTF8String:xxwidgets_treeview_display_text(widget, node)];
            CGFloat width = ceil([text sizeWithAttributes:attributes].width) +
                (xxwidgets_treeview_depth(widget, node) + 1) * [tree indentationPerLevel] + 24;
            if (width > state->hex_row_width) state->hex_row_width = width;
        }
    }
    for (NSTableColumn *column in [tree tableColumns]) {
        NSInteger mark = cocoa_tree_mark_column(column);
        if (mark >= 0) [column setHidden:(size_t)mark >= xxwidgets_treeview_mark_count(widget)];
    }
    [tree setNeedsDisplay:YES];
    cocoa_tree_selection_apply(widget);
    cocoa_size_table(widget);
    return XXWIDGETS_OK;
}

static xxwidgets_status cocoa_sync_backend(xxwidgets_widget *widget);
static void cocoa_destroy_backend(xxwidgets_widget *widget);

static xxwidgets_status cocoa_create_backend(xxwidgets_widget *widget)
{
    @autoreleasepool {
        cocoa_widget_state *state = calloc(1, sizeof(*state));
        if (!state) return XXWIDGETS_OUT_OF_MEMORY;
        widget->platform = state;
        state->delegate = [[XXWidgetsDelegate alloc] init];
        if (!state->delegate) { cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY; }
        state->delegate->widget = widget;
        NSFont *font = [NSFont systemFontOfSize:[NSFont systemFontSize]];
        id native = nil;
        switch (widget->kind) {
        case XXWIDGETS_WINDOW: {
            NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 320, 200)
                styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                          NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                backing:NSBackingStoreBuffered defer:NO];
            [window setReleasedWhenClosed:NO];
            [window setDelegate:state->delegate];
            XXWidgetsContentView *content = [[XXWidgetsContentView alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)];
            content->drop_window = widget;
            [window setContentView:content];
            [content release];
            state->root = [window contentView];
            native = window;
            break;
        }
        case XXWIDGETS_LABEL:
        case XXWIDGETS_EDIT: {
            NSTextField *field = [[NSTextField alloc] initWithFrame:NSZeroRect];
            [field setFont:font];
            [field setEditable:widget->kind == XXWIDGETS_EDIT];
            [field setSelectable:widget->kind == XXWIDGETS_EDIT];
            [field setBezeled:widget->kind == XXWIDGETS_EDIT];
            [field setDrawsBackground:widget->kind == XXWIDGETS_EDIT];
            [[field cell] setWraps:NO];
            [[field cell] setScrollable:widget->kind == XXWIDGETS_EDIT];
            if (widget->kind == XXWIDGETS_EDIT) { [field setDelegate:state->delegate]; [field setTarget:state->delegate]; [field setAction:@selector(editActivated:)]; }
            else [[field cell] setLineBreakMode:NSLineBreakByTruncatingTail];
            native = field;
            break;
        }
        case XXWIDGETS_DIE_SIGNATURE_EDIT: {
            XXWidgetsDieSignatureView *view = [[XXWidgetsDieSignatureView alloc] initWithFrame:NSZeroRect];
            if (view) {
                view->signature_widget = widget;
                [view setAccessibilityLabel:@"DIE signature source editor"];
                [view setAccessibilityHelp:@"JavaScript signature editor. Tab indents; Control-Tab moves focus."];
            }
            native = view;
            break;
        }
        case XXWIDGETS_BUTTON:
        case XXWIDGETS_CHECKBOX: {
            NSButton *button = [[NSButton alloc] initWithFrame:NSZeroRect];
            [button setFont:font];
            [button setButtonType:widget->kind == XXWIDGETS_CHECKBOX ?
                NSButtonTypeSwitch : NSButtonTypeMomentaryPushIn];
            if (widget->kind == XXWIDGETS_BUTTON) [button setBezelStyle:NSBezelStyleRounded];
            [button setTarget:state->delegate];
            [button setAction:@selector(clicked:)];
            native = button;
            break;
        }
        case XXWIDGETS_COMBOBOX: {
            NSPopUpButton *button = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
            [button setFont:font]; [button setTarget:state->delegate]; [button setAction:@selector(comboSelected:)];
            native = button; break;
        }
        case XXWIDGETS_CHECKCOMBOBOX: {
            NSButton *button = [[NSButton alloc] initWithFrame:NSZeroRect];
            [button setFont:font]; [button setBezelStyle:NSBezelStyleRounded];
            [button setTarget:state->delegate]; [button setAction:@selector(comboOpen:)];
            NSViewController *controller = [[NSViewController alloc] init];
            NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 240, 160)];
            state->combo_checks = [[XXWidgetsContentView alloc] initWithFrame:NSMakeRect(0, 0, 240, 160)];
            state->combo_popover = [[NSPopover alloc] init];
            if (!button || !controller || !scroll || !state->combo_checks || !state->combo_popover) {
                [button release]; [controller release]; [scroll release]; [state->combo_checks release];
                state->combo_checks = nil; cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY;
            }
            [scroll setHasVerticalScroller:YES]; [scroll setAutohidesScrollers:YES];
            [scroll setDocumentView:state->combo_checks]; [state->combo_checks release];
            [controller setView:scroll]; [scroll release];
            [state->combo_popover setContentViewController:controller]; [controller release];
            [state->combo_popover setBehavior:NSPopoverBehaviorTransient];
            native = button; break;
        }
        case XXWIDGETS_LISTBOX:
        case XXWIDGETS_ARCHIVEVIEW:
        case XXWIDGETS_HEXVIEW: {
            NSTableView *table = widget->kind == XXWIDGETS_HEXVIEW ?
                [[XXWidgetsHexTable alloc] initWithFrame:NSZeroRect] :
                [[NSTableView alloc] initWithFrame:NSZeroRect];
            if (widget->kind == XXWIDGETS_HEXVIEW)
                ((XXWidgetsHexTable *)table)->hex_widget = widget;
            if (widget->kind == XXWIDGETS_HEXVIEW) {
                [table setSelectionHighlightStyle:NSTableViewSelectionHighlightStyleNone];
                XXWidgetsHexHeader *header = [[XXWidgetsHexHeader alloc] initWithFrame:NSMakeRect(0, 0, 100, 23)];
                [table setHeaderView:header]; [header release];
                for (size_t i = 0; i < 3; ++i) {
                    NSTableColumn *c = [[NSTableColumn alloc] initWithIdentifier:[NSString stringWithFormat:@"%zu", i]];
                    if (i == 2) {
                        XXWidgetsHexSymbolCell *cell = [[XXWidgetsHexSymbolCell alloc] initTextCell:@""];
                        cell->hex_widget = widget;
                        [c setDataCell:cell];
                        [cell release];
                    }
                    [[c headerCell] setStringValue:[NSString stringWithUTF8String:xxwidgets_hexview_title(widget, i)]];
                    [[c dataCell] setFont:[NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]]];
                    [[c dataCell] setEditable:NO]; [[c dataCell] setLineBreakMode:NSLineBreakByClipping];
                    [c setMinWidth:32]; [c setMaxWidth:CGFLOAT_MAX]; [c setResizingMask:NSTableColumnUserResizingMask];
                    [c setWidth:xxwidgets_hexview_column_width(widget, i) * cocoa_hex_cell_width(widget)];
                    [table addTableColumn:c]; [c release];
                }
            } else {
            NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"item"];
            NSFont *row_font = xxwidgets_formatted_rows(widget) ?
                [NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]] : font;
            [[column dataCell] setFont:row_font ? row_font : font];
            [[column dataCell] setEditable:NO];
            [[column dataCell] setLineBreakMode:xxwidgets_formatted_rows(widget) ?
                NSLineBreakByClipping : NSLineBreakByTruncatingTail];
            [column setResizingMask:xxwidgets_formatted_rows(widget) ?
                NSTableColumnNoResizing : NSTableColumnAutoresizingMask];
            if (xxwidgets_formatted_rows(widget)) {
                [column setMinWidth:1];
                [column setMaxWidth:CGFLOAT_MAX];
            }
            [table addTableColumn:column];
            [column release];
            [table setHeaderView:nil];
            }
            [table setAllowsMultipleSelection:NO];
            [table setAllowsEmptySelection:YES];
            [table setAllowsColumnReordering:NO];
            [table setAllowsColumnResizing:YES];
            [table setColumnAutoresizingStyle:xxwidgets_formatted_rows(widget) ?
                NSTableViewNoColumnAutoresizing : NSTableViewLastColumnOnlyAutoresizingStyle];
            if (!xxwidgets_formatted_rows(widget)) [table setAutoresizingMask:NSViewWidthSizable];
            [table setRowHeight:((cocoa_app_state *)widget->app->platform)->cell_height];
            [table setDataSource:state->delegate];
            [table setDelegate:state->delegate];
            NSScrollView *scroll = widget->kind == XXWIDGETS_HEXVIEW ?
                [[XXWidgetsHexScroll alloc] initWithFrame:NSZeroRect] :
                [[NSScrollView alloc] initWithFrame:NSZeroRect];
            if (widget->kind == XXWIDGETS_HEXVIEW)
                ((XXWidgetsHexScroll *)scroll)->hex_widget = widget;
            [scroll setBorderType:NSBezelBorder];
            [scroll setHasVerticalScroller:YES];
            [scroll setHasHorizontalScroller:xxwidgets_formatted_rows(widget)];
            [scroll setAutohidesScrollers:YES];
            [scroll setDocumentView:table];
            state->root = scroll;
            native = table;
            break;
        }
        case XXWIDGETS_CANVAS:
        case XXWIDGETS_SPLITTER: { XXWidgetsCanvas *view=[[XXWidgetsCanvas alloc]initWithFrame:NSZeroRect];view->widget=widget;native=view;[view setAccessibilityLabel:widget->kind==XXWIDGETS_CANVAS?@"Binary canvas. Arrow keys select pixels.":@"Resizable panel splitter"];break;}
        case XXWIDGETS_ENTROPY_PLOT: {
            XXWidgetsEntropyPlot *plot = [[XXWidgetsEntropyPlot alloc] initWithFrame:NSZeroRect];
            plot->widget = widget; native = plot;
            [plot setAccessibilityLabel:@"Entropy diagram. Click for sample details."];
            break;
        }
        case XXWIDGETS_PROGRESS: {
            NSProgressIndicator *progress = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
            [progress setStyle:NSProgressIndicatorStyleBar];
            [progress setIndeterminate:NO];
            [progress setMinValue:0];
            [progress setMaxValue:100];
            native = progress;
            break;
        }
        case XXWIDGETS_TREEVIEW: {
            XXWidgetsTree *tree = [[XXWidgetsTree alloc] initWithFrame:NSZeroRect];
            NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"item"];
            if (!tree || !column) {
                [tree release]; [column release]; cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY;
            }
            tree->tree_widget = widget;
            [[column dataCell] setFont:font];
            [[column dataCell] setEditable:NO];
            [[column dataCell] setWraps:NO];
            [[column dataCell] setLineBreakMode:NSLineBreakByClipping];
            [column setMinWidth:1]; [column setMaxWidth:CGFLOAT_MAX];
            [column setResizingMask:NSTableColumnNoResizing];
            [tree addTableColumn:column]; [tree setOutlineTableColumn:column]; [column release];
            for (int mark = 0; mark < XXWIDGETS_TREE_MARKS_MAX; ++mark) {
                NSTableColumn *marks = [[NSTableColumn alloc] initWithIdentifier:[NSString stringWithFormat:@"mark%d", mark]];
                if (!marks) { [tree release]; cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY; }
                [[marks dataCell] setFont:font];
                [[marks dataCell] setEditable:NO];
                [[marks dataCell] setAlignment:NSTextAlignmentCenter];
                [marks setWidth:((cocoa_app_state *)widget->app->platform)->cell_height * 2];
                [marks setResizingMask:NSTableColumnNoResizing];
                [marks setHidden:YES];
                [tree addTableColumn:marks]; [marks release];
            }
            [tree setHeaderView:nil]; [tree setAllowsMultipleSelection:NO];
            [tree setAllowsEmptySelection:YES]; [tree setAllowsColumnReordering:NO];
            [tree setColumnAutoresizingStyle:NSTableViewNoColumnAutoresizing];
            [tree setRowHeight:((cocoa_app_state *)widget->app->platform)->cell_height];
            [tree setDataSource:state->delegate]; [tree setDelegate:state->delegate];
            [tree setTarget:state->delegate]; [tree setDoubleAction:@selector(treeActivated:)];
            [tree setAction:@selector(treeClicked:)];
            NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
            if (!scroll) { [tree release]; cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY; }
            [scroll setBorderType:NSBezelBorder];
            [scroll setHasVerticalScroller:YES]; [scroll setHasHorizontalScroller:YES];
            [scroll setAutohidesScrollers:YES]; [scroll setDocumentView:tree];
            state->root = scroll; native = tree;
            break;
        }
        case XXWIDGETS_SCANRESULTS:
        case XXWIDGET_RESOURCESVIEWER:
        case XXWIDGET_IMPORTSVIEWER:
        case XXWIDGET_EXPORTSVIEWER:
        case XXWIDGET_SYMBOLSVIEWER:
        case XXWIDGETS_DEBUGGER_DISASM:
        case XXWIDGETS_DEBUGGER_REGISTERS:
        case XXWIDGETS_MEMORY_MAP:
        case XXWIDGETS_DEBUGGER_STACK:
        case XXWIDGET_SYSTEMPROCESSESVIEWER:
        case XXWIDGETS_ENTROPY_REGIONS:
        case XXWIDGETS_ENTROPY_BYTES:
        case XXWIDGETS_DATA_STRUCT_TABLE: {
            const CGFloat scan_widths[] = {100, 220, 100, 260};
            const CGFloat data_widths[] = {160, 110, 130, 120, 80, 110, 180, 180, 260};
            size_t column_count = xxwidgets_tabular_column_count(widget);
            NSTableView *table = widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER ?
                [[XXWidgetsProcessTable alloc] initWithFrame:NSZeroRect] :
                (xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget)) ? [[XXWidgetsBinaryTable alloc] initWithFrame:NSZeroRect] :
                [[NSTableView alloc] initWithFrame:NSZeroRect];
            if (widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER) {
                ((XXWidgetsProcessTable *)table)->process_widget = widget;
                [table setTarget:state->delegate];
                [table setDoubleAction:@selector(tableActivated:)];
            }
            if (xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget)) {
                ((XXWidgetsBinaryTable *)table)->binary_widget = widget;
                [table setTarget:state->delegate];
                [table setDoubleAction:@selector(tableActivated:)];
            }
            for (NSUInteger index = 0; index < column_count; ++index) {
                NSTableColumn *column = [[NSTableColumn alloc]
                    initWithIdentifier:[NSString stringWithFormat:@"%lu", (unsigned long)index]];
                NSString *title = [NSString stringWithUTF8String:xxwidgets_tabular_column_title(widget, index)];
                [[column headerCell] setStringValue:title ? title : @""];
                [[column dataCell] setFont:font];
                [[column dataCell] setEditable:NO];
                [[column dataCell] setWraps:NO];
                [[column dataCell] setLineBreakMode:NSLineBreakByTruncatingTail];
                [column setWidth:(xxwidgets_entropy_table(widget) || widget->kind == XXWIDGET_SYSTEMPROCESSESVIEWER || xxwidgets_binaryviewer_kind(widget) || xxwidgets_debugger_table_kind(widget)) ? xxwidgets_tabular_column_width(widget, index) * 8 : widget->kind == XXWIDGETS_SCANRESULTS ? scan_widths[index] : data_widths[index]];
                [column setMinWidth:xxwidgets_debugger_table_kind(widget) && index == 0 ? 16 : 60];
                [column setResizingMask:NSTableColumnUserResizingMask];
                [table addTableColumn:column];
                [column release];
            }
            [table setAllowsMultipleSelection:NO];
            [table setAllowsEmptySelection:YES];
            [table setAllowsColumnReordering:NO];
            [table setColumnAutoresizingStyle:NSTableViewNoColumnAutoresizing];
            [table setRowHeight:((cocoa_app_state *)widget->app->platform)->cell_height];
            [table setDataSource:state->delegate];
            [table setDelegate:state->delegate];
            NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
            [scroll setBorderType:NSBezelBorder];
            [scroll setHasVerticalScroller:YES];
            [scroll setHasHorizontalScroller:YES];
            [scroll setAutohidesScrollers:YES];
            [scroll setDocumentView:table];
            state->root = scroll;
            native = table;
            break;
        }
        case XXWIDGETS_ARCHIVEBROWSER: {
            NSArray *titles = @[@"Name", @"Size", @"Packed Size", @"Modified", @"Attributes"];
            XXWidgetsArchiveTable *table = [[XXWidgetsArchiveTable alloc] initWithFrame:NSZeroRect];
            table->archive_widget = widget;
            for (NSUInteger index = 0; index < [titles count]; ++index) {
                NSTableColumn *column = [[NSTableColumn alloc]
                    initWithIdentifier:[NSString stringWithFormat:@"%lu", (unsigned long)index]];
                [[column headerCell] setStringValue:[titles objectAtIndex:index]];
                if (!index) {
                    NSBrowserCell *name_cell = [[NSBrowserCell alloc] initTextCell:@""];
                    [column setDataCell:name_cell];
                    [name_cell release];
                }
                [[column dataCell] setFont:font];
                [[column dataCell] setEditable:NO];
                [[column dataCell] setLineBreakMode:NSLineBreakByTruncatingTail];
                if (index == 1 || index == 2) [[column dataCell] setAlignment:NSTextAlignmentRight];
                [column setWidth:index == 0 ? 260 : index == 3 ? 155 : 105];
                [column setMinWidth:60];
                [column setResizingMask:NSTableColumnUserResizingMask];
                [table addTableColumn:column];
                [column release];
            }
            [table setAllowsMultipleSelection:YES];
            [table setAllowsEmptySelection:YES];
            [table setAllowsColumnReordering:NO];
            [table setColumnAutoresizingStyle:NSTableViewNoColumnAutoresizing];
            [table setRowHeight:((cocoa_app_state *)widget->app->platform)->cell_height];
            [table setDataSource:state->delegate];
            [table setDelegate:state->delegate];
            [table setTarget:state->delegate];
            [table setDoubleAction:@selector(browserActivated:)];
            state->root = [[XXWidgetsContentView alloc] initWithFrame:NSZeroRect];
            state->browser_up = [[NSButton alloc] initWithFrame:NSZeroRect];
            [state->browser_up setTitle:@"\u2191"];
            [state->browser_up setToolTip:@"Up one folder (Backspace)"];
            [state->browser_up setBezelStyle:NSBezelStyleRounded];
            [state->browser_up setTarget:state->delegate];
            [state->browser_up setAction:@selector(browserUp:)];
            [state->root addSubview:state->browser_up];
            [state->browser_up release];
            state->browser_address = [[NSTextField alloc] initWithFrame:NSZeroRect];
            [state->browser_address setFont:font];
            [state->browser_address setEditable:NO];
            [state->browser_address setSelectable:YES];
            [[state->browser_address cell] setWraps:NO];
            [[state->browser_address cell] setLineBreakMode:NSLineBreakByTruncatingTail];
            [state->root addSubview:state->browser_address];
            [state->browser_address release];
            state->browser_scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
            [state->browser_scroll setBorderType:NSBezelBorder];
            [state->browser_scroll setHasVerticalScroller:YES];
            [state->browser_scroll setHasHorizontalScroller:YES];
            [state->browser_scroll setAutohidesScrollers:YES];
            [state->browser_scroll setDocumentView:table];
            [state->root addSubview:state->browser_scroll];
            [state->browser_scroll release];
            native = table;
            break;
        }
        default:
            cocoa_destroy_backend(widget);
            return XXWIDGETS_INVALID_ARGUMENT;
        }
        if (!native) { cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY; }
        widget->native = native;
        if (!state->root) state->root = native;
        if (widget->parent)
            [[(NSWindow *)widget->parent->native contentView] addSubview:state->root];
        xxwidgets_status status = cocoa_sync_backend(widget);
        if (status != XXWIDGETS_OK) cocoa_destroy_backend(widget);
        return status;
    }
}

static void cocoa_destroy_backend(xxwidgets_widget *widget)
{
    cocoa_widget_state *state = widget->platform;
    if (!state) return;
    @autoreleasepool {
        id native = widget->native;
        if (state->menu) {
            cocoa_app_state *app = widget->app->platform;
            if ([NSApp mainMenu] == state->menu) [NSApp setMainMenu:app->base_menu];
            [state->menu release];
        }
        if (state->delegate) state->delegate->widget = NULL;
        if (widget->kind == XXWIDGETS_WINDOW) {
            if (native) ((XXWidgetsContentView *)[(NSWindow *)native contentView])->drop_window = NULL;
            if (state->field_editor) ((XXWidgetsFieldEditor *)state->field_editor)->drop_window = NULL;
            [(NSWindow *)native endEditingFor:nil];
            [state->field_editor release];
            [(NSWindow *)native setDelegate:nil];
            [(NSWindow *)native orderOut:nil];
            [(NSWindow *)native close];
        } else {
            if (widget->kind == XXWIDGETS_EDIT) [(NSTextField *)native setDelegate:nil];
            if (widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT && native) {
                [(XXWidgetsDieSignatureView *)native unmarkText];
                ((XXWidgetsDieSignatureView *)native)->signature_widget = NULL;
            }
            if (xxwidgets_list_kind(widget)) {
                [(NSTableView *)native setDelegate:nil];
                [(NSTableView *)native setDataSource:nil];
            }
            if (widget->kind == XXWIDGETS_HEXVIEW && native) {
                ((XXWidgetsHexTable *)native)->hex_widget = NULL;
                ((XXWidgetsHexScroll *)state->root)->hex_widget = NULL;
                ((XXWidgetsHexScroll *)state->root)->overview->hex_widget = NULL;
            }
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
                ((XXWidgetsArchiveTable *)native)->archive_widget = NULL;
                [(NSTableView *)native setTarget:nil];
                [state->browser_up setTarget:nil];
            }
            if (widget->kind == XXWIDGETS_TREEVIEW && native) {
                ((XXWidgetsTree *)native)->tree_widget = NULL;
                [(NSOutlineView *)native setTarget:nil];
            }
            if (widget->kind == XXWIDGETS_BUTTON || widget->kind == XXWIDGETS_CHECKBOX || xxwidgets_combo_kind(widget))
                [(NSButton *)native setTarget:nil];
            [state->root removeFromSuperview];
            if (state->root != native) [state->root release];
        }
        [state->combo_popover close]; [state->combo_popover release];
        [state->tree_items release];
        [state->preview_font release];
        [state->hex_font release];
        [state->applied_font release];
        [native release];
        [state->delegate release];
        free(state);
        widget->native = NULL;
        widget->platform = NULL;
    }
}

static xxwidgets_status cocoa_sync_backend(xxwidgets_widget *widget)
{
    @autoreleasepool {
        cocoa_app_state *app_state = widget->app->platform;
        cocoa_widget_state *state = widget->platform;
        id native = widget->native;
        NSString *text = widget->kind == XXWIDGETS_DIE_SIGNATURE_EDIT ? @"" : [NSString stringWithUTF8String:widget->text];
        if (!text) return XXWIDGETS_INVALID_ARGUMENT;
        NSRect frame = NSMakeRect(widget->rect.x * app_state->cell_width,
            widget->rect.y * app_state->cell_height, widget->rect.width * app_state->cell_width,
            widget->rect.height * app_state->cell_height);
        if (!state->has_rect || !rect_equal(state->applied_rect, widget->rect)) {
            if (widget->kind == XXWIDGETS_WINDOW) {
                NSWindow *window = native;
                [window setContentSize:frame.size];
                NSScreen *screen = [window screen] ? [window screen] : [NSScreen mainScreen];
                NSRect available = [screen visibleFrame];
                [window setFrameTopLeftPoint:NSMakePoint(NSMinX(available) + frame.origin.x,
                                                        NSMaxY(available) - frame.origin.y)];
            } else {
                [state->root setFrame:frame];
                if (xxwidgets_list_kind(widget))
                    cocoa_size_table(widget);
            }
            state->applied_rect = widget->rect;
            state->has_rect = 1;
        }
        switch (widget->kind) {
        case XXWIDGETS_WINDOW:
            [(NSWindow *)native setTitle:text];
            break;
        case XXWIDGETS_TREEVIEW: {
            xxwidgets_status status = cocoa_tree_sync(widget);
            if (status != XXWIDGETS_OK) return status;
            [(NSOutlineView *)native setAccessibilityLabel:text];
            break;
        }
        case XXWIDGETS_COMBOBOX: {
            NSPopUpButton *button = native;
            if (!state->combo_initialized || state->combo_revision != widget->combo_revision) {
                NSMenu *menu = [[NSMenu alloc] initWithTitle:@""];
                [menu setAutoenablesItems:NO];
                for (size_t index = 0; index < widget->item_count; ++index) {
                    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:
                        [NSString stringWithUTF8String:widget->items[index]] action:NULL keyEquivalent:@""];
                    [menu addItem:item]; [item release];
                }
                [button setMenu:menu]; [menu release];
                state->combo_initialized = 1; state->combo_revision = widget->combo_revision;
            }
            [button selectItemAtIndex:widget->value];
            break;
        }
        case XXWIDGETS_CHECKCOMBOBOX: {
            CGFloat width = MAX(24 * app_state->cell_width, frame.size.width);
            CGFloat height = (widget->item_count < 10 ? widget->item_count : 10) * app_state->cell_height + 8;
            [(NSButton *)native setTitle:[NSString stringWithFormat:@"%@ \u25be", [NSString stringWithUTF8String:xxwidgets_combobox_caption(widget)]]];
            if (!state->combo_initialized || state->combo_revision != widget->combo_revision) {
                NSArray *views = [[state->combo_checks subviews] copy];
                for (NSView *view in views) [view removeFromSuperview];
                [views release];
                for (size_t index = 0; index < widget->item_count; ++index) {
                    NSButton *check = [[NSButton alloc] initWithFrame:NSMakeRect(4, index * app_state->cell_height,
                        width - 24, app_state->cell_height)];
                    [check setButtonType:NSButtonTypeSwitch]; [check setFont:[NSFont systemFontOfSize:[NSFont systemFontSize]]];
                    [check setTitle:[NSString stringWithUTF8String:widget->items[index]]]; [check setTag:(NSInteger)index];
                    [check setTarget:state->delegate]; [check setAction:@selector(comboChecked:)];
                    [state->combo_checks addSubview:check]; [check release];
                }
                state->combo_initialized = 1; state->combo_revision = widget->combo_revision;
            }
            [state->combo_checks setFrameSize:NSMakeSize(width, MAX(height, widget->item_count * app_state->cell_height))];
            [state->combo_popover setContentSize:NSMakeSize(width, height)];
            for (NSButton *check in [state->combo_checks subviews]) {
                [check setFrameSize:NSMakeSize(width - 24, app_state->cell_height)];
                [check setState:xxwidgets_checkcombobox_checked(widget, (size_t)[check tag]) ? NSControlStateValueOn : NSControlStateValueOff];
            }
            if (!widget->visible || !widget->enabled || !widget->parent->enabled || !widget->item_count) [state->combo_popover close];
            break;
        }
        case XXWIDGETS_CANVAS:
        case XXWIDGETS_SPLITTER:
        case XXWIDGETS_ENTROPY_PLOT: [(NSView *)native setNeedsDisplay:YES]; break;
        case XXWIDGETS_LABEL:
            [[(NSTextField *)native cell] setLineBreakMode:widget->keep_end ?
                NSLineBreakByTruncatingMiddle : NSLineBreakByTruncatingTail];
            [(NSTextField *)native setStringValue:text];
            break;
        case XXWIDGETS_EDIT: {
            NSTextField *field = native;
            if (![edit_text(field) isEqualToString:text]) {
                [field setStringValue:text];
                NSText *editor = [field currentEditor];
                if (editor) {
                    [editor setString:text];
                    [editor setSelectedRange:NSMakeRange([text length], 0)];
                }
            }
            break;
        }
        case XXWIDGETS_DIE_SIGNATURE_EDIT:
            [(XXWidgetsDieSignatureView *)native refresh];
            break;
        case XXWIDGETS_BUTTON:
            [(NSButton *)native setTitle:text];
            break;
        case XXWIDGETS_CHECKBOX:
            [(NSButton *)native setTitle:text];
            [(NSButton *)native setState:widget->value ? NSControlStateValueOn : NSControlStateValueOff];
            break;
        case XXWIDGETS_LISTBOX:
        case XXWIDGETS_ARCHIVEVIEW:
        case XXWIDGETS_ARCHIVEBROWSER:
        case XXWIDGETS_SCANRESULTS:
        case XXWIDGET_RESOURCESVIEWER:
        case XXWIDGET_IMPORTSVIEWER:
        case XXWIDGET_EXPORTSVIEWER:
        case XXWIDGET_SYMBOLSVIEWER:
        case XXWIDGETS_DEBUGGER_DISASM:
        case XXWIDGETS_DEBUGGER_REGISTERS:
        case XXWIDGETS_MEMORY_MAP:
        case XXWIDGETS_DEBUGGER_STACK:
        case XXWIDGET_SYSTEMPROCESSESVIEWER:
        case XXWIDGETS_ENTROPY_REGIONS:
        case XXWIDGETS_ENTROPY_BYTES:
        case XXWIDGETS_DATA_STRUCT_TABLE:
        case XXWIDGETS_HEXVIEW: {
            NSTableView *table = native;
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
                NSSize size = [state->root frame].size;
                CGFloat address_height = app_state->cell_height + 8;
                [state->browser_up setFrame:NSMakeRect(0, 0, 32, address_height)];
                [state->browser_address setFrame:NSMakeRect(36, 2, MAX(1, size.width - 36), address_height - 4)];
                [state->browser_scroll setFrame:NSMakeRect(0, address_height + 2, size.width,
                    MAX(1, size.height - address_height - 2))];
                NSString *archive = [NSString stringWithUTF8String:xxwidgets_archivebrowser_archive(widget)];
                NSString *directory = [NSString stringWithUTF8String:xxwidgets_archivebrowser_directory(widget)];
                [state->browser_address setStringValue:[NSString stringWithFormat:@"%@:/%@", archive, directory]];
                if (state->rendered_hex_revision != widget->browser_revision) {
                    while ([[table tableColumns] count] > 5)
                        [table removeTableColumn:[[table tableColumns] objectAtIndex:5]];
                    for (size_t index = 5; index < xxwidgets_archivebrowser_column_count(widget); ++index) {
                        NSTableColumn *column = [[NSTableColumn alloc]
                            initWithIdentifier:[NSString stringWithFormat:@"%zu", index]];
                        [[column headerCell] setStringValue:[NSString stringWithUTF8String:
                            xxwidgets_archivebrowser_column_title(widget, index)]];
                        [[column dataCell] setFont:[[[[table tableColumns] objectAtIndex:0] dataCell] font]];
                        [[column dataCell] setEditable:NO];
                        [[column dataCell] setLineBreakMode:NSLineBreakByTruncatingTail];
                        [column setWidth:150]; [column setMinWidth:60];
                        [column setResizingMask:NSTableColumnUserResizingMask];
                        [table addTableColumn:column]; [column release];
                    }
                }
                [table setHighlightedTableColumn:[[table tableColumns]
                    objectAtIndex:xxwidgets_archivebrowser_sort_column(widget)]];
            }
            BOOL refreshed = state->rendered_items != widget->item_count ||
                (xxwidgets_formatted_rows(widget) &&
                 state->rendered_hex_revision != xxwidgets_row_revision(widget));
            if (widget->kind == XXWIDGETS_HEXVIEW) {
                [(XXWidgetsHexScroll *)state->root refreshOverview];
                [table setNeedsDisplay:YES];
                for (size_t i = 0; i < 3; ++i) {
                    NSTableColumn *column = [[table tableColumns] objectAtIndex:i];
                    NSAttributedString *title = [[[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:xxwidgets_hexview_title(widget, i)]
                        attributes:@{NSFontAttributeName:[[column dataCell] font]}] autorelease];
                    [[column headerCell] setAttributedStringValue:title];
                    [column setWidth:xxwidgets_hexview_column_width(widget, i) * cocoa_hex_cell_width(widget)];
                }
                uint64_t revision = xxwidgets_row_revision(widget);
                BOOL revision_changed = state->rendered_hex_revision != revision;
                refreshed = !state->hex_rows_initialized || state->rendered_items != widget->item_count ||
                    widget->hex_sync_full;
                if (revision_changed && !refreshed) {
                    NSMutableIndexSet *dirty_rows = [NSMutableIndexSet indexSet];
                    if (widget->hex_sync_dirty_count > sizeof(widget->hex_sync_dirty_rows) / sizeof(widget->hex_sync_dirty_rows[0]))
                        refreshed = YES;
                    for (size_t dirty = 0; !refreshed && dirty < widget->hex_sync_dirty_count; ++dirty) {
                        size_t index = widget->hex_sync_dirty_rows[dirty];
                        if (index >= widget->item_count) {
                            refreshed = YES;
                            break;
                        }
                        [dirty_rows addIndex:(NSUInteger)index];
                    }
                    if (!refreshed) {
                        if ([dirty_rows count]) {
                            NSIndexSet *columns = [NSIndexSet indexSetWithIndexesInRange:
                                NSMakeRange(0, [[table tableColumns] count])];
                            [table reloadDataForRowIndexes:dirty_rows columnIndexes:columns];
                        }
                        state->rendered_hex_revision = revision;
                    }
                }
            }
            if (refreshed) {
                if (xxwidgets_debugger_table_kind(widget)) {
                    CGFloat character_width = cocoa_hex_cell_width(widget);
                    for (size_t index = 0; index < xxwidgets_tabular_column_count(widget); ++index) {
                        NSTableColumn *column = [[table tableColumns] objectAtIndex:index];
                        CGFloat needed = ceil(xxwidgets_tabular_column_width(widget, index) * character_width) + 12;
                        /* New models may contain wider SIMD values. Keep any
                         * additional width chosen by the user. */
                        if ([column width] < needed) [column setWidth:needed];
                    }
                }
                if (xxwidgets_formatted_rows(widget) && widget->kind != XXWIDGETS_ARCHIVEBROWSER &&
                    widget->kind != XXWIDGETS_SCANRESULTS && widget->kind != XXWIDGET_SYSTEMPROCESSESVIEWER && !xxwidgets_binaryviewer_kind(widget) && !xxwidgets_debugger_table_kind(widget) && widget->kind != XXWIDGETS_HEXVIEW && !xxwidgets_entropy_table(widget) && widget->kind != XXWIDGETS_DATA_STRUCT_TABLE) {
                    NSFont *row_font = [[[[table tableColumns] objectAtIndex:0] dataCell] font];
                    NSDictionary *attributes = [NSDictionary dictionaryWithObject:row_font
                        forKey:NSFontAttributeName];
                    size_t row;
                    state->hex_row_width = 0;
                    for (row = 0; row < widget->item_count; ++row) {
                        NSString *line = [NSString stringWithUTF8String:widget->items[row]];
                        CGFloat width = ceil([line sizeWithAttributes:attributes].width) + 12;
                        if (width > state->hex_row_width) state->hex_row_width = width;
                    }
                }
                [table reloadData];
                cocoa_size_table(widget);
                state->rendered_items = widget->item_count;
                state->rendered_hex_revision = xxwidgets_row_revision(widget);
                if (widget->kind == XXWIDGETS_HEXVIEW) state->hex_rows_initialized = 1;
            }
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER || xxwidgets_tabular_kind(widget))
                cocoa_size_table(widget);
            BOOL selection_changed = [table selectedRow] != widget->value;
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER) {
                NSMutableIndexSet *indexes = [NSMutableIndexSet indexSet];
                for (size_t i = 0; i < widget->item_count; ++i)
                    if (xxwidgets_archivebrowser_row_selected(widget, i)) [indexes addIndex:(NSUInteger)i];
                selection_changed = ![indexes isEqualToIndexSet:[table selectedRowIndexes]];
                if (selection_changed) [table selectRowIndexes:indexes byExtendingSelection:NO];
            } else if (selection_changed) {
                if (widget->value < 0) [table deselectAll:nil];
                else [table selectRowIndexes:[NSIndexSet indexSetWithIndex:(NSUInteger)widget->value]
                        byExtendingSelection:NO];
            }
            if (xxwidgets_formatted_rows(widget) && widget->value >= 0 &&
                (refreshed || selection_changed)) [table scrollRowToVisible:widget->value];
            [table setAccessibilityLabel:text];
            break;
        }
        case XXWIDGETS_PROGRESS:
            [(NSProgressIndicator *)native setDoubleValue:widget->value];
            [(NSProgressIndicator *)native setAccessibilityLabel:text];
            break;
        }
        cocoa_widget_fonts(widget);
        if (widget->kind == XXWIDGETS_WINDOW) {
            NSWindow *window = native;
            [window setIgnoresMouseEvents:!widget->enabled];
            if (!widget->enabled) [window makeFirstResponder:nil];
            xxwidgets_widget *child;
            for (child = widget->app->widgets; child; child = child->next)
                if (child->parent == widget) cocoa_enable_control(child);
            if (widget->visible && ![window isVisible]) {
                [window makeKeyAndOrderFront:nil];
                [NSApp activateIgnoringOtherApps:YES];
            } else if (!widget->visible && [window isVisible]) [window orderOut:nil];
        } else {
            cocoa_enable_control(widget);
            [state->root setHidden:!widget->visible];
        }
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_read_text_backend(xxwidgets_widget *widget)
{
    @autoreleasepool {
        id native = widget->native;
        NSString *text;
        switch (widget->kind) {
        case XXWIDGETS_WINDOW: text = [(NSWindow *)native title]; break;
        case XXWIDGETS_LABEL: text = [(NSTextField *)native stringValue]; break;
        case XXWIDGETS_EDIT: text = edit_text(native); break;
        case XXWIDGETS_DIE_SIGNATURE_EDIT: return XXWIDGETS_OK; /* The C model already owns source text. */
        case XXWIDGETS_BUTTON:
        case XXWIDGETS_CHECKBOX: text = [(NSButton *)native title]; break;
        default: return XXWIDGETS_OK;
        }
        const char *utf8 = [text UTF8String];
        return utf8 ? xxwidgets_store_text(widget, utf8) : XXWIDGETS_PLATFORM_ERROR;
    }
}

static xxwidgets_status cocoa_read_value_backend(xxwidgets_widget *widget)
{
    @autoreleasepool {
        if (widget->kind == XXWIDGETS_COMBOBOX)
            widget->value = (int)[(NSPopUpButton *)widget->native indexOfSelectedItem];
        else if (widget->kind == XXWIDGETS_CHECKBOX)
            widget->value = [(NSButton *)widget->native state] == NSControlStateValueOn ? 1 : 0;
        else if (widget->kind == XXWIDGETS_TREEVIEW) {
            NSOutlineView *tree = widget->native;
            NSInteger row = [tree selectedRow];
            id item = row < 0 ? nil : [tree itemAtRow:row];
            widget->value = item ? (int)[item unsignedLongLongValue] : -1;
        }
        else if (xxwidgets_list_kind(widget))
            widget->value = (int)[(NSTableView *)widget->native selectedRow];
        else if (widget->kind == XXWIDGETS_PROGRESS)
            widget->value = (int)([(NSProgressIndicator *)widget->native doubleValue] + 0.5);
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_focus_backend(xxwidgets_widget *widget)
{
    @autoreleasepool {
        NSWindow *window = widget->parent ? widget->parent->native : widget->native;
        [window makeKeyAndOrderFront:nil];
        if (widget->kind != XXWIDGETS_WINDOW && ![window makeFirstResponder:widget->native])
            return XXWIDGETS_PLATFORM_ERROR;
        return XXWIDGETS_OK;
    }
}

static int cocoa_has_focus(const xxwidgets_widget *widget)
{
    @autoreleasepool {
        NSWindow *window = widget->parent ? widget->parent->native : widget->native;
        if (!window || !widget->visible || !widget->enabled || (widget->parent && (!widget->parent->visible || !widget->parent->enabled))) return 0;
        if (widget->kind == XXWIDGETS_WINDOW) return [window isKeyWindow] ? 1 : 0;
        /* The assigned responder is meaningful before AppKit delivers the
         * become-key notification, including synthetic input in UI tests. */
        NSResponder *responder = [window firstResponder];
        if (!responder) return 0;
        if (responder == widget->native) return 1;
        if (widget->kind == XXWIDGETS_EDIT && [(NSTextField *)widget->native currentEditor] == responder) return 1;
        cocoa_widget_state *state = widget->platform;
        return state && state->root && [responder isKindOfClass:[NSView class]] && [(NSView *)responder isDescendantOf:state->root];
    }
}

static xxwidgets_status cocoa_modal_owner(xxwidgets_widget *dialog, xxwidgets_widget *owner, int active)
{
    @autoreleasepool {
        NSWindow *window = dialog->native, *parent = owner->native;
        if (active) {
            NSRect area = [parent frame], bounds = [window frame];
            NSScreen *screen = [parent screen] ? [parent screen] : [NSScreen mainScreen];
            NSRect available = [screen visibleFrame];
            NSPoint origin = NSMakePoint(NSMidX(area) - bounds.size.width / 2, NSMidY(area) - bounds.size.height / 2);
            /* A dialog wider than an owner near a screen edge must not lose
             * its labels or buttons off-screen. Keep oversized dialogs at the
             * top-left so their title bar remains reachable. */
            origin.x = MAX(NSMinX(available), MIN(origin.x, NSMaxX(available) - bounds.size.width));
            origin.y = MIN(NSMaxY(available) - bounds.size.height, MAX(origin.y, NSMinY(available)));
            [window setFrameOrigin:origin];
            [parent addChildWindow:window ordered:NSWindowAbove];
            for (xxwidgets_widget *widget = dialog->app->widgets; widget; widget = widget->next)
                if (widget->parent == dialog && widget->kind == XXWIDGETS_BUTTON) {
                    if (widget == dialog->app->modal_default || !strcmp(widget->text, "OK"))
                        [(NSButton *)widget->native setKeyEquivalent:@"\r"];
                    if (!strcmp(widget->text, "Cancel")) [(NSButton *)widget->native setKeyEquivalent:@"\033"];
                }
            [window makeKeyAndOrderFront:nil];
        } else {
            [parent removeChildWindow:window]; [parent makeKeyAndOrderFront:nil];
        }
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_about_content(xxwidgets_widget *window,
    const xxwidgets_about_dialog *about, const char *body)
{
    @autoreleasepool {
        cocoa_app_state *app = window->app->platform;
        NSWindow *native = window->native;
        NSView *content = [native contentView];
        CGFloat padding = 2 * app->cell_width, image_box = 12 * app->cell_width;
        CGFloat text_x = about->image ? image_box + 2 * padding : padding;
        CGFloat width = NSWidth([content bounds]) - text_x - padding;
        [native setStyleMask:[native styleMask] & ~(NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable)];
        NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:
            NSMakeRect(text_x, app->cell_height, width, 17 * app->cell_height)];
        NSTextView *text = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, width, 17 * app->cell_height)];
        if (!scroll || !text) { [scroll release]; [text release]; return XXWIDGETS_OUT_OF_MEMORY; }
        [scroll setHasVerticalScroller:YES]; [scroll setHasHorizontalScroller:NO];
        [scroll setAutohidesScrollers:YES];
        [text setEditable:NO]; [text setSelectable:YES]; [text setRichText:NO];
        [text setFont:cocoa_role_font(window, XXWIDGETS_FONT_TEXT_EDITS)];
        [text setString:[NSString stringWithUTF8String:body]];
        [text setVerticallyResizable:YES]; [text setHorizontallyResizable:NO];
        [text setAutoresizingMask:NSViewWidthSizable];
        [[text textContainer] setContainerSize:NSMakeSize(width, CGFLOAT_MAX)];
        [[text textContainer] setWidthTracksTextView:YES];
        [scroll setDocumentView:text]; [content addSubview:scroll];
        ((cocoa_widget_state *)window->platform)->about_text = text;
        [text release]; [scroll release];
        if (about->image) {
            NSBitmapImageRep *pixels = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                pixelsWide:about->image_width pixelsHigh:about->image_height bitsPerSample:8
                samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
                bitmapFormat:NSBitmapFormatAlphaNonpremultiplied bytesPerRow:(NSInteger)about->image_width * 4
                bitsPerPixel:32];
            if (!pixels || ![pixels bitmapData]) { [pixels release]; return XXWIDGETS_OUT_OF_MEMORY; }
            for (unsigned int row = 0; row < about->image_height; ++row)
                memcpy([pixels bitmapData] + (size_t)row * [pixels bytesPerRow],
                       about->image + (size_t)row * about->image_width * 4, (size_t)about->image_width * 4);
            NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(about->image_width, about->image_height)];
            NSImageView *view = [[NSImageView alloc] initWithFrame:
                NSMakeRect(padding, app->cell_height, image_box, image_box)];
            if (!image || !view) { [pixels release]; [image release]; [view release]; return XXWIDGETS_OUT_OF_MEMORY; }
            [image addRepresentation:pixels]; [view setImage:image];
            [view setImageScaling:NSImageScaleProportionallyUpOrDown];
            [content addSubview:view]; [pixels release]; [image release]; [view release];
        }
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_copy_text(xxwidgets_widget *window, const char *text)
{
    (void)window;
    @autoreleasepool {
        NSPasteboard *clipboard = [NSPasteboard generalPasteboard];
        [clipboard clearContents];
        return [clipboard setString:[NSString stringWithUTF8String:text] forType:NSPasteboardTypeString]
            ? XXWIDGETS_OK : XXWIDGETS_PLATFORM_ERROR;
    }
}

static xxwidgets_status cocoa_paste_text(xxwidgets_widget *owner, char *text, size_t capacity, size_t *required)
{
    (void)owner;
    @autoreleasepool {
        NSString *value = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        const char *bytes = value ? [value UTF8String] : "";
        if (!bytes) return XXWIDGETS_PLATFORM_ERROR;
        *required = strlen(bytes) + 1;
        if (!text && !capacity) return XXWIDGETS_OK;
        if (capacity < *required) return XXWIDGETS_BUFFER_TOO_SMALL;
        memcpy(text, bytes, *required); return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_choose_file(xxwidgets_widget *owner, xxwidgets_file_dialog_mode mode, const char *title, const char *initial, char **path, int *accepted)
{
    (void)owner;
    @autoreleasepool {
        NSSavePanel *panel = mode == XXWIDGETS_FILE_DIALOG_OPEN ? [NSOpenPanel openPanel] : [NSSavePanel savePanel];
        if (mode == XXWIDGETS_FILE_DIALOG_OPEN) {
            [(NSOpenPanel *)panel setCanChooseDirectories:NO];
            [(NSOpenPanel *)panel setAllowsMultipleSelection:NO];
        }
        if (title) [panel setTitle:[NSString stringWithUTF8String:title]];
        if (initial && *initial) {
            NSString *name = [NSString stringWithUTF8String:initial];
            if (!name) return XXWIDGETS_INVALID_ARGUMENT;
            BOOL directory = NO;
            if ([[NSFileManager defaultManager] fileExistsAtPath:name isDirectory:&directory] && directory)
                [panel setDirectoryURL:[NSURL fileURLWithPath:name isDirectory:YES]];
            else {
                if ([name isAbsolutePath]) [panel setDirectoryURL:[NSURL fileURLWithPath:[name stringByDeletingLastPathComponent]]];
                [panel setNameFieldStringValue:[name lastPathComponent]];
            }
        }
        if ([panel runModal] == NSModalResponseOK) {
            *path = xxwidgets_strdup([[[panel URL] path] UTF8String]);
            if (!*path) return XXWIDGETS_OUT_OF_MEMORY;
            *accepted = 1;
        }
        return XXWIDGETS_OK;
    }
}

static xxwidgets_status cocoa_accept_file_drops(xxwidgets_widget *window, int accept)
{
    NSView *content = [(NSWindow *)window->native contentView];
    if (accept) [content registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    else [content unregisterDraggedTypes];
    return XXWIDGETS_OK;
}

static size_t cocoa_hexview_page_rows(const xxwidgets_widget *widget)
{
    const cocoa_widget_state *state = widget->platform;
    NSTableView *table = widget->native;
    if (!state || !state->root || !table)
        return (size_t)(widget->rect.height > 1 ? widget->rect.height - 1 : 1);
    CGFloat height = [[(NSScrollView *)state->root contentView] bounds].size.height;
    CGFloat row_height = [table rowHeight] + [table intercellSpacing].height;
    size_t rows = height > 0 && row_height > 0 ? (size_t)(height / row_height) : 0;
    return rows ? rows : 1;
}

static xxwidgets_status cocoa_window_menu(xxwidgets_widget *window)
{
    @autoreleasepool {
        cocoa_widget_state *state = window->platform;
        cocoa_app_state *app = window->app->platform;
        NSMenu *menu = nil, *group = nil;
        const char *last = NULL;
        if (window->menu_count) {
            menu = [app->base_menu copy];
            if (!menu) return XXWIDGETS_OUT_OF_MEMORY;
            NSInteger group_index = 1; /* Keep the application menu first. */
            for (size_t i = 0; i < window->menu_count; ++i) {
                const xxwidgets_menu_item *entry = &window->menu_items[i];
                if (!last || strcmp(last, entry->menu)) {
                    NSString *title = [NSString stringWithUTF8String:entry->menu];
                    NSMenuItem *heading = [[NSMenuItem alloc] initWithTitle:title action:NULL keyEquivalent:@""];
                    group = [[NSMenu alloc] initWithTitle:title];
                    if (!heading || !group) { [heading release]; [group release]; [menu release]; return XXWIDGETS_OUT_OF_MEMORY; }
                    [heading setSubmenu:group];
                    [menu insertItem:heading atIndex:group_index++];
                    [heading release]; [group release];
                    last = entry->menu;
                }
                if (!*entry->label) [group addItem:[NSMenuItem separatorItem]];
                else {
                    NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:[NSString stringWithUTF8String:entry->label]
                        action:@selector(menuActivated:) keyEquivalent:@""];
                    if (!item) { [menu release]; return XXWIDGETS_OUT_OF_MEMORY; }
                    [item setTarget:state->delegate];
                    [item setTag:(NSInteger)i];
                    [item setEnabled:entry->enabled];
                    [group addItem:item];
                    [item release];
                }
            }
        }
        NSMenu *previous = state->menu;
        state->menu = menu;
        if ([NSApp mainMenu] == previous || [(NSWindow *)window->native isMainWindow] || [(NSWindow *)window->native isKeyWindow])
            [NSApp setMainMenu:menu ? menu : app->base_menu];
        [previous release];
        return XXWIDGETS_OK;
    }
}

const xxwidgets_backend_ops xxwidgets_native_ops = {
    "AppKit", cocoa_init_backend, cocoa_shutdown_backend, cocoa_poll_backend,
    cocoa_create_backend, cocoa_destroy_backend, cocoa_sync_backend,
    cocoa_read_text_backend, cocoa_read_value_backend, cocoa_focus_backend, cocoa_modal_owner, cocoa_about_content, cocoa_copy_text,
    cocoa_apply_fonts, NULL, cocoa_preview_font, NULL, NULL, cocoa_choose_file, cocoa_has_focus, cocoa_accept_file_drops, cocoa_hexview_page_rows, cocoa_paste_text, cocoa_hexview_font, cocoa_window_menu, NULL
};
