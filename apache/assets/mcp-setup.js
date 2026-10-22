// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// MCP setup page: show the endpoint on the origin the browser actually used.
//
// The server writes the endpoint as it sees the request, which behind a
// TLS-terminating proxy is the wrong scheme and possibly the wrong host. Each
// such URL carries data-geist-endpoint="<path>", the path the server knows,
// and data-geist-quote saying how the text around it expects it spelled.
// Without JavaScript the server's URL stays, so this only ever corrects it.
(function () {
  "use strict";

  var origin = window.location.origin;
  if (!/^https?:\/\//.test(origin || "")) return;

  function shell(value) {
    return "'" + value.replace(/'/g, "'\\''") + "'";
  }

  var nodes = document.querySelectorAll("[data-geist-endpoint]");
  for (var i = 0; i < nodes.length; i++) {
    var node = nodes[i];
    var path = node.getAttribute("data-geist-endpoint");
    if (!path || path.charAt(0) !== "/" || path.charAt(1) === "/") continue;
    var url = origin + path;
    var quote = node.getAttribute("data-geist-quote");
    node.textContent = quote === "shell" ? shell(url)
                     : quote === "json" ? JSON.stringify(url)
                     : url;
    var attrs = (node.getAttribute("data-geist-attrs") || "").split(" ");
    for (var j = 0; j < attrs.length; j++) {
      if (attrs[j]) node.setAttribute(attrs[j], url);
    }
  }
})();
