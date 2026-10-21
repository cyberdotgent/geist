// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "../apache/machine/json.hpp"
#include "../apache/rest/rest.hpp"

#include <cassert>
#include <string>
#include <vector>

namespace {

using geist_apache::machine::parse_json;
using geist_apache::rest::Request;
using geist_apache::rest::Response;
using geist_apache::service::BookSource;
using geist_apache::service::Catalog;

std::string header(const Response& response, const std::string& name) {
  for (const auto& item : response.headers)
    if (item.first == name) return item.second;
  return {};
}

void expect_problem(const Response& response, int status) {
  assert(response.status == status);
  assert(response.content_type == "application/problem+json; charset=utf-8");
  assert(header(response, "X-Content-Type-Options") == "nosniff");
  const auto value = parse_json(response.body);
  assert(value.find("status"));
  assert(value.find("status")->number_text() == std::to_string(status));
}

} // namespace

int main() {
  BookSource visible;
  visible.summary.id = "manual.BOO";
  visible.summary.title = "A \"manual\"";
  visible.summary.document_number = "SC00-0000";
  visible.summary.built = "2026-10-21";
  visible.summary.size = 42;
  visible.summary.readable = false;
  Catalog catalog({std::move(visible)});

  auto result = geist_apache::rest::route(
      Request{"GET", "/.geist/api/v1", ""}, catalog, true);
  assert(result.status == 200);
  assert(result.content_type == "application/json; charset=utf-8");
  assert(header(result, "X-Content-Type-Options") == "nosniff");
  assert(parse_json(result.body).find("mcp")->as_string() == "../mcp");

  result = geist_apache::rest::route(
      Request{"GET", "/.geist/api/v1/books", ""}, catalog);
  assert(result.status == 200);
  const auto books = parse_json(result.body);
  assert(books.find("books"));
  assert(books.find("books")->as_array().size() == 1);
  assert(books.find("books")->as_array()[0].find("id")->as_string() ==
         "manual.BOO");
  const auto get_length = header(result, "Content-Length");

  result = geist_apache::rest::route(
      Request{"HEAD", "/.geist/api/v1/books", ""}, catalog);
  assert(result.status == 200);
  assert(result.body.empty());
  assert(header(result, "Content-Length") == get_length);

  result = geist_apache::rest::route(
      Request{"POST", "/.geist/api/v1/books", ""}, catalog);
  expect_problem(result, 405);
  assert(header(result, "Allow") == "GET, HEAD");

  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/missing", ""}, catalog),
                 404);
  result = geist_apache::rest::route(
      Request{"HEAD", "/.geist/api/v1/missing", ""}, catalog);
  assert(result.status == 404);
  assert(result.body.empty());
  assert(header(result, "Content-Length") != "0");
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/nope.boo", ""},
                     catalog),
                 404);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/manual.BOO", "x=1"},
                     catalog),
                 400);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/a%2fb/topics", ""},
                     catalog),
                 400);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/%2e%2e/topics", ""},
                     catalog),
                 400);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/%ff/topics", ""},
                     catalog),
                 400);
  // Percent escapes are decoded exactly once. This is the (missing) literal
  // book id "a%2fb", not a rejected encoded slash.
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/a%252fb/topics", ""},
                     catalog),
                 404);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/manual.BOO/topics/t%31/content",
                             "format=pdf"},
                     catalog),
                 406);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/manual.BOO/topics/t1/content",
                             "format="},
                     catalog),
                 406);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/manual.BOO/topics/t1/content",
                             "format=html&format=markdown"},
                     catalog),
                 400);
  expect_problem(geist_apache::rest::route(
                     Request{"GET", "/.geist/api/v1/books/manual.BOO/resources/r/content",
                             "other=stored"},
                     catalog),
                 400);
}
