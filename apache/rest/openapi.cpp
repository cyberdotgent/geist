// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

#include "openapi.hpp"

namespace geist_apache::rest {
namespace {

constexpr std::string_view document = R"OPENAPI({
  "openapi": "3.1.2",
  "info": {
    "title": "Geist BOO shelf API",
    "version": "1.0.0",
    "description": "Read-only access to every indexed topic and resource in an Apache mod_geist BOO shelf. The API is available only when both BooIndex and GeistAPI are enabled. Authentication and authorization are configured with Apache directives; this API defines no separate credential scheme."
  },
  "servers": [
    {
      "url": ".",
      "description": "The shelf that served this document"
    }
  ],
  "tags": [
    {"name": "Service"},
    {"name": "Books"},
    {"name": "Topics"},
    {"name": "Resources"}
  ],
  "paths": {
    "/.geist/api/v1": {
      "get": {
        "tags": ["Service"],
        "summary": "Describe this shelf service",
        "operationId": "getService",
        "responses": {
          "200": {
            "description": "Service capabilities",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Service"}}}
          },
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Service"],
        "summary": "Inspect the service response headers",
        "operationId": "headService",
        "responses": {
          "200": {"description": "The service is available; no response body"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books": {
      "get": {
        "tags": ["Books"],
        "summary": "List the BOO books in this shelf",
        "operationId": "listBooks",
        "responses": {
          "200": {
            "description": "All immediate .boo and .BOO files visible in the shelf",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/BookList"}}}
          },
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Books"],
        "summary": "Inspect the book-list response headers",
        "operationId": "headBooks",
        "responses": {
          "200": {"description": "The book list is available; no response body"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}": {
      "parameters": [{"$ref": "#/components/parameters/BookId"}],
      "get": {
        "tags": ["Books"],
        "summary": "Get BOO book metadata",
        "operationId": "getBook",
        "responses": {
          "200": {
            "description": "Book metadata",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Book"}}}
          },
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Books"],
        "summary": "Inspect the book-metadata response headers",
        "operationId": "headBook",
        "responses": {
          "200": {"description": "The book exists; no response body"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/toc": {
      "parameters": [{"$ref": "#/components/parameters/BookId"}],
      "get": {
        "tags": ["Books"],
        "summary": "Get the book table of contents",
        "operationId": "getTableOfContents",
        "responses": {
          "200": {
            "description": "The authored table-of-contents entries in source order",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/TableOfContents"}}}
          },
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Books"],
        "summary": "Inspect the table-of-contents response headers",
        "operationId": "headTableOfContents",
        "responses": {
          "200": {"description": "The table of contents is available; no response body"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/topics": {
      "parameters": [{"$ref": "#/components/parameters/BookId"}],
      "get": {
        "tags": ["Topics"],
        "summary": "List every indexed topic",
        "description": "Returns the complete container topic index, including topics absent from the authored table of contents.",
        "operationId": "listTopics",
        "responses": {
          "200": {
            "description": "All indexed topics in source order",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/TopicList"}}}
          },
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Topics"],
        "summary": "Inspect the topic-list response headers",
        "operationId": "headTopics",
        "responses": {
          "200": {"description": "The indexed topic list is available; no response body"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/topics/{topicId}": {
      "parameters": [
        {"$ref": "#/components/parameters/BookId"},
        {"$ref": "#/components/parameters/TopicId"}
      ],
      "get": {
        "tags": ["Topics"],
        "summary": "Get topic metadata and rendering diagnostics",
        "operationId": "getTopic",
        "responses": {
          "200": {
            "description": "Topic metadata",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Topic"}}}
          },
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Topics"],
        "summary": "Inspect the topic-metadata response headers",
        "operationId": "headTopic",
        "responses": {
          "200": {"description": "The topic exists; no response body"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/topics/{topicId}/content": {
      "parameters": [
        {"$ref": "#/components/parameters/BookId"},
        {"$ref": "#/components/parameters/TopicId"},
        {"$ref": "#/components/parameters/TopicFormat"}
      ],
      "get": {
        "tags": ["Topics"],
        "summary": "Render topic content",
        "description": "Renders the same DocumentIR as Markdown or an HTML fragment. Consult the topic metadata endpoint for its lowering route and fidelity diagnostics.",
        "operationId": "getTopicContent",
        "responses": {
          "200": {
            "description": "Rendered topic content",
            "content": {
              "text/markdown": {"schema": {"type": "string"}},
              "text/html": {"schema": {"type": "string"}}
            }
          },
          "400": {"$ref": "#/components/responses/BadRequest"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "406": {"$ref": "#/components/responses/NotAcceptable"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Topics"],
        "summary": "Inspect rendered-topic response headers",
        "operationId": "headTopicContent",
        "responses": {
          "200": {"description": "The requested rendering is available; no response body"},
          "400": {"$ref": "#/components/responses/BadRequest"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "406": {"$ref": "#/components/responses/NotAcceptable"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/resources": {
      "parameters": [{"$ref": "#/components/parameters/BookId"}],
      "get": {
        "tags": ["Resources"],
        "summary": "List book resources",
        "operationId": "listResources",
        "responses": {
          "200": {
            "description": "Resource metadata in source order",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/ResourceList"}}}
          },
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Resources"],
        "summary": "Inspect the resource-list response headers",
        "operationId": "headResources",
        "responses": {
          "200": {"description": "The resource list is available; no response body"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/resources/{resourceId}": {
      "parameters": [
        {"$ref": "#/components/parameters/BookId"},
        {"$ref": "#/components/parameters/ResourceId"}
      ],
      "get": {
        "tags": ["Resources"],
        "summary": "Get resource metadata",
        "operationId": "getResource",
        "responses": {
          "200": {
            "description": "Resource metadata",
            "content": {"application/json": {"schema": {"$ref": "#/components/schemas/Resource"}}}
          },
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Resources"],
        "summary": "Inspect the resource-metadata response headers",
        "operationId": "headResource",
        "responses": {
          "200": {"description": "The resource exists; no response body"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    },
    "/.geist/api/v1/books/{bookId}/resources/{resourceId}/content": {
      "parameters": [
        {"$ref": "#/components/parameters/BookId"},
        {"$ref": "#/components/parameters/ResourceId"},
        {"$ref": "#/components/parameters/ResourceRepresentation"}
      ],
      "get": {
        "tags": ["Resources"],
        "summary": "Read resource bytes",
        "description": "The stored representation is byte-for-byte container data. The web representation is an explicit supported conversion, never a guessed or partial conversion.",
        "operationId": "getResourceContent",
        "responses": {
          "200": {
            "description": "Stored or explicitly converted resource bytes",
            "content": {
              "application/octet-stream": {"schema": {"type": "string", "contentEncoding": "binary"}},
              "image/png": {"schema": {"type": "string", "contentEncoding": "binary"}},
              "image/gif": {"schema": {"type": "string", "contentEncoding": "binary"}}
            }
          },
          "400": {"$ref": "#/components/responses/BadRequest"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "406": {"$ref": "#/components/responses/NotAcceptable"},
          "415": {"$ref": "#/components/responses/UnsupportedMediaType"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      },
      "head": {
        "tags": ["Resources"],
        "summary": "Inspect resource-content response headers",
        "operationId": "headResourceContent",
        "responses": {
          "200": {"description": "The requested representation is available; no response body"},
          "400": {"$ref": "#/components/responses/BadRequest"},
          "404": {"$ref": "#/components/responses/NotFound"},
          "406": {"$ref": "#/components/responses/NotAcceptable"},
          "415": {"$ref": "#/components/responses/UnsupportedMediaType"},
          "500": {"$ref": "#/components/responses/InternalError"}
        }
      }
    }
  },
  "components": {
    "parameters": {
      "BookId": {
        "name": "bookId",
        "in": "path",
        "required": true,
        "description": "Exact shelf-relative .boo or .BOO filename, percent-encoded as one path segment. Filesystem paths and document numbers are not book identifiers.",
        "schema": {"type": "string", "minLength": 1}
      },
      "TopicId": {
        "name": "topicId",
        "in": "path",
        "required": true,
        "description": "Exact BOO topic identifier, percent-encoded as one path segment.",
        "schema": {"type": "string", "minLength": 1}
      },
      "ResourceId": {
        "name": "resourceId",
        "in": "path",
        "required": true,
        "description": "Exact BOO resource identifier, percent-encoded as one path segment.",
        "schema": {"type": "string", "minLength": 1}
      },
      "TopicFormat": {
        "name": "format",
        "in": "query",
        "required": false,
        "description": "Requested DocumentIR rendering; defaults to markdown.",
        "schema": {"type": "string", "enum": ["markdown", "html"], "default": "markdown"}
      },
      "ResourceRepresentation": {
        "name": "representation",
        "in": "query",
        "required": false,
        "description": "stored returns exact container bytes; web requests an explicit supported web conversion and fails if none exists.",
        "schema": {"type": "string", "enum": ["stored", "web"], "default": "stored"}
      }
    },
    "responses": {
      "BadRequest": {
        "description": "Malformed identifier or query parameter",
        "content": {"application/problem+json": {"schema": {"$ref": "#/components/schemas/Problem"}}}
      },
      "NotFound": {
        "description": "The requested book, topic, or resource does not exist in this shelf",
        "content": {"application/problem+json": {"schema": {"$ref": "#/components/schemas/Problem"}}}
      },
      "NotAcceptable": {
        "description": "The requested representation is not acceptable",
        "content": {"application/problem+json": {"schema": {"$ref": "#/components/schemas/Problem"}}}
      },
      "UnsupportedMediaType": {
        "description": "No explicit web conversion exists for this resource",
        "content": {"application/problem+json": {"schema": {"$ref": "#/components/schemas/Problem"}}}
      },
      "InternalError": {
        "description": "The BOO data could not be read",
        "content": {"application/problem+json": {"schema": {"$ref": "#/components/schemas/Problem"}}}
      }
    },
    "schemas": {
      "Service": {
        "type": "object",
        "additionalProperties": false,
        "required": ["service", "apiVersion", "readOnly"],
        "properties": {
          "service": {"const": "mod_geist"},
          "apiVersion": {"const": "v1"},
          "readOnly": {"const": true},
          "openapi": {"type": "string", "format": "uri-reference"},
          "mcp": {"type": ["string", "null"], "format": "uri-reference"}
        }
      },
      "BookSummary": {
        "type": "object",
        "additionalProperties": false,
        "required": ["id", "title", "size", "readable"],
        "properties": {
          "id": {"type": "string", "description": "Exact shelf-relative filename"},
          "title": {"type": "string"},
          "documentNumber": {"type": "string"},
          "built": {"type": "string"},
          "size": {"type": "integer", "minimum": 0},
          "readable": {"type": "boolean"}
        }
      },
      "BookList": {
        "type": "object",
        "additionalProperties": false,
        "required": ["books"],
        "properties": {
          "books": {"type": "array", "items": {"$ref": "#/components/schemas/BookSummary"}}
        }
      },
      "Book": {
        "type": "object",
        "additionalProperties": false,
        "required": ["id", "title", "size", "readable", "language", "formatVersion", "buildVersion", "copyright", "security", "date", "authors", "topicCount", "tocCount", "resourceCount"],
        "properties": {
          "id": {"type": "string", "description": "Exact shelf-relative filename"},
          "title": {"type": "string"},
          "documentNumber": {"type": "string"},
          "built": {"type": "string"},
          "size": {"type": "integer", "minimum": 0},
          "readable": {"type": "boolean"},
          "language": {"type": "string"},
          "formatVersion": {"type": "string"},
          "buildVersion": {"type": "string"},
          "copyright": {"type": "string"},
          "security": {"type": "string"},
          "date": {"type": "string"},
          "authors": {"type": "array", "items": {"type": "string"}},
          "topicCount": {"type": "integer", "minimum": 0},
          "tocCount": {"type": "integer", "minimum": 0},
          "resourceCount": {"type": "integer", "minimum": 0}
        }
      },
      "TableOfContents": {
        "type": "object",
        "additionalProperties": false,
        "required": ["bookId", "entries"],
        "properties": {
          "bookId": {"type": "string"},
          "entries": {"type": "array", "items": {"$ref": "#/components/schemas/TopicSummary"}}
        }
      },
      "Diagnostic": {
        "type": "object",
        "additionalProperties": false,
        "required": ["severity", "route", "family", "reason", "detail", "degradationCount"],
        "properties": {
          "severity": {"type": "string"},
          "route": {"type": "string", "description": "DocumentIR lowering route used for this topic"},
          "family": {"type": "string"},
          "reason": {"type": "string"},
          "detail": {"type": "string"},
          "degradationCount": {"type": "integer", "minimum": 0}
        }
      },
      "TopicSummary": {
        "type": "object",
        "additionalProperties": false,
        "required": ["id", "title", "headingLevel", "sourceIndex", "topicNumber", "startLogicalRecord", "endLogicalRecord", "inToc", "tocLevel", "tocStyle"],
        "properties": {
          "id": {"type": "string"},
          "title": {"type": "string"},
          "headingLevel": {"type": "string"},
          "sourceIndex": {"type": "integer", "minimum": 0},
          "topicNumber": {"type": "integer", "minimum": 0},
          "startLogicalRecord": {"type": "integer", "minimum": 0},
          "endLogicalRecord": {"type": "integer", "minimum": 0},
          "inToc": {"type": "boolean", "description": "False for indexed topics omitted from the authored table of contents"},
          "tocLevel": {"type": "integer", "minimum": 0},
          "tocStyle": {"type": "integer", "minimum": 0}
        }
      },
      "TopicList": {
        "type": "object",
        "additionalProperties": false,
        "required": ["bookId", "topics"],
        "properties": {
          "bookId": {"type": "string"},
          "topics": {"type": "array", "items": {"$ref": "#/components/schemas/TopicSummary"}}
        }
      },
      "Topic": {
        "type": "object",
        "additionalProperties": false,
        "required": ["bookId", "id", "title", "headingLevel", "sourceIndex", "topicNumber", "startLogicalRecord", "endLogicalRecord", "inToc", "tocLevel", "tocStyle", "diagnostic"],
        "properties": {
          "bookId": {"type": "string"},
          "id": {"type": "string"},
          "title": {"type": "string"},
          "headingLevel": {"type": "string"},
          "sourceIndex": {"type": "integer", "minimum": 0},
          "topicNumber": {"type": "integer", "minimum": 0},
          "startLogicalRecord": {"type": "integer", "minimum": 0},
          "endLogicalRecord": {"type": "integer", "minimum": 0},
          "inToc": {"type": "boolean"},
          "tocLevel": {"type": "integer", "minimum": 0},
          "tocStyle": {"type": "integer", "minimum": 0},
          "diagnostic": {"$ref": "#/components/schemas/Diagnostic"}
        }
      },
      "ResourceSummary": {
        "type": "object",
        "additionalProperties": false,
        "required": ["id", "name", "kind", "storedFormat", "description", "layout", "size"],
        "properties": {
          "id": {"type": "string"},
          "name": {"type": "string"},
          "kind": {"type": "string"},
          "storedFormat": {"type": "string"},
          "description": {"type": "string"},
          "layout": {"type": "string"},
          "size": {"type": "integer", "minimum": 0}
        }
      },
      "ResourceList": {
        "type": "object",
        "additionalProperties": false,
        "required": ["bookId", "resources"],
        "properties": {
          "bookId": {"type": "string"},
          "resources": {"type": "array", "items": {"$ref": "#/components/schemas/ResourceSummary"}}
        }
      },
      "Resource": {
        "type": "object",
        "additionalProperties": false,
        "required": ["bookId", "id", "name", "kind", "storedFormat", "description", "layout", "size"],
        "properties": {
          "bookId": {"type": "string"},
          "id": {"type": "string"},
          "name": {"type": "string"},
          "kind": {"type": "string"},
          "storedFormat": {"type": "string"},
          "description": {"type": "string"},
          "layout": {"type": "string"},
          "size": {"type": "integer", "minimum": 0}
        }
      },
      "Problem": {
        "type": "object",
        "additionalProperties": true,
        "required": ["type", "title", "status"],
        "properties": {
          "type": {"type": "string", "format": "uri-reference"},
          "title": {"type": "string"},
          "status": {"type": "integer", "minimum": 400, "maximum": 599},
          "detail": {"type": "string"},
          "instance": {"type": "string", "format": "uri-reference"}
        }
      }
    }
  }
})OPENAPI";

} // namespace

std::string_view openapi_document() { return document; }

} // namespace geist_apache::rest
