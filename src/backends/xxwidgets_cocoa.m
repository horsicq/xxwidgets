#include "xxwidgets_internal.h"

#import <AppKit/AppKit.h>
#include <math.h>
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
    NSFont *applied_font;
} cocoa_widget_state;

static xxwidgets_app *active_app;
static int finished_launching;

static void cocoa_size_table(xxwidgets_widget *widget);

/* Returned fonts are retained. Build every role before changing any views so
 * a font allocation failure leaves the preceding application fonts intact. */
static NSFont *cocoa_font_create(const xxwidgets_font *settings, int formatted)
{
    CGFloat size = settings->point_size ? settings->point_size : [NSFont systemFontSize];
    NSFont *base = formatted ? [NSFont userFixedPitchFontOfSize:size] : [NSFont systemFontOfSize:size];
    NSFontManager *manager = [NSFontManager sharedFontManager];
    if (!base) base = [NSFont systemFontOfSize:size];
    if (!base) return nil;
    if (settings->family[0]) {
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
    if (!state->preview_font && (widget->kind == XXWIDGETS_HEXVIEW || widget->kind == XXWIDGETS_ARCHIVEVIEW)) {
        font = app->formatted_font ? app->formatted_font : [NSFont userFixedPitchFontOfSize:[NSFont systemFontSize]];
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
        if (font_changed && xxwidgets_formatted_rows(widget) && widget->kind != XXWIDGETS_SCANRESULTS &&
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

static int rect_equal(xxwidgets_rect a, xxwidgets_rect b)
{
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

static NSString *edit_text(NSTextField *field)
{
    NSText *editor = [field currentEditor];
    return editor ? [editor string] : [field stringValue];
}

@interface XXWidgetsContentView : NSView
@end

@implementation XXWidgetsContentView
- (BOOL)isFlipped { return YES; }
@end

@interface XXWidgetsDelegate : NSObject <NSWindowDelegate, NSTextFieldDelegate,
    NSTableViewDataSource, NSTableViewDelegate, NSOutlineViewDataSource, NSOutlineViewDelegate> {
@public
    xxwidgets_widget *widget;
}
- (void)clicked:(id)sender;
- (void)comboSelected:(id)sender;
- (void)comboOpen:(id)sender;
- (void)comboChecked:(id)sender;
- (void)browserUp:(id)sender;
- (void)browserActivated:(id)sender;
- (void)treeActivated:(id)sender;
@end

@interface XXWidgetsTree : NSOutlineView {
@public
    xxwidgets_widget *tree_widget;
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
    }
    [super keyDown:event];
}
@end

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

@implementation XXWidgetsDelegate
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

- (void)browserActivated:(id)sender
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    NSInteger row = [(NSTableView *)sender clickedRow];
    if (row >= 0) xxwidgets_archivebrowser_user_activate(widget, (size_t)row);
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
    (void)outline; (void)column;
    if (!widget || !item) return @"";
    const char *value = xxwidgets_treeview_display_text(widget, (size_t)[item unsignedLongLongValue]);
    NSString *result = [NSString stringWithUTF8String:value ? value : ""];
    return result ? result : @"";
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
    if (widget->kind == XXWIDGETS_SCANRESULTS) {
        const char *value = xxwidgets_scanresults_cell(widget, (size_t)row,
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
    (void)table;
    if (!widget || widget->kind != XXWIDGETS_ARCHIVEBROWSER ||
        [[column identifier] intValue] != 0 || row < 0) return;
    xxwidgets_archive_browser_entry entry;
    size_t source;
    if (xxwidgets_archivebrowser_get_entry(widget, (size_t)row, &source, &entry) != XXWIDGETS_OK) return;
    [(NSBrowserCell *)cell setLeaf:!entry.is_directory];
    [(NSBrowserCell *)cell setImage:[NSImage imageNamed:entry.is_directory ?
        NSImageNameFolder : NSImageNameMultipleDocuments]];
}

- (void)tableViewSelectionDidChange:(NSNotification *)notification
{
    if (!widget || widget->app->syncing || !xxwidgets_focusable(widget)) return;
    widget->value = (int)[(NSTableView *)[notification object] selectedRow];
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
        for (size_t role = 0; role < XXWIDGETS_FONT_ROLE_COUNT; ++role) [state->fonts[role] release];
        [state->formatted_font release];
        free(state);
        app->platform = NULL;
    }
}

static int cocoa_shortcut(xxwidgets_app *app, NSEvent *event)
{
    xxwidgets_widget *window;
    NSString *characters;
    unsigned int key, modifiers = 0;
    if ([event type] != NSEventTypeKeyDown) return 0;
    for (window = app->widgets; window; window = window->next)
        if (window->kind == XXWIDGETS_WINDOW && window->native == [event window]) break;
    if (!window) return 0;
    characters = [event charactersIgnoringModifiers];
    if (![characters length]) return 0;
    key = [characters characterAtIndex:0];
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
    return xxwidgets_shortcut_dispatch(window, key, modifiers);
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
            else if (app->modal_default && [event type] == NSEventTypeKeyDown &&
                [event window] == app->modal_window->native &&
                ([event keyCode] == 36 || [event keyCode] == 76))
                xxwidgets_emit(app->modal_default, XXWIDGETS_EVENT_CLICK, 0);
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
    if (xxwidgets_list_kind(widget) ||
        widget->kind == XXWIDGETS_PROGRESS)
        [state->root setAlphaValue:enabled ? 1.0 : 0.5];
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
    if (widget->kind == XXWIDGETS_ARCHIVEBROWSER || widget->kind == XXWIDGETS_SCANRESULTS) {
        CGFloat column_width = 0;
        for (NSTableColumn *column in [table tableColumns]) column_width += [column width];
        frame.size.width = MAX(content.width, column_width);
        [table setFrame:frame];
        return;
    }
    frame.size.width = xxwidgets_formatted_rows(widget) ?
        MAX(content.width, state->hex_row_width) : content.width;
    [table setFrame:frame];
    if (xxwidgets_formatted_rows(widget))
        [[[table tableColumns] objectAtIndex:0] setWidth:MAX(1, state->hex_row_width)];
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
            if (widget->kind == XXWIDGETS_EDIT) [field setDelegate:state->delegate];
            else [[field cell] setLineBreakMode:NSLineBreakByTruncatingTail];
            native = field;
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
            NSTableView *table = [[NSTableView alloc] initWithFrame:NSZeroRect];
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
            [table setAllowsMultipleSelection:NO];
            [table setAllowsEmptySelection:YES];
            [table setAllowsColumnReordering:NO];
            [table setColumnAutoresizingStyle:xxwidgets_formatted_rows(widget) ?
                NSTableViewNoColumnAutoresizing : NSTableViewLastColumnOnlyAutoresizingStyle];
            if (!xxwidgets_formatted_rows(widget)) [table setAutoresizingMask:NSViewWidthSizable];
            [table setRowHeight:((cocoa_app_state *)widget->app->platform)->cell_height];
            [table setDataSource:state->delegate];
            [table setDelegate:state->delegate];
            NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
            [scroll setBorderType:NSBezelBorder];
            [scroll setHasVerticalScroller:YES];
            [scroll setHasHorizontalScroller:xxwidgets_formatted_rows(widget)];
            [scroll setAutohidesScrollers:YES];
            [scroll setDocumentView:table];
            state->root = scroll;
            native = table;
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
            [tree setHeaderView:nil]; [tree setAllowsMultipleSelection:NO];
            [tree setAllowsEmptySelection:YES]; [tree setAllowsColumnReordering:NO];
            [tree setColumnAutoresizingStyle:NSTableViewNoColumnAutoresizing];
            [tree setRowHeight:((cocoa_app_state *)widget->app->platform)->cell_height];
            [tree setDataSource:state->delegate]; [tree setDelegate:state->delegate];
            [tree setTarget:state->delegate]; [tree setDoubleAction:@selector(treeActivated:)];
            NSScrollView *scroll = [[NSScrollView alloc] initWithFrame:NSZeroRect];
            if (!scroll) { [tree release]; cocoa_destroy_backend(widget); return XXWIDGETS_OUT_OF_MEMORY; }
            [scroll setBorderType:NSBezelBorder];
            [scroll setHasVerticalScroller:YES]; [scroll setHasHorizontalScroller:YES];
            [scroll setAutohidesScrollers:YES]; [scroll setDocumentView:tree];
            state->root = scroll; native = tree;
            break;
        }
        case XXWIDGETS_SCANRESULTS: {
            NSArray *titles = @[@"Type", @"Name", @"Version", @"Info"];
            const CGFloat widths[] = {100, 220, 100, 260};
            NSTableView *table = [[NSTableView alloc] initWithFrame:NSZeroRect];
            for (NSUInteger index = 0; index < [titles count]; ++index) {
                NSTableColumn *column = [[NSTableColumn alloc]
                    initWithIdentifier:[NSString stringWithFormat:@"%lu", (unsigned long)index]];
                [[column headerCell] setStringValue:[titles objectAtIndex:index]];
                [[column dataCell] setFont:font];
                [[column dataCell] setEditable:NO];
                [[column dataCell] setWraps:NO];
                [[column dataCell] setLineBreakMode:NSLineBreakByTruncatingTail];
                [column setWidth:widths[index]];
                [column setMinWidth:60];
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
        if (state->delegate) state->delegate->widget = NULL;
        if (widget->kind == XXWIDGETS_WINDOW) {
            [(NSWindow *)native setDelegate:nil];
            [(NSWindow *)native orderOut:nil];
            [(NSWindow *)native close];
        } else {
            if (widget->kind == XXWIDGETS_EDIT) [(NSTextField *)native setDelegate:nil];
            if (xxwidgets_list_kind(widget)) {
                [(NSTableView *)native setDelegate:nil];
                [(NSTableView *)native setDataSource:nil];
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
        NSString *text = [NSString stringWithUTF8String:widget->text];
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
        case XXWIDGETS_LABEL:
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
            if (refreshed) {
                if (xxwidgets_formatted_rows(widget) && widget->kind != XXWIDGETS_ARCHIVEBROWSER &&
                    widget->kind != XXWIDGETS_SCANRESULTS) {
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
            }
            if (widget->kind == XXWIDGETS_ARCHIVEBROWSER || widget->kind == XXWIDGETS_SCANRESULTS)
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

static xxwidgets_status cocoa_modal_owner(xxwidgets_widget *dialog, xxwidgets_widget *owner, int active)
{
    @autoreleasepool {
        NSWindow *window = dialog->native, *parent = owner->native;
        if (active) {
            NSRect area = [parent frame], bounds = [window frame];
            [window setFrameOrigin:NSMakePoint(NSMidX(area) - bounds.size.width / 2,
                                               NSMidY(area) - bounds.size.height / 2)];
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

const xxwidgets_backend_ops xxwidgets_native_ops = {
    "AppKit", cocoa_init_backend, cocoa_shutdown_backend, cocoa_poll_backend,
    cocoa_create_backend, cocoa_destroy_backend, cocoa_sync_backend,
    cocoa_read_text_backend, cocoa_read_value_backend, cocoa_focus_backend, cocoa_modal_owner, cocoa_about_content, cocoa_copy_text,
    cocoa_apply_fonts, NULL, cocoa_preview_font, NULL, NULL
};
