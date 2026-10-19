// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import "ReaderWindow.h"
#import "PreferencesWindow.h"

#include "geist/version.hpp"

#include <cstring>
#include <iostream>
#include <string>

@interface GBApplicationDelegate : NSObject <NSApplicationDelegate, GBBookOpening>
@property(nonatomic, strong) NSMutableArray<GBReaderWindow *> *readers;
@property(nonatomic, strong) NSMutableArray<NSURL *> *pendingURLs;
@property(nonatomic, strong) GBPreferencesWindow *preferences;
@property(nonatomic) BOOL ready;
@end

@implementation GBApplicationDelegate
- (instancetype)init {
  if ((self = [super init])) {
    _readers = [NSMutableArray array];
    _pendingURLs = [NSMutableArray array];
  }
  return self;
}
- (NSMenu *)menu:(NSString *)title main:(NSMenu *)main {
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:nil keyEquivalent:@""];
  [main addItem:item];
  NSMenu *menu = [[NSMenu alloc] initWithTitle:title];
  item.submenu = menu;
  return menu;
}
- (void)add:(NSString *)title action:(SEL)action key:(NSString *)key to:(NSMenu *)menu {
  NSMenuItem *item = [[NSMenuItem alloc] initWithTitle:title action:action keyEquivalent:key ?: @""];
  item.target = self;
  [menu addItem:item];
}
- (void)buildMenus {
  NSMenu *main = [NSMenu new];
  NSMenu *app = [self menu:@"Geist BOO Reader" main:main];
  [self add:@"About Geist BOO Reader" action:@selector(showAbout:) key:nil to:app];
  [app addItem:NSMenuItem.separatorItem];
  [self add:@"Settings…" action:@selector(showPreferences:) key:@"," to:app];
  [app addItem:NSMenuItem.separatorItem];
  [self add:@"Quit Geist BOO Reader" action:@selector(quit:) key:@"q" to:app];

  NSMenu *file = [self menu:@"File" main:main];
  [self add:@"New Window" action:@selector(newWindow:) key:@"n" to:file];
  [self add:@"Open Book…" action:@selector(chooseBook:) key:@"o" to:file];
  [self add:@"Close Window" action:@selector(closeWindow:) key:@"w" to:file];
  [self add:@"Close Book" action:@selector(closeBook:) key:nil to:file];
  [file addItem:NSMenuItem.separatorItem];
  [self add:@"Print Topic…" action:@selector(printTopic:) key:@"p" to:file];

  NSMenu *edit = [self menu:@"Edit" main:main];
  [edit addItem:[[NSMenuItem alloc] initWithTitle:@"Copy" action:@selector(copy:) keyEquivalent:@"c"]];
  [edit addItem:[[NSMenuItem alloc] initWithTitle:@"Select All" action:@selector(selectAll:) keyEquivalent:@"a"]];
  NSMenu *view = [self menu:@"View" main:main];
  [self add:@"Show or Hide Sidebar" action:@selector(toggleSidebar:) key:nil to:view];
  [self add:@"Larger Topic Text" action:@selector(largerTopicText:) key:@"+" to:view];
  [self add:@"Smaller Topic Text" action:@selector(smallerTopicText:) key:@"-" to:view];
  NSMenu *navigate = [self menu:@"Navigate" main:main];
  [self add:@"Table of Contents" action:@selector(showContents:) key:nil to:navigate];
  [self add:@"Back" action:@selector(goBack:) key:@"[" to:navigate];
  [self add:@"Forward" action:@selector(goForward:) key:@"]" to:navigate];
  [self add:@"Previous Topic" action:@selector(previousTopic:) key:nil to:navigate];
  [self add:@"Next Topic" action:@selector(nextTopic:) key:nil to:navigate];
  NSMenu *search = [self menu:@"Search" main:main];
  [self add:@"Search Book" action:@selector(findInTopic:) key:@"f" to:search];

  NSMenu *window = [self menu:@"Window" main:main];
  [window addItem:[[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"]];
  [window addItem:[[NSMenuItem alloc] initWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""]];
  [window addItem:NSMenuItem.separatorItem];
  [window addItem:[[NSMenuItem alloc] initWithTitle:@"Bring All to Front" action:@selector(arrangeInFront:) keyEquivalent:@""]];
  NSApp.windowsMenu = window;
  NSMenu *help = [self menu:@"Help" main:main];
  [self add:@"Book Information" action:@selector(showBookInformation:) key:nil to:help];
  NSApp.mainMenu = main;
}
- (GBReaderWindow *)frontReader {
  NSWindowController *controller = NSApp.keyWindow.windowController;
  return [controller isKindOfClass:GBReaderWindow.class] ? (GBReaderWindow *)controller : self.readers.lastObject;
}
- (GBReaderWindow *)makeReader {
  GBReaderWindow *reader = [GBReaderWindow new];
  [self.readers addObject:reader];
  [reader showWindow:nil];
  [reader.window makeKeyAndOrderFront:nil];
  return reader;
}
- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  [self buildMenus];
  self.ready = YES;
  [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(windowClosed:)
      name:NSWindowWillCloseNotification object:nil];
  if (!self.pendingURLs.count) [self makeReader];
  for (NSURL *url in self.pendingURLs) [self openDocumentAtURL:url];
  [self.pendingURLs removeAllObjects];
  [NSApp activateIgnoringOtherApps:YES];
}
- (void)windowClosed:(NSNotification *)note {
  for (GBReaderWindow *reader in [self.readers copy])
    if (reader.window == note.object) [self.readers removeObject:reader];
}
- (void)openDocumentAtURL:(NSURL *)url {
  GBReaderWindow *reader = nil;
  for (GBReaderWindow *candidate in self.readers) {
    if (!candidate.hasBook) { reader = candidate; break; }
  }
  if (!reader) reader = [self makeReader];
  [reader openBookAtURL:url];
  [reader.window makeKeyAndOrderFront:nil];
}
- (BOOL)application:(NSApplication *)sender openFile:(NSString *)filename {
  (void)sender;
  NSURL *url = [NSURL fileURLWithPath:filename];
  if (self.ready) [self openDocumentAtURL:url];
  else if (![self.pendingURLs containsObject:url]) [self.pendingURLs addObject:url];
  return YES;
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender { (void)sender; return NO; }
- (BOOL)applicationShouldHandleReopen:(NSApplication *)sender hasVisibleWindows:(BOOL)visible {
  (void)sender;
  if (!visible) [self makeReader];
  return YES;
}
- (IBAction)newWindow:(id)sender { (void)sender; [self makeReader]; }
- (IBAction)chooseBook:(id)sender {
  (void)sender;
  GBReaderWindow *reader = [self frontReader] ?: [self makeReader];
  [reader chooseBook:nil];
}
- (IBAction)closeWindow:(id)sender { (void)sender; [NSApp.keyWindow performClose:nil]; }
- (IBAction)closeBook:(id)sender { (void)sender; [[self frontReader] closeBook:nil]; }
- (IBAction)printTopic:(id)sender { (void)sender; [[self frontReader] printTopic:nil]; }
- (IBAction)toggleSidebar:(id)sender { (void)sender; [[self frontReader] toggleSidebar:nil]; }
- (IBAction)largerTopicText:(id)sender { (void)sender; [[self frontReader] largerTopicText:nil]; }
- (IBAction)smallerTopicText:(id)sender { (void)sender; [[self frontReader] smallerTopicText:nil]; }
- (IBAction)showContents:(id)sender { (void)sender; [[self frontReader] showContents:nil]; }
- (IBAction)goBack:(id)sender { (void)sender; [[self frontReader] goBack:nil]; }
- (IBAction)goForward:(id)sender { (void)sender; [[self frontReader] goForward:nil]; }
- (IBAction)previousTopic:(id)sender { (void)sender; [[self frontReader] previousTopic:nil]; }
- (IBAction)nextTopic:(id)sender { (void)sender; [[self frontReader] nextTopic:nil]; }
- (IBAction)findInTopic:(id)sender { (void)sender; [[self frontReader] findInTopic:nil]; }
- (IBAction)showBookInformation:(id)sender { (void)sender; [[self frontReader] showBookInformation:nil]; }
- (IBAction)showAbout:(id)sender {
  (void)sender;
  [NSApp orderFrontStandardAboutPanelWithOptions:@{
    NSAboutPanelOptionApplicationName:@"Geist BOO Reader",
    NSAboutPanelOptionVersion:@GEIST_READER_VERSION,
    NSAboutPanelOptionApplicationVersion:@GEIST_READER_VERSION}];
}
- (IBAction)showPreferences:(id)sender {
  (void)sender;
  if (!self.preferences) self.preferences = [GBPreferencesWindow new];
  [self.preferences showWindow:nil];
  [self.preferences.window makeKeyAndOrderFront:nil];
}
- (IBAction)quit:(id)sender { (void)sender; [NSApp terminate:nil]; }
@end

int main(int argc, const char *argv[]) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--version") != 0 && std::strcmp(argv[i], "-V") != 0) continue;
    std::cout << "geist-boo-reader/" << GEIST_READER_VERSION;
    if (std::strcmp(GEIST_READER_REVISION, "unknown") != 0)
      std::cout << " (" << GEIST_READER_REVISION << ')';
    std::cout << " libgeist/" << geist::library_version();
    if (std::string(geist::library_revision()) != "unknown")
      std::cout << " (" << geist::library_revision() << ')';
    std::cout << '\n';
    return 0;
  }
  @autoreleasepool {
    NSApplication *app = NSApplication.sharedApplication;
    app.activationPolicy = NSApplicationActivationPolicyRegular;
    GBApplicationDelegate *delegate = [GBApplicationDelegate new];
    app.delegate = delegate;
    if (argc > 1) [delegate.pendingURLs addObject:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]];
    [app run];
  }
  return 0;
}
