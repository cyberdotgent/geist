// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import "BookPrintJob.h"
#import "BookSource.h"

#import <PDFKit/PDFKit.h>
#import <WebKit/WebKit.h>

namespace {
NSString *const GBPrintErrorDomain = @"gent.cyber.geist-viewer.print";

BOOL isTopic(NSDictionary *row, NSString *identifier) {
  return [row[@"id"] caseInsensitiveCompare:identifier] == NSOrderedSame;
}

NSError *printError(NSString *message) {
  return [NSError errorWithDomain:GBPrintErrorDomain code:1
      userInfo:@{NSLocalizedDescriptionKey:message}];
}

NSDictionary *textStyle(CGFloat size, NSFontWeight weight) {
  return @{NSFontAttributeName:[NSFont systemFontOfSize:size weight:weight],
           NSForegroundColorAttributeName:NSColor.textColor};
}

CGFloat rowHeight(NSDictionary *row, CGFloat width) {
  CGFloat indent = MIN([row[@"level"] integerValue], 4) * 16;
  NSString *title = row[@"title"];
  if (![title isKindOfClass:NSString.class] || !title.length) title = row[@"id"];
  NSRect bounds = [title boundingRectWithSize:NSMakeSize(width - indent - 45, CGFLOAT_MAX)
      options:NSStringDrawingUsesLineFragmentOrigin attributes:textStyle(10.5, NSFontWeightRegular)];
  return MAX(19, ceil(bounds.size.height) + 5);
}

NSUInteger contentsPages(NSArray<NSDictionary *> *rows, NSSize paper) {
  CGFloat width = paper.width - 144;
  CGFloat y = paper.height - 112;
  NSUInteger pages = 1;
  for (NSDictionary *row in rows) {
    CGFloat height = rowHeight(row, width);
    if (y - height < 84) { ++pages; y = paper.height - 112; }
    y -= height;
  }
  return pages;
}

void drawString(NSString *value, NSRect rect, NSDictionary *style) {
  [value drawInRect:rect withAttributes:style];
}

PDFDocument *documentFromDrawing(void (^draw)(CGContextRef, NSSize), NSSize paper) {
  NSMutableData *data = [NSMutableData data];
  CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData((__bridge CFMutableDataRef)data);
  CGRect box = CGRectMake(0, 0, paper.width, paper.height);
  CGContextRef context = CGPDFContextCreate(consumer, &box, nullptr);
  draw(context, paper);
  CGPDFContextClose(context);
  CGContextRelease(context);
  CGDataConsumerRelease(consumer);
  return [[PDFDocument alloc] initWithData:data];
}

void beginPage(CGContextRef context, NSSize paper) {
  (void)paper;
  CGPDFContextBeginPage(context, nullptr);
  [NSGraphicsContext saveGraphicsState];
  NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context flipped:NO];
}

void endPage(CGContextRef context) {
  [NSGraphicsContext restoreGraphicsState];
  CGPDFContextEndPage(context);
}

PDFDocument *titlePage(NSDictionary *info, NSSize paper) {
  return documentFromDrawing(^(CGContextRef context, NSSize size) {
    beginPage(context, size);
    NSString *title = info[@"title"] ?: info[@"shortTitle"] ?: @"Untitled Book";
    drawString(title, NSMakeRect(72, size.height * .48, size.width - 144, size.height * .26),
        textStyle(28, NSFontWeightBold));
    NSString *authors = info[@"authors"] ?: @"";
    drawString(authors, NSMakeRect(72, size.height * .39, size.width - 144, 54),
        textStyle(14, NSFontWeightRegular));
    NSString *number = info[@"documentNumber"] ?: @"";
    drawString(number, NSMakeRect(72, 98, size.width - 144, 30),
        textStyle(11, NSFontWeightRegular));
    endPage(context);
  }, paper);
}

PDFDocument *contentsDocument(NSArray<NSDictionary *> *rows,
                              NSDictionary<NSString *, NSNumber *> *pageNumbers,
                              NSSize paper) {
  return documentFromDrawing(^(CGContextRef context, NSSize size) {
    CGFloat width = size.width - 144;
    CGFloat y = 0;
    NSUInteger page = 0;
    void (^start)(void) = ^{
      beginPage(context, size);
      NSString *heading = page ? @"Contents (continued)" : @"Contents";
      drawString(heading, NSMakeRect(72, size.height - 85, width, 38),
          textStyle(22, NSFontWeightBold));
      drawString(@"Page", NSMakeRect(size.width - 117, size.height - 111, 45, 20),
          textStyle(10, NSFontWeightSemibold));
    };
    start();
    y = size.height - 112;
    for (NSDictionary *row in rows) {
      CGFloat indent = MIN([row[@"level"] integerValue], 4) * 16;
      CGFloat height = rowHeight(row, width);
      if (y - height < 84) {
        endPage(context);
        ++page;
        start();
        y = size.height - 112;
      }
      y -= height;
      NSString *title = row[@"title"];
      if (![title isKindOfClass:NSString.class] || !title.length) title = row[@"id"];
      drawString(title, NSMakeRect(72 + indent, y, width - indent - 45, height),
          textStyle(10.5, NSFontWeightRegular));
      NSString *number = pageNumbers[row[@"id"]].stringValue ?: @"";
      NSMutableParagraphStyle *right = [NSMutableParagraphStyle new];
      right.alignment = NSTextAlignmentRight;
      drawString(number, NSMakeRect(size.width - 112, y, 40, height),
          @{NSFontAttributeName:[NSFont monospacedDigitSystemFontOfSize:10.5
              weight:NSFontWeightRegular], NSParagraphStyleAttributeName:right});
    }
    endPage(context);
  }, paper);
}

void addPageNumber(PDFPage *page, NSUInteger number) {
  NSRect box = [page boundsForBox:kPDFDisplayBoxMediaBox];
  PDFAnnotation *label = [[PDFAnnotation alloc] initWithBounds:
      NSMakeRect(box.size.width - 114, 31, 42, 18)
      forType:PDFAnnotationSubtypeFreeText withProperties:nil];
  label.contents = [NSString stringWithFormat:@"%lu", (unsigned long)number];
  label.font = [NSFont monospacedDigitSystemFontOfSize:10 weight:NSFontWeightRegular];
  label.fontColor = NSColor.darkGrayColor;
  label.alignment = NSTextAlignmentRight;
  label.color = NSColor.clearColor;
  label.shouldPrint = YES;
  [page addAnnotation:label];
}

PDFDocument *paginatePDF(NSData *data, NSDictionary<NSString *, NSArray<NSNumber *> *> *breaks,
                          NSSize paper) {
  if (!data.length || paper.width < 300 || paper.height < 300) return nil;
  CGDataProviderRef provider = CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
  CGPDFDocumentRef source = CGPDFDocumentCreateWithProvider(provider);
  CGDataProviderRelease(provider);
  if (!source || !CGPDFDocumentGetNumberOfPages(source)) {
    if (source) CGPDFDocumentRelease(source);
    return nil;
  }
  CGRect firstBox = CGPDFPageGetBoxRect(CGPDFDocumentGetPage(source, 1), kCGPDFMediaBox);
  if (firstBox.size.width < 1 || firstBox.size.height < 1) {
    CGPDFDocumentRelease(source);
    return nil;
  }
  NSSize pageSize = firstBox.size.width > paper.width - 126
      ? NSMakeSize(paper.height, paper.width) : paper;
  CGFloat scale = MIN(1.0, (pageSize.width - 128) / firstBox.size.width);
  CGFloat available = (pageSize.height - 132) / scale;
  NSMutableData *output = [NSMutableData data];
  CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData((__bridge CFMutableDataRef)output);
  CGRect targetBox = CGRectMake(0, 0, pageSize.width, pageSize.height);
  CGContextRef context = CGPDFContextCreate(consumer, &targetBox, nullptr);
  CGFloat offset = 0;
  for (size_t sourceIndex = 1; sourceIndex <= CGPDFDocumentGetNumberOfPages(source);
       ++sourceIndex) {
    CGPDFPageRef sourcePage = CGPDFDocumentGetPage(source, sourceIndex);
    CGRect sourceBox = CGPDFPageGetBoxRect(sourcePage, kCGPDFMediaBox);
    CGFloat start = 0;
    while (start < sourceBox.size.height - 1) {
      CGFloat end = MIN(start + available, sourceBox.size.height);
      if (end < sourceBox.size.height) {
        CGFloat best = 0;
        for (NSNumber *candidate in breaks[@"blocks"]) {
          CGFloat position = candidate.doubleValue - offset;
          if (position > start + available * .8 && position < end - 8)
            best = MAX(best, position);
        }
        if (!best) for (NSNumber *candidate in breaks[@"lines"]) {
          CGFloat position = candidate.doubleValue - offset;
          if (position > start + available * .6 && position < end - 8)
            best = MAX(best, position);
        }
        if (best) end = best;
      }
      CGPDFContextBeginPage(context, nullptr);
      CGContextSaveGState(context);
      CGFloat slice = end - start;
      CGContextClipToRect(context, CGRectMake(64, pageSize.height - 62 - slice * scale,
          sourceBox.size.width * scale, slice * scale));
      CGContextTranslateCTM(context, 64,
          pageSize.height - 62 - (sourceBox.size.height - start) * scale);
      CGContextScaleCTM(context, scale, scale);
      CGContextDrawPDFPage(context, sourcePage);
      CGContextRestoreGState(context);
      CGPDFContextEndPage(context);
      start = end;
    }
    offset += sourceBox.size.height;
  }
  CGPDFContextClose(context);
  CGContextRelease(context);
  CGDataConsumerRelease(consumer);
  CGPDFDocumentRelease(source);
  return [[PDFDocument alloc] initWithData:output];
}
} // namespace

@interface GBBookPrintJob () <WKNavigationDelegate>
@end

@implementation GBBookPrintJob {
  GBBookSource *_source;
  NSPrintInfo *_printInfo;
  WKWebView *_web;
  NSArray<NSDictionary *> *_topics;
  NSMutableDictionary<NSString *, PDFDocument *> *_rendered;
  NSUInteger _index;
  BOOL _cancelled;
  void (^_completion)(PDFDocument *, NSError *);
}

- (instancetype)initWithSource:(GBBookSource *)source printInfo:(NSPrintInfo *)printInfo {
  if ((self = [super init])) {
    _source = source;
    _printInfo = [printInfo copy];
    _rendered = [NSMutableDictionary dictionary];
  }
  return self;
}

- (void)startWithCompletion:(void (^)(PDFDocument *, NSError *))completion {
  _completion = [completion copy];
  if (!_source.hasBook) { [self finish:nil error:printError(@"No book is open.")]; return; }
  _topics = _source.contents;
  if (!_topics.count) { [self assemble]; return; }
  WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
  [configuration setURLSchemeHandler:_source forURLScheme:@"geist"];
  CGFloat width = MAX(320, _printInfo.paperSize.width - 128);
  _web = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, width, 700)
      configuration:configuration];
  _web.navigationDelegate = self;
  [self loadNext];
}

- (void)cancel {
  _cancelled = YES;
  [_web stopLoading];
  [self finish:nil error:nil];
}

- (void)loadNext {
  if (!_completion) return;
  if (_index == _topics.count) { [self assemble]; return; }
  NSDictionary *row = _topics[_index];
  if (isTopic(row, @"CONTENTS")) { ++_index; [self loadNext]; return; }
  [_web loadRequest:[NSURLRequest requestWithURL:[_source topicURL:row[@"id"]]]];
}

- (void)webView:(WKWebView *)webView didFinishNavigation:(WKNavigation *)navigation {
  (void)webView; (void)navigation;
  if (!_completion || _cancelled) return;
  // Allow WebKit's resource loads and layout to settle before pagination.
  [_web evaluateJavaScript:@"document.fonts.ready.then(() => Promise.all(Array.from(document.images).map(i => i.decode().catch(() => {}))))"
      completionHandler:^(id value, NSError *error) {
    (void)value; (void)error;
    if (!self->_completion || self->_cancelled) return;
    [self->_web evaluateJavaScript:
        @"(() => { const root = document.querySelector('.geist-topic');"
         "const uniq = a => [...new Set(a)].sort((x,y) => x-y);"
         "const blocks = [...root.querySelectorAll('*')]"
         ".filter(e => /^(block|table|table-row|list-item|pre)$/.test(getComputedStyle(e).display))"
         ".map(e => Math.round(e.getBoundingClientRect().top + scrollY));"
         "const lines = []; const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);"
         "for (let node; node = walker.nextNode();) { if (!node.textContent.trim()) continue;"
         "const range = document.createRange(); range.selectNodeContents(node);"
         "for (const rect of range.getClientRects()) if (rect.height)"
         "lines.push(Math.round(rect.top + scrollY)); }"
         "for (const pre of root.querySelectorAll('pre')) {"
         "const text = document.createTreeWalker(pre, NodeFilter.SHOW_TEXT);"
         "for (let node; node = text.nextNode();) { const value = node.textContent;"
         "for (let i = 0; i + 1 < value.length; ++i) if (value[i] === '\\n') {"
         "const range = document.createRange(); range.setStart(node, i + 1);"
         "range.setEnd(node, i + 2); const rect = range.getBoundingClientRect();"
         "if (rect.height) lines.push(Math.round(rect.top + scrollY)); } } }"
         "return {blocks:uniq(blocks), lines:uniq(lines)}; })()"
        completionHandler:^(id positions, NSError *layoutError) {
      if (layoutError) { [self finish:nil error:layoutError]; return; }
      [self renderCurrentTopicAtBreaks:positions];
    }];
  }];
}

- (void)webView:(WKWebView *)webView didFailNavigation:(WKNavigation *)navigation
      withError:(NSError *)error {
  (void)webView; (void)navigation;
  [self finish:nil error:error];
}

- (void)webView:(WKWebView *)webView didFailProvisionalNavigation:(WKNavigation *)navigation
      withError:(NSError *)error {
  (void)webView; (void)navigation;
  [self finish:nil error:error];
}

- (void)renderCurrentTopicAtBreaks:(NSDictionary<NSString *, NSArray<NSNumber *> *> *)breaks {
  NSDictionary *row = _topics[_index];
  [_web createPDFWithConfiguration:[WKPDFConfiguration new]
      completionHandler:^(NSData *data, NSError *error) {
    if (!self->_completion || self->_cancelled) return;
    PDFDocument *document = error ? nil : paginatePDF(data, breaks,
        self->_printInfo.paperSize);
    if (!document || !document.pageCount) {
      [self finish:nil error:error ?: printError([NSString stringWithFormat:
          @"Could not paginate %@.", row[@"title"] ?: row[@"id"]])];
      return;
    }
    self->_rendered[row[@"id"]] = document;
    ++self->_index;
    dispatch_async(dispatch_get_main_queue(), ^{ [self loadNext]; });
  }];
}

- (void)assemble {
  NSSize paper = _printInfo.paperSize;
  NSMutableArray<NSDictionary *> *ordered = [NSMutableArray array];
  NSMutableArray<NSDictionary *> *titleTopics = [NSMutableArray array];
  for (NSString *identifier in @[@"COVER", @"TITLE"])
    for (NSDictionary *row in _topics)
      if (isTopic(row, identifier)) [titleTopics addObject:row];
  [ordered addObjectsFromArray:titleTopics];
  NSUInteger contentsIndex = NSNotFound;
  for (NSDictionary *row in _topics) {
    if (isTopic(row, @"COVER") || isTopic(row, @"TITLE")) continue;
    if (isTopic(row, @"CONTENTS")) {
      if (contentsIndex == NSNotFound) contentsIndex = ordered.count;
      continue;
    }
    [ordered addObject:row];
  }
  if (contentsIndex == NSNotFound) {
    contentsIndex = titleTopics.count;
    while (contentsIndex < ordered.count) {
      NSString *identifier = [ordered[contentsIndex][@"id"] uppercaseString];
      BOOL frontMatter = [identifier hasPrefix:@"NOTICES"] ||
          [identifier hasPrefix:@"EDITION"] ||
          [identifier hasPrefix:@"PREFACE"] ||
          [identifier hasPrefix:@"FRONT"] ||
          [identifier hasPrefix:@"FOREWORD"] ||
          [identifier hasPrefix:@"ACKNOWLEDG"];
      if (!frontMatter) break;
      ++contentsIndex;
    }
  }
  NSMutableArray<NSDictionary *> *listing = [NSMutableArray array];
  for (NSDictionary *row in ordered)
    if (![titleTopics containsObject:row]) [listing addObject:row];
  NSUInteger tocPages = contentsPages(listing, paper);
  NSMutableDictionary<NSString *, NSNumber *> *pageNumbers = [NSMutableDictionary dictionary];
  NSUInteger numberedPage = 1;
  for (NSUInteger i = titleTopics.count; i <= ordered.count; ++i) {
    if (i == contentsIndex) numberedPage += tocPages;
    if (i == ordered.count) break;
    NSDictionary *row = ordered[i];
    pageNumbers[row[@"id"]] = @(numberedPage);
    numberedPage += _rendered[row[@"id"]].pageCount;
  }
  PDFDocument *contents = contentsDocument(listing, pageNumbers, paper);
  PDFDocument *book = [PDFDocument new];
  if (!titleTopics.count) [book insertPage:[[titlePage(_source.bookInformation, paper) pageAtIndex:0] copy]
      atIndex:book.pageCount];
  NSUInteger pageNumber = 1;
  for (NSUInteger i = 0; i <= ordered.count; ++i) {
    if (i == contentsIndex)
      for (NSUInteger p = 0; p < contents.pageCount; ++p) {
        PDFPage *page = [[contents pageAtIndex:p] copy];
        addPageNumber(page, pageNumber++);
        [book insertPage:page atIndex:book.pageCount];
      }
    if (i == ordered.count) break;
    NSDictionary *row = ordered[i];
    PDFDocument *topic = _rendered[row[@"id"]];
    BOOL isTitle = [titleTopics containsObject:row];
    for (NSUInteger p = 0; p < topic.pageCount; ++p) {
      PDFPage *page = [[topic pageAtIndex:p] copy];
      if (!isTitle) addPageNumber(page, pageNumber++);
      [book insertPage:page atIndex:book.pageCount];
    }
  }
  // PDFKit page copies can retain their source document's backing store.
  // Serialize while those sources are alive so the print dialog owns one
  // independent document after this job is released.
  PDFDocument *standalone = [[PDFDocument alloc] initWithData:book.dataRepresentation];
  [self finish:standalone error:nil];
}

- (void)finish:(PDFDocument *)document error:(NSError *)error {
  if (!_completion) return;
  void (^completion)(PDFDocument *, NSError *) = _completion;
  _completion = nil;
  _web.navigationDelegate = nil;
  _web = nil;
  completion(document, error);
}
@end
