// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#import "BookSource.h"

#include "geist/document.hpp"

#include <algorithm>
#include <optional>
#include <memory>
#include <string>
#include <unordered_map>

namespace {
NSString *stringOf(const std::string &value) {
  return [[NSString alloc] initWithBytes:value.data() length:value.size()
                                encoding:NSUTF8StringEncoding] ?: @"";
}
std::string bytesOf(NSString *value) { return value.UTF8String ?: ""; }
NSString *escaped(NSString *value) {
  NSMutableString *result = [value mutableCopy];
  for (NSArray *pair in @[@[@"&", @"&amp;"], @[@"<", @"&lt;"],
                           @[@">", @"&gt;"], @[@"\"", @"&quot;"]]) {
    [result replaceOccurrencesOfString:pair[0] withString:pair[1]
                               options:0 range:NSMakeRange(0, result.length)];
  }
  return result;
}
NSString *segment(NSString *value) {
  NSMutableCharacterSet *unreserved = [NSCharacterSet.alphanumericCharacterSet mutableCopy];
  [unreserved addCharactersInString:@"-._~"];
  return [value stringByAddingPercentEncodingWithAllowedCharacters:
      unreserved] ?: value;
}
std::string topicURL(const std::string &id, const std::string &fragment = {}) {
  NSString *url = [@"geist://book/topic/" stringByAppendingString:segment(stringOf(id))];
  if (!fragment.empty()) url = [url stringByAppendingFormat:@"#%@", segment(stringOf(fragment))];
  return bytesOf(url);
}
std::string objectURL(const std::string &id) {
  return bytesOf([@"geist://book/object/" stringByAppendingString:segment(stringOf(id))]);
}
NSString *cleanSnippet(NSString *text) {
  for (NSArray<NSString *> *rule in @[
    @[@"\\[([^\\]]+)\\]\\([^)]*\\)", @"$1"],
    @[@"\\\\([[:punct:]])", @"$1"],
    @[@"[`*_#>]+", @" "],
    @[@"\\s+", @" "]]) {
    NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:rule[0]
        options:0 error:nil];
    text = [regex stringByReplacingMatchesInString:text options:0
        range:NSMakeRange(0, text.length) withTemplate:rule[1]];
  }
  return [text stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
}
struct Destination { std::string topic, fragment, resource; };
} // namespace

@implementation GBBookSource {
  std::shared_ptr<geist::BooDocument> _document;
  std::unordered_map<std::string, Destination> _links;
  NSMutableDictionary<NSString *, NSData *> *_objects;
  NSString *_bookTitle;
  NSArray<NSDictionary<NSString *, id> *> *_contents;
  NSDictionary<NSString *, id> *_bookInformation;
  NSString *_stylesheet;
}

- (instancetype)init {
  if ((self = [super init])) {
    _objects = [NSMutableDictionary dictionary];
    _contents = @[];
    _bookTitle = @"";
    _bookInformation = @{};
    NSString *path = [NSBundle.mainBundle pathForResource:@"html-styling" ofType:@"css"];
    _stylesheet = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil] ?: @"";
  }
  return self;
}

- (BOOL)hasBook { return _document != nullptr; }
- (NSString *)bookTitle { return _bookTitle; }
- (NSArray<NSDictionary<NSString *, id> *> *)contents { return _contents; }
- (NSDictionary<NSString *, id> *)bookInformation { return _bookInformation; }

- (void)closeBook {
  @synchronized(self) {
    _document.reset();
    _links.clear();
    [_objects removeAllObjects];
  }
  _contents = @[];
  _bookTitle = @"";
  _bookInformation = @{};
}

- (BOOL)openURL:(NSURL *)url error:(NSError **)error {
  try {
    auto opened = geist::BooDocument::open(std::filesystem::path(bytesOf(url.path)));
    [self closeBook];
    @synchronized(self) { _document = std::make_shared<geist::BooDocument>(std::move(opened)); }
    const auto &book = _document->book_properties();
    _bookTitle = [stringOf(book.title.empty() ? book.short_title : book.title)
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    NSMutableArray *rows = [NSMutableArray array];
    for (const auto &entry : _document->table_of_contents()) {
      NSString *topicID = stringOf(entry.id);
      NSString *title = [stringOf(entry.title) stringByTrimmingCharactersInSet:
          NSCharacterSet.whitespaceAndNewlineCharacterSet];
      [rows addObject:@{@"id":topicID, @"title":title, @"level":@(entry.level)}];
    }
    _contents = [rows copy];
    for (const auto &entry : _document->table_of_contents()) {
      for (const auto &target : entry.link_targets()) {
        Destination dest{entry.id, target.fragment, target.resource};
        _links[target.id] = dest;
        if (target.kind == geist::LinkTargetKind::figure) _links["FIG" + target.id] = dest;
        if (target.kind == geist::LinkTargetKind::table) _links["TBL" + target.id] = dest;
      }
    }
    const auto &metadata = _document->metadata();
    NSMutableDictionary<NSString *, id> *info = [NSMutableDictionary dictionary];
    auto append = [&](NSString *key, const std::string &value) {
      NSString *text = [stringOf(value) stringByTrimmingCharactersInSet:
          NSCharacterSet.whitespaceAndNewlineCharacterSet];
      if (text.length) info[key] = text;
    };
    append(@"title", book.title); append(@"shortTitle", book.short_title);
    append(@"documentNumber", book.document_number);
    append(@"language", book.language); append(@"version", book.version);
    append(@"buildVersion", book.build_version); append(@"date", book.date);
    append(@"security", book.security); append(@"copyright", book.copyright);
    if (!book.authors.empty()) {
      NSMutableArray *authors = [NSMutableArray array];
      for (const auto &author : book.authors) [authors addObject:stringOf(author)];
      info[@"authors"] = [authors componentsJoinedByString:@", "];
    }
    info[@"topicCount"] = @(_contents.count);
    info[@"resourceCount"] = @(_document->resources().size());
    info[@"fileURL"] = url;
    info[@"fileSize"] = @(metadata.file_size);
    info[@"pageCount"] = @(metadata.page_count);
    _bookInformation = [info copy];
    return YES;
  } catch (const std::exception &exception) {
    [self closeBook];
    if (error) *error = [NSError errorWithDomain:@"gent.cyber.geist-viewer" code:1
      userInfo:@{NSLocalizedDescriptionKey:stringOf(exception.what())}];
    return NO;
  }
}

- (NSURL *)topicURL:(NSString *)topicID {
  return [NSURL URLWithString:stringOf(topicURL(bytesOf(topicID)))];
}

- (NSArray<NSDictionary<NSString *, NSString *> *> *)searchForText:(NSString *)query {
  std::shared_ptr<geist::BooDocument> document;
  @synchronized(self) { document = _document; }
  if (!document || !query.length) return @[];
  NSMutableArray *matches = [NSMutableArray array];
  for (const auto &entry : document->table_of_contents()) {
    @autoreleasepool {
      NSString *title = stringOf(entry.title);
      NSString *body = @"";
      try { body = stringOf(entry.markdown()); } catch (const std::exception &) { continue; }
      NSRange titleMatch = [title rangeOfString:query options:NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch];
      NSRange bodyMatch = [body rangeOfString:query options:NSCaseInsensitiveSearch | NSDiacriticInsensitiveSearch];
      if (titleMatch.location == NSNotFound && bodyMatch.location == NSNotFound) continue;
      NSString *snippet = title;
      if (bodyMatch.location != NSNotFound) {
        NSUInteger start = bodyMatch.location > 45 ? bodyMatch.location - 45 : 0;
        NSUInteger length = MIN(body.length - start, (NSUInteger)110);
        snippet = [body substringWithRange:NSMakeRange(start, length)];
        snippet = cleanSnippet(snippet);
      }
      [matches addObject:@{@"id":stringOf(entry.id), @"title":title, @"snippet":snippet}];
    }
  }
  return matches;
}

- (NSData *)topicData:(NSString *)topicID {
  std::shared_ptr<geist::BooDocument> document;
  std::unordered_map<std::string, Destination> links;
  @synchronized(self) { document = _document; links = _links; }
  if (!document) return nil;
  const auto *entry = document->find_toc_entry(bytesOf(topicID));
  if (!entry) return nil;
  geist::HtmlRenderOptions render;
  render.resolve_topic = [](const std::string &id) -> std::optional<std::string> { return topicURL(id); };
  render.resolve_resource = [](const std::string &id) -> std::optional<std::string> { return objectURL(id); };
  render.resolve_anchor = [document, links, topicID](const std::string &id) -> std::optional<std::string> {
    auto found = links.find(id);
    if (found == links.end()) {
      if (document->find_toc_entry(id)) return topicURL(id);
      return std::nullopt;
    }
    const auto &dest = found->second;
    if (!dest.resource.empty()) {
      const std::string prefix = "resource:";
      return objectURL(dest.resource.rfind(prefix, 0) == 0 ? dest.resource.substr(prefix.size()) : dest.resource);
    }
    if (dest.topic == bytesOf(topicID)) {
      if (dest.fragment.empty()) return std::nullopt;
      return "#" + bytesOf(segment(stringOf(dest.fragment)));
    }
    return topicURL(dest.topic, dest.fragment);
  };
  NSString *title = escaped([stringOf(entry->title) stringByTrimmingCharactersInSet:
      NSCharacterSet.whitespaceAndNewlineCharacterSet]);
  NSString *html = [NSString stringWithFormat:
      @"<!doctype html><html lang='en'><head><meta charset='utf-8'><title>%@</title>"
       "<style>%@\n.reader-topic-header{margin:0 0 1rem 0;padding:0 0 .35rem 0;}"
       ".reader-topic-id{font-style:italic;margin-right:.5rem;}"
       ".reader-topic-title{font-weight:700;}</style></head>"
       "<body class='geist-book'><div class='reader-topic-header'>"
       "<span class='reader-topic-id'>%@</span> <span class='reader-topic-title'>%@</span>"
       "</div>%@</body></html>", title, _stylesheet, escaped(topicID), title,
      stringOf(entry->html_fragment(render))];
  return [html dataUsingEncoding:NSUTF8StringEncoding];
}

- (NSData *)objectData:(NSString *)objectID {
  std::shared_ptr<geist::BooDocument> document;
  NSData *cached;
  @synchronized(self) { document = _document; cached = _objects[objectID]; }
  if (!document) return nil;
  if (cached) return cached.length ? cached : nil;
  try {
    auto bytes = document->read_resource_png(bytesOf(objectID));
    NSData *data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    @synchronized(self) { if (document == _document) _objects[objectID] = data; }
    return data;
  } catch (const std::exception &) {
    @synchronized(self) { if (document == _document) _objects[objectID] = [NSData data]; }
    return nil;
  }
}

- (void)webView:(WKWebView *)webView startURLSchemeTask:(id<WKURLSchemeTask>)task {
  (void)webView;
  NSURL *url = task.request.URL;
  NSString *path = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO].percentEncodedPath;
  NSData *data = nil;
  NSString *type = nil;
  try {
    if ([url.host isEqualToString:@"book"] && [path hasPrefix:@"/topic/"]) {
      data = [self topicData:[[path substringFromIndex:7] stringByRemovingPercentEncoding]];
      type = @"text/html";
    } else if ([url.host isEqualToString:@"book"] && [path hasPrefix:@"/object/"]) {
      data = [self objectData:[[path substringFromIndex:8] stringByRemovingPercentEncoding]];
      type = @"image/png";
    }
  } catch (const std::exception &exception) {
    [task didFailWithError:[NSError errorWithDomain:@"gent.cyber.geist-viewer" code:2
      userInfo:@{NSLocalizedDescriptionKey:stringOf(exception.what())}]];
    return;
  }
  if (!data) {
    [task didFailWithError:[NSError errorWithDomain:NSURLErrorDomain code:NSURLErrorFileDoesNotExist userInfo:nil]];
    return;
  }
  [task didReceiveResponse:[[NSURLResponse alloc] initWithURL:url MIMEType:type
      expectedContentLength:data.length textEncodingName:[type isEqualToString:@"text/html"] ? @"utf-8" : nil]];
  [task didReceiveData:data];
  [task didFinish];
}

- (void)webView:(WKWebView *)webView stopURLSchemeTask:(id<WKURLSchemeTask>)task {
  (void)webView; (void)task;
}
@end
