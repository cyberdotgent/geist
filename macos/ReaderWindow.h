// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import <AppKit/AppKit.h>

@interface GBReaderWindow : NSWindowController
@property(nonatomic, readonly) BOOL hasBook;
- (void)openBookAtURL:(NSURL *)url;
- (IBAction)toggleSidebar:(id)sender;
- (IBAction)chooseBook:(id)sender;
- (IBAction)closeBook:(id)sender;
- (IBAction)goBack:(id)sender;
- (IBAction)goForward:(id)sender;
- (IBAction)previousTopic:(id)sender;
- (IBAction)nextTopic:(id)sender;
- (IBAction)showContents:(id)sender;
- (IBAction)findInTopic:(id)sender;
- (IBAction)printTopic:(id)sender;
- (IBAction)showBookInformation:(id)sender;
- (IBAction)largerTopicText:(id)sender;
- (IBAction)smallerTopicText:(id)sender;
@end

@protocol GBBookOpening <NSObject>
- (void)openDocumentAtURL:(NSURL *)url;
@end
