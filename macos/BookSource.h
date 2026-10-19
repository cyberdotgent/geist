// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import <Foundation/Foundation.h>
#import <WebKit/WebKit.h>

@interface GBBookSource : NSObject <WKURLSchemeHandler>
@property(nonatomic, readonly) BOOL hasBook;
@property(nonatomic, readonly, copy) NSString *bookTitle;
@property(nonatomic, readonly, copy) NSArray<NSDictionary<NSString *, id> *> *contents;
@property(nonatomic, readonly, copy) NSString *information;
- (BOOL)openURL:(NSURL *)url error:(NSError **)error;
- (void)closeBook;
- (NSURL *)topicURL:(NSString *)topicID;
- (NSArray<NSDictionary<NSString *, NSString *> *> *)searchForText:(NSString *)query;
@end
