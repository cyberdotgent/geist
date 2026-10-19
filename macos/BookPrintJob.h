// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import <AppKit/AppKit.h>

@class GBBookSource;
@class PDFDocument;

// Renders each BOO topic through the same WebKit source as the reader, then
// assembles a paginated book with a contents list based on the actual pages.
@interface GBBookPrintJob : NSObject
- (instancetype)initWithSource:(GBBookSource *)source printInfo:(NSPrintInfo *)printInfo;
- (void)startWithCompletion:(void (^)(PDFDocument *document, NSError *error))completion;
- (void)cancel;
@end
