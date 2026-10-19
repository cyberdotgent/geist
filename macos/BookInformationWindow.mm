// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import "BookInformationWindow.h"

@interface GBInformationContentView : NSView
@end
@implementation GBInformationContentView
- (BOOL)isFlipped { return YES; }
@end

@interface GBBookInformationWindow () <NSWindowDelegate>
@end

@implementation GBBookInformationWindow {
  NSURL *_fileURL;
}

- (NSTextField *)text:(NSString *)value font:(NSFont *)font color:(NSColor *)color width:(CGFloat)width {
  NSTextField *field = [NSTextField wrappingLabelWithString:value];
  field.font = font;
  field.textColor = color;
  field.selectable = YES;
  field.preferredMaxLayoutWidth = width;
  [field.widthAnchor constraintLessThanOrEqualToConstant:width].active = YES;
  return field;
}

- (void)addSection:(NSString *)heading rows:(NSArray<NSArray<NSString *> *> *)rows
          toStack:(NSStackView *)body {
  if (!rows.count) return;
  NSStackView *section = [NSStackView stackViewWithViews:@[]];
  section.orientation = NSUserInterfaceLayoutOrientationVertical;
  section.alignment = NSLayoutAttributeLeading;
  section.spacing = 10;
  NSTextField *title = [NSTextField labelWithString:heading];
  title.font = [NSFont systemFontOfSize:13 weight:NSFontWeightSemibold];
  title.textColor = NSColor.secondaryLabelColor;
  [section addArrangedSubview:title];
  NSMutableArray<NSArray<NSView *> *> *views = [NSMutableArray array];
  for (NSArray<NSString *> *row in rows) {
    NSTextField *label = [NSTextField labelWithString:row[0]];
    label.textColor = NSColor.secondaryLabelColor;
    NSTextField *value = [self text:row[1] font:[NSFont systemFontOfSize:13]
        color:NSColor.labelColor width:390];
    [views addObject:@[label, value]];
  }
  NSGridView *grid = [NSGridView gridViewWithViews:views];
  grid.rowSpacing = 9;
  grid.columnSpacing = 14;
  [grid columnAtIndex:0].width = 140;
  [grid columnAtIndex:1].width = 390;
  [section addArrangedSubview:grid];
  [body addArrangedSubview:section];
}

- (instancetype)initWithInformation:(NSDictionary<NSString *, id> *)information {
  NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 640, 640)
      styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
      backing:NSBackingStoreBuffered defer:NO];
  if (!(self = [super initWithWindow:panel])) return nil;
  panel.title = @"Book Information";
  panel.delegate = self;
  _fileURL = information[@"fileURL"];

  NSStackView *body = [NSStackView stackViewWithViews:@[]];
  body.orientation = NSUserInterfaceLayoutOrientationVertical;
  body.alignment = NSLayoutAttributeLeading;
  body.spacing = 22;

  NSImageView *icon = [[NSImageView alloc] initWithFrame:NSMakeRect(0, 0, 64, 64)];
  icon.image = [NSWorkspace.sharedWorkspace iconForFile:_fileURL.path];
  icon.imageScaling = NSImageScaleProportionallyUpOrDown;
  [icon.widthAnchor constraintEqualToConstant:64].active = YES;
  [icon.heightAnchor constraintEqualToConstant:64].active = YES;
  NSString *titleText = information[@"title"] ?: information[@"shortTitle"] ?: _fileURL.lastPathComponent;
  NSTextField *bookTitle = [self text:titleText
      font:[NSFont systemFontOfSize:19 weight:NSFontWeightSemibold]
      color:NSColor.labelColor width:470];
  NSMutableArray<NSView *> *headerLines = [NSMutableArray arrayWithObject:bookTitle];
  NSString *shortTitle = information[@"shortTitle"];
  if (shortTitle.length && ![shortTitle isEqualToString:titleText])
    [headerLines addObject:[self text:shortTitle font:[NSFont systemFontOfSize:13]
        color:NSColor.secondaryLabelColor width:470]];
  [headerLines addObject:[self text:_fileURL.lastPathComponent
      font:[NSFont systemFontOfSize:12] color:NSColor.secondaryLabelColor width:470]];
  NSStackView *headText = [NSStackView stackViewWithViews:headerLines];
  headText.orientation = NSUserInterfaceLayoutOrientationVertical;
  headText.alignment = NSLayoutAttributeLeading;
  headText.spacing = 4;
  NSStackView *header = [NSStackView stackViewWithViews:@[icon, headText]];
  header.orientation = NSUserInterfaceLayoutOrientationHorizontal;
  header.alignment = NSLayoutAttributeCenterY;
  header.spacing = 16;
  [body addArrangedSubview:header];

  NSMutableArray<NSArray<NSString *> *> *publication = [NSMutableArray array];
  for (NSArray<NSString *> *entry in @[@[@"Author", @"authors"],
      @[@"Document number", @"documentNumber"], @[@"Date", @"date"],
      @[@"Language", @"language"], @[@"Version", @"version"],
      @[@"Build version", @"buildVersion"], @[@"Security", @"security"],
      @[@"Copyright", @"copyright"]]) {
    NSString *value = information[entry[1]];
    if (value.length) [publication addObject:@[entry[0], value]];
  }
  [self addSection:@"Publication" rows:publication toStack:body];

  NSNumberFormatter *numbers = [NSNumberFormatter new];
  numbers.numberStyle = NSNumberFormatterDecimalStyle;
  NSMutableArray<NSArray<NSString *> *> *contents = [NSMutableArray array];
  for (NSArray<NSString *> *entry in @[@[@"Topics", @"topicCount"],
      @[@"Stored objects", @"resourceCount"], @[@"Pages", @"pageCount"]]) {
    NSNumber *value = information[entry[1]];
    [contents addObject:@[entry[0], [numbers stringFromNumber:value] ?: @"0"]];
  }
  [self addSection:@"Contents" rows:contents toStack:body];

  NSNumber *size = information[@"fileSize"];
  NSString *sizeText = [NSByteCountFormatter stringFromByteCount:size.longLongValue
      countStyle:NSByteCountFormatterCountStyleFile];
  NSString *exactSize = [numbers stringFromNumber:size] ?: @"0";
  [self addSection:@"File" rows:@[
    @[@"Name", _fileURL.lastPathComponent],
    @[@"Size", [NSString stringWithFormat:@"%@ (%@ bytes)", sizeText, exactSize]],
    @[@"Location", _fileURL.URLByDeletingLastPathComponent.path]] toStack:body];

  CGFloat contentHeight = MAX(1, body.fittingSize.height);
  [panel setContentSize:NSMakeSize(640, contentHeight + 112)];
  GBInformationContentView *document = [[GBInformationContentView alloc]
      initWithFrame:NSMakeRect(24, 76, 592, contentHeight + 16)];
  body.frame = NSMakeRect(8, 8, 560, contentHeight);
  [document addSubview:body];
  [panel.contentView addSubview:document];

  NSButton *done = [NSButton buttonWithTitle:@"Done" target:self action:@selector(dismiss:)];
  done.keyEquivalent = @"\r";
  NSButton *reveal = [NSButton buttonWithTitle:@"Show in Finder" target:self
      action:@selector(showInFinder:)];
  done.translatesAutoresizingMaskIntoConstraints = NO;
  reveal.translatesAutoresizingMaskIntoConstraints = NO;
  [panel.contentView addSubview:done];
  [panel.contentView addSubview:reveal];
  [NSLayoutConstraint activateConstraints:@[
    [done.trailingAnchor constraintEqualToAnchor:panel.contentView.trailingAnchor constant:-24],
    [done.bottomAnchor constraintEqualToAnchor:panel.contentView.bottomAnchor constant:-20],
    [reveal.leadingAnchor constraintEqualToAnchor:panel.contentView.leadingAnchor constant:24],
    [reveal.centerYAnchor constraintEqualToAnchor:done.centerYAnchor]]];
  return self;
}

- (void)showForWindow:(NSWindow *)parent {
  [parent beginSheet:self.window completionHandler:nil];
}
- (IBAction)dismiss:(id)sender {
  (void)sender;
  [NSApp endSheet:self.window];
  [self.window orderOut:nil];
}
- (IBAction)showInFinder:(id)sender {
  (void)sender;
  [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[_fileURL]];
}
- (BOOL)windowShouldClose:(NSWindow *)sender {
  (void)sender;
  [self dismiss:nil];
  return NO;
}
@end
