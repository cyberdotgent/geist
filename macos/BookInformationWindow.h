// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import <AppKit/AppKit.h>

@interface GBBookInformationWindow : NSWindowController
- (instancetype)initWithInformation:(NSDictionary<NSString *, id> *)information;
- (void)showForWindow:(NSWindow *)parent;
@end
