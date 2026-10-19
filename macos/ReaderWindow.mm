// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import "ReaderWindow.h"
#import "BookSource.h"

#import <WebKit/WebKit.h>

@interface GBTopicNode : NSObject
@property(nonatomic, copy) NSString *topicID;
@property(nonatomic, copy) NSString *label;
@property(nonatomic, copy) NSString *detail;
@property(nonatomic, strong) NSMutableArray<GBTopicNode *> *children;
@end
@implementation GBTopicNode
- (instancetype)init { if ((self = [super init])) _children = [NSMutableArray array]; return self; }
@end

@interface GBReaderWindow () <NSOutlineViewDataSource, NSOutlineViewDelegate, WKNavigationDelegate, NSToolbarDelegate, NSSearchFieldDelegate, NSToolbarItemValidation>
@end

@implementation GBReaderWindow {
  GBBookSource *_source;
  WKWebView *_web;
  NSOutlineView *_outline;
  NSSplitView *_split;
  GBTopicNode *_root;
  GBTopicNode *_tocRoot;
  NSMutableDictionary<NSString *, GBTopicNode *> *_nodes;
  NSArray<NSString *> *_order;
  NSString *_currentTopic;
  NSString *_lastSearch;
  NSSearchField *_search;
  NSMutableDictionary<NSString *, NSToolbarItem *> *_buttons;
  BOOL _syncing;
  NSUInteger _searchGeneration;
}

- (instancetype)init {
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1200, 820)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
      backing:NSBackingStoreBuffered defer:NO];
  if (!(self = [super initWithWindow:window])) return nil;
  window.title = @"Geist BOO Reader";
  window.minSize = NSMakeSize(800, 500);
  window.toolbarStyle = NSWindowToolbarStyleUnified;
  [window center];
  _source = [GBBookSource new];
  _nodes = [NSMutableDictionary dictionary];
  _buttons = [NSMutableDictionary dictionary];
  [self buildContent];
  [self buildToolbar];
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(applyFonts:)
      name:@"GBReaderPreferencesDidChange" object:nil];
  [self updateControls];
  return self;
}

- (void)buildContent {
  NSView *content = self.window.contentView;
  _split = [[NSSplitView alloc] initWithFrame:NSZeroRect];
  _split.vertical = YES;
  _split.dividerStyle = NSSplitViewDividerStyleThin;
  _split.translatesAutoresizingMaskIntoConstraints = NO;
  [content addSubview:_split];

  _outline = [[NSOutlineView alloc] initWithFrame:NSZeroRect];
  NSTableColumn *column = [[NSTableColumn alloc] initWithIdentifier:@"Topic"];
  column.title = @"Contents";
  column.width = 300;
  column.resizingMask = NSTableColumnAutoresizingMask;
  [_outline addTableColumn:column];
  _outline.outlineTableColumn = column;
  _outline.columnAutoresizingStyle = NSTableViewUniformColumnAutoresizingStyle;
  _outline.headerView = nil;
  _outline.dataSource = self; _outline.delegate = self;
  _outline.rowHeight = MAX(20, ([NSUserDefaults.standardUserDefaults integerForKey:@"ReaderTOCSize"] ?: 14) + 8);
  _outline.autoresizingMask = NSViewWidthSizable;
  _outline.target = self; _outline.action = @selector(selectTopic:);
  NSScrollView *tocScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, 300, 700)];
  tocScroll.documentView = _outline;
  tocScroll.hasVerticalScroller = YES;
  tocScroll.autohidesScrollers = YES;
  [_split addSubview:tocScroll];

  WKWebViewConfiguration *config = [WKWebViewConfiguration new];
  [config setURLSchemeHandler:_source forURLScheme:@"geist"];
  _web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 880, 700) configuration:config];
  _web.navigationDelegate = self;
  [_split addSubview:_web];
  [_split setHoldingPriority:250 forSubviewAtIndex:0];

  [NSLayoutConstraint activateConstraints:@[
    [_split.topAnchor constraintEqualToAnchor:content.topAnchor],
    [_split.leadingAnchor constraintEqualToAnchor:content.leadingAnchor],
    [_split.trailingAnchor constraintEqualToAnchor:content.trailingAnchor],
    [_split.bottomAnchor constraintEqualToAnchor:content.bottomAnchor]]];
  [self.window.contentView layoutSubtreeIfNeeded];
  [_split setPosition:300 ofDividerAtIndex:0];
  [self applyFonts:nil];
}

- (void)buildToolbar {
  NSToolbar *toolbar = [[NSToolbar alloc] initWithIdentifier:@"ReaderToolbar"];
  toolbar.delegate = self;
  toolbar.displayMode = NSToolbarDisplayModeIconOnly;
  toolbar.allowsUserCustomization = YES;
  self.window.toolbar = toolbar;
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarAllowedItemIdentifiers:(NSToolbar *)toolbar {
  (void)toolbar;
  return @[@"Sidebar", @"Open Book", @"Back", @"Forward", @"Previous Topic",
    @"Next Topic", @"Table of Contents", @"Search", @"Print Topic",
    @"Book Information", NSToolbarFlexibleSpaceItemIdentifier, NSToolbarSpaceItemIdentifier];
}
- (NSArray<NSToolbarItemIdentifier> *)toolbarDefaultItemIdentifiers:(NSToolbar *)toolbar {
  (void)toolbar;
  return @[@"Sidebar", @"Open Book", NSToolbarSpaceItemIdentifier,
    @"Back", @"Forward", @"Previous Topic", @"Next Topic",
    @"Table of Contents", NSToolbarFlexibleSpaceItemIdentifier,
    @"Search", @"Book Information", @"Print Topic"];
}
- (NSToolbarItem *)toolbar:(NSToolbar *)toolbar itemForItemIdentifier:(NSToolbarItemIdentifier)identifier
 willBeInsertedIntoToolbar:(BOOL)flag {
  (void)toolbar; (void)flag;
  NSToolbarItem *item = [[NSToolbarItem alloc] initWithItemIdentifier:identifier];
  if ([identifier isEqualToString:@"Search"]) {
    _search = [[NSSearchField alloc] initWithFrame:NSMakeRect(0, 0, 170, 26)];
    _search.placeholderString = @"Search Book";
    _search.delegate = self;
    _search.target = self; _search.action = @selector(searchFromField:);
    item.view = _search;
    [_search.widthAnchor constraintEqualToConstant:170].active = YES;
    item.label = @"Search"; item.paletteLabel = @"Search Book";
  } else {
    NSDictionary *symbols = @{@"Sidebar":@"sidebar.left", @"Open Book":@"folder",
      @"Back":@"chevron.left", @"Forward":@"chevron.right",
      @"Previous Topic":@"arrow.up", @"Next Topic":@"arrow.down",
      @"Table of Contents":@"list.bullet", @"Print Topic":@"printer",
      @"Book Information":@"info.circle"};
    NSDictionary *actions = @{@"Sidebar":NSStringFromSelector(@selector(toggleSidebar:)),
      @"Open Book":NSStringFromSelector(@selector(chooseBook:)),
      @"Back":NSStringFromSelector(@selector(goBack:)),
      @"Forward":NSStringFromSelector(@selector(goForward:)),
      @"Previous Topic":NSStringFromSelector(@selector(previousTopic:)),
      @"Next Topic":NSStringFromSelector(@selector(nextTopic:)),
      @"Table of Contents":NSStringFromSelector(@selector(showContents:)),
      @"Print Topic":NSStringFromSelector(@selector(printTopic:)),
      @"Book Information":NSStringFromSelector(@selector(showBookInformation:))};
    item.label = identifier; item.paletteLabel = identifier; item.toolTip = identifier;
    item.image = [NSImage imageWithSystemSymbolName:symbols[identifier] accessibilityDescription:identifier];
    item.target = self; item.action = NSSelectorFromString(actions[identifier]);
  }
  _buttons[identifier] = item;
  return item;
}
- (BOOL)validateToolbarItem:(NSToolbarItem *)item {
  NSString *name = item.itemIdentifier;
  if ([name isEqualToString:@"Back"]) return _web.canGoBack;
  if ([name isEqualToString:@"Forward"]) return _web.canGoForward;
  NSInteger index = [_order indexOfObject:_currentTopic];
  if ([name isEqualToString:@"Previous Topic"]) return _source.hasBook && index != NSNotFound && index > 0;
  if ([name isEqualToString:@"Next Topic"]) return _source.hasBook && index != NSNotFound && index + 1 < (NSInteger)_order.count;
  if ([name isEqualToString:@"Table of Contents"] || [name isEqualToString:@"Print Topic"] ||
      [name isEqualToString:@"Book Information"] || [name isEqualToString:@"Search"])
    return _source.hasBook;
  return YES;
}

- (IBAction)chooseBook:(id)sender {
  (void)sender;
  NSOpenPanel *panel = NSOpenPanel.openPanel;
  panel.title = @"Open Book";
  [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
    if (response == NSModalResponseOK)
      [(id<GBBookOpening>)NSApp.delegate openDocumentAtURL:panel.URL];
  }];
}
- (BOOL)hasBook { return _source.hasBook; }
- (IBAction)toggleSidebar:(id)sender {
  (void)sender;
  NSView *sidebar = _split.subviews.firstObject;
  sidebar.hidden = !sidebar.hidden;
  [_split adjustSubviews];
}

- (void)openBookAtURL:(NSURL *)url {
  NSError *error = nil;
  if (![_source openURL:url error:&error]) {
    NSAlert *alert = [NSAlert alertWithError:error];
    alert.messageText = @"Cannot Open Book";
    [alert beginSheetModalForWindow:self.window completionHandler:nil];
    return;
  }
  self.window.representedURL = url;
  self.window.title = _source.bookTitle.length ? _source.bookTitle : url.lastPathComponent;
  _root = [GBTopicNode new];
  _root.label = _source.bookTitle.length ? _source.bookTitle : @"Book";
  _tocRoot = _root;
  ++_searchGeneration;
  _search.stringValue = @"";
  _outline.rowHeight = MAX(20, ([NSUserDefaults.standardUserDefaults integerForKey:@"ReaderTOCSize"] ?: 14) + 8);
  [_nodes removeAllObjects];
  NSMutableArray<GBTopicNode *> *parents = [NSMutableArray array];
  NSMutableArray<NSString *> *order = [NSMutableArray array];
  for (NSDictionary *row in _source.contents) {
    GBTopicNode *node = [GBTopicNode new];
    node.topicID = row[@"id"];
    node.label = node.topicID.length ? [NSString stringWithFormat:@"%@ %@", node.topicID, row[@"title"]] : row[@"title"];
    NSUInteger level = [row[@"level"] unsignedIntegerValue];
    if (level == 0 || parents.count == 0) {
      [_root.children addObject:node]; [parents removeAllObjects]; [parents addObject:node];
    } else {
      NSUInteger slot = MIN(level, parents.count);
      [parents[slot - 1].children addObject:node];
      while (parents.count > slot) [parents removeLastObject];
      [parents addObject:node];
    }
    _nodes[node.topicID] = node;
    [order addObject:node.topicID];
  }
  _order = order;
  _currentTopic = nil;
  [_outline reloadData];
  [_outline expandItem:_root];
  if (_order.count) [self navigateTo:_order[0]];
  [self updateControls];
}

- (IBAction)closeBook:(id)sender {
  (void)sender;
  [_web loadHTMLString:@"" baseURL:nil];
  [_source closeBook];
  _root = nil; _order = @[]; _currentTopic = nil;
  _tocRoot = nil;
  ++_searchGeneration;
  _search.stringValue = @"";
  [_nodes removeAllObjects]; [_outline reloadData];
  self.window.title = @"Geist BOO Reader";
  self.window.representedURL = nil;
  [self updateControls];
}

- (void)navigateTo:(NSString *)topicID { if (topicID.length) [_web loadRequest:[NSURLRequest requestWithURL:[_source topicURL:topicID]]]; }
- (void)selectTopic:(id)sender {
  (void)sender;
  if (_syncing) return;
  GBTopicNode *node = [_outline itemAtRow:_outline.selectedRow];
  if (node.topicID.length) [self navigateTo:node.topicID];
}
- (NSInteger)outlineView:(NSOutlineView *)view numberOfChildrenOfItem:(id)item {
  (void)view; return item ? ((GBTopicNode *)item).children.count : (_root ? 1 : 0);
}
- (id)outlineView:(NSOutlineView *)view child:(NSInteger)index ofItem:(id)item {
  (void)view; return item ? ((GBTopicNode *)item).children[index] : _root;
}
- (BOOL)outlineView:(NSOutlineView *)view isItemExpandable:(id)item {
  (void)view; return ((GBTopicNode *)item).children.count > 0;
}
- (NSView *)outlineView:(NSOutlineView *)view viewForTableColumn:(NSTableColumn *)column item:(id)item {
  (void)column;
  NSTextField *field = [view makeViewWithIdentifier:@"TopicCell" owner:self];
  if (!field) { field = [NSTextField labelWithString:@""]; field.identifier = @"TopicCell"; }
  field.stringValue = ((GBTopicNode *)item).label ?: @"";
  field.toolTip = ((GBTopicNode *)item).topicID;
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSString *family = [defaults stringForKey:@"ReaderFontFamily"] ?: @"Helvetica";
  NSInteger size = [defaults integerForKey:@"ReaderTOCSize"] ?: 14;
  field.font = [NSFont fontWithName:family size:size] ?: [NSFont systemFontOfSize:size];
  GBTopicNode *node = item;
  if (node.detail.length) {
    NSMutableAttributedString *text = [[NSMutableAttributedString alloc] initWithString:
      [NSString stringWithFormat:@"%@\n%@", node.label, node.detail]];
    [text addAttributes:@{NSFontAttributeName:[NSFont systemFontOfSize:12],
      NSForegroundColorAttributeName:NSColor.secondaryLabelColor}
      range:NSMakeRange(node.label.length + 1, node.detail.length)];
    field.attributedStringValue = text;
    field.maximumNumberOfLines = 2;
    field.toolTip = node.detail;
  } else field.maximumNumberOfLines = 1;
  return field;
}
- (void)webView:(WKWebView *)webView didCommitNavigation:(WKNavigation *)navigation {
  (void)navigation;
  NSURL *url = webView.URL;
  NSString *path = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO].percentEncodedPath;
  if ([url.scheme isEqualToString:@"geist"] && [url.host isEqualToString:@"book"] &&
      [path hasPrefix:@"/topic/"]) {
    _currentTopic = [[path substringFromIndex:7] stringByRemovingPercentEncoding];
    GBTopicNode *node = _nodes[_currentTopic];
    if (node) {
      _syncing = YES;
      NSInteger row = [_outline rowForItem:node];
      if (row >= 0) [_outline selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
      _syncing = NO;
    }
  }
  [self updateControls];
}
- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
  (void)webView; (void)navigation; [self applyTopicFont]; [self updateControls];
}
- (void)webView:(WKWebView *)webView decidePolicyForNavigationAction:(WKNavigationAction *)action
 decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
  (void)webView;
  NSString *scheme = action.request.URL.scheme.lowercaseString;
  if ([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"]) {
    [NSWorkspace.sharedWorkspace openURL:action.request.URL];
    decisionHandler(WKNavigationActionPolicyCancel);
  } else decisionHandler(WKNavigationActionPolicyAllow);
}

- (IBAction)goBack:(id)sender { (void)sender; [_web goBack]; }
- (IBAction)goForward:(id)sender { (void)sender; [_web goForward]; }
- (void)goRelative:(NSInteger)delta {
  NSInteger index = [_order indexOfObject:_currentTopic];
  NSInteger next = index + delta;
  if (index != NSNotFound && next >= 0 && next < (NSInteger)_order.count) [self navigateTo:_order[next]];
}
- (IBAction)previousTopic:(id)sender { (void)sender; [self goRelative:-1]; }
- (IBAction)nextTopic:(id)sender { (void)sender; [self goRelative:1]; }
- (IBAction)showContents:(id)sender {
  (void)sender;
  [self restoreContents];
  if (_order.count) [self navigateTo:_order[0]];
  [self.window makeFirstResponder:_outline];
}
- (IBAction)findInTopic:(id)sender { (void)sender; [self.window makeFirstResponder:_search]; }
- (void)restoreContents {
  ++_searchGeneration;
  _root = _tocRoot;
  _outline.rowHeight = MAX(20, ([NSUserDefaults.standardUserDefaults integerForKey:@"ReaderTOCSize"] ?: 14) + 8);
  [_outline reloadData];
  [_outline expandItem:_root];
  GBTopicNode *node = _nodes[_currentTopic];
  NSInteger row = [_outline rowForItem:node];
  if (row >= 0) [_outline selectRowIndexes:[NSIndexSet indexSetWithIndex:row] byExtendingSelection:NO];
}
- (void)controlTextDidChange:(NSNotification *)note {
  if (note.object == _search && !_search.stringValue.length) [self restoreContents];
}
- (IBAction)searchFromField:(id)sender {
  (void)sender;
  _lastSearch = _search.stringValue;
  if (!_lastSearch.length) { [self restoreContents]; return; }
  NSUInteger generation = ++_searchGeneration;
  NSString *query = [_lastSearch copy];
  dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
    NSArray *results = [self->_source searchForText:query];
    dispatch_async(dispatch_get_main_queue(), ^{
      if (generation != self->_searchGeneration || !self->_source.hasBook) return;
      GBTopicNode *root = [GBTopicNode new];
      root.label = [NSString stringWithFormat:@"Search Results (%lu)", (unsigned long)results.count];
      for (NSDictionary *result in results) {
        GBTopicNode *node = [GBTopicNode new];
        node.topicID = result[@"id"];
        node.label = [NSString stringWithFormat:@"%@ %@", node.topicID, result[@"title"]];
        node.detail = result[@"snippet"];
        [root.children addObject:node];
      }
      self->_root = root;
      self->_outline.rowHeight = 42;
      [self->_outline reloadData];
      [self->_outline expandItem:root];
    });
  });
}
- (IBAction)printTopic:(id)sender {
  (void)sender;
  if (!_source.hasBook) return;
  NSPrintOperation *operation = [_web printOperationWithPrintInfo:NSPrintInfo.sharedPrintInfo];
  [operation runOperationModalForWindow:self.window delegate:nil didRunSelector:NULL contextInfo:NULL];
}
- (IBAction)showBookInformation:(id)sender {
  (void)sender;
  if (!_source.hasBook) return;
  NSAlert *alert = [NSAlert new];
  alert.messageText = @"Book Information";
  alert.informativeText = _source.information;
  [alert beginSheetModalForWindow:self.window completionHandler:nil];
}
- (IBAction)applyFonts:(id)sender {
  (void)sender;
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSInteger tocSize = [defaults integerForKey:@"ReaderTOCSize"] ?: 14;
  [_outline reloadData];
  [self applyTopicFont];
  _outline.rowHeight = _root != _tocRoot ? 42 : MAX(20, tocSize + 8);
}
- (void)applyTopicFont {
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSString *family = [defaults stringForKey:@"ReaderFontFamily"] ?: @"Helvetica";
  NSInteger size = [defaults integerForKey:@"ReaderTopicSize"] ?: 14;
  NSData *jsonData = [NSJSONSerialization dataWithJSONObject:@{ @"family":family }
    options:NSJSONWritingFragmentsAllowed error:nil];
  NSString *json = [[NSString alloc] initWithData:jsonData encoding:NSUTF8StringEncoding];
  NSString *script = [NSString stringWithFormat:
      @"document.body.style.fontFamily = (%@).family; document.body.style.fontSize = '%ldpx';", json, (long)size];
  [_web evaluateJavaScript:script completionHandler:nil];
}
- (IBAction)largerTopicText:(id)sender { (void)sender; [self stepTopicSize:1]; }
- (IBAction)smallerTopicText:(id)sender { (void)sender; [self stepTopicSize:-1]; }
- (void)stepTopicSize:(NSInteger)delta {
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  NSInteger size = [defaults integerForKey:@"ReaderTopicSize"] ?: 14;
  [defaults setInteger:MAX(8, MIN(32, size + delta)) forKey:@"ReaderTopicSize"];
  [NSNotificationCenter.defaultCenter postNotificationName:@"GBReaderPreferencesDidChange" object:nil];
}
- (void)updateControls {
  BOOL loaded = _source.hasBook;
  NSInteger index = [_order indexOfObject:_currentTopic];
  _buttons[@"Back"].enabled = _web.canGoBack;
  _buttons[@"Forward"].enabled = _web.canGoForward;
  _buttons[@"Previous Topic"].enabled = loaded && index != NSNotFound && index > 0;
  _buttons[@"Next Topic"].enabled = loaded && index != NSNotFound && index + 1 < (NSInteger)_order.count;
  for (NSString *key in @[@"Table of Contents", @"Search", @"Print Topic", @"Book Information"])
    _buttons[key].enabled = loaded;
  _search.enabled = loaded;
}
@end
