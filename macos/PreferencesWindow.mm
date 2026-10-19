// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import "PreferencesWindow.h"

@implementation GBPreferencesWindow {
  NSPopUpButton *_family, *_tocSize, *_topicSize;
}

- (instancetype)init {
  NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 450, 210)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
      backing:NSBackingStoreBuffered defer:NO];
  if (!(self = [super initWithWindow:window])) return nil;
  window.title = @"Geist BOO Reader Settings";
  [window center];
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  _family = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (NSString *name in NSFontManager.sharedFontManager.availableFontFamilies)
    [_family addItemWithTitle:name];
  [_family selectItemWithTitle:[defaults stringForKey:@"ReaderFontFamily"] ?: @"Helvetica"];
  _tocSize = [self sizeMenu:[defaults integerForKey:@"ReaderTOCSize"] ?: 14];
  _topicSize = [self sizeMenu:[defaults integerForKey:@"ReaderTopicSize"] ?: 14];
  NSGridView *grid = [NSGridView gridViewWithViews:@[
    @[[NSTextField labelWithString:@"Font family:"], _family],
    @[[NSTextField labelWithString:@"Contents size:"], _tocSize],
    @[[NSTextField labelWithString:@"Topic size:"], _topicSize]]];
  grid.rowSpacing = 12; grid.columnSpacing = 16;
  grid.translatesAutoresizingMaskIntoConstraints = NO;
  [window.contentView addSubview:grid];
  [NSLayoutConstraint activateConstraints:@[
    [grid.leadingAnchor constraintEqualToAnchor:window.contentView.leadingAnchor constant:24],
    [grid.trailingAnchor constraintEqualToAnchor:window.contentView.trailingAnchor constant:-24],
    [grid.centerYAnchor constraintEqualToAnchor:window.contentView.centerYAnchor]]];
  for (NSPopUpButton *box in @[_family, _tocSize, _topicSize]) {
    box.target = self; box.action = @selector(savePreferences:);
  }
  return self;
}

- (NSPopUpButton *)sizeMenu:(NSInteger)selected {
  NSPopUpButton *box = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
  for (NSNumber *size in @[@8,@9,@10,@11,@12,@14,@16,@18,@20,@24,@28,@32])
    [box addItemWithTitle:size.stringValue];
  [box selectItemWithTitle:[NSString stringWithFormat:@"%ld", (long)selected]];
  return box;
}

- (IBAction)savePreferences:(id)sender {
  (void)sender;
  NSUserDefaults *defaults = NSUserDefaults.standardUserDefaults;
  [defaults setObject:_family.titleOfSelectedItem forKey:@"ReaderFontFamily"];
  [defaults setInteger:_tocSize.titleOfSelectedItem.integerValue forKey:@"ReaderTOCSize"];
  [defaults setInteger:_topicSize.titleOfSelectedItem.integerValue forKey:@"ReaderTopicSize"];
  [NSNotificationCenter.defaultCenter postNotificationName:@"GBReaderPreferencesDidChange" object:nil];
}
@end
