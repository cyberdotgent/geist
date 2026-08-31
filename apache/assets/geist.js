// Copyright 2026 Yvan Janssens
// SPDX-License-Identifier: Apache-2.0

// Progressive enhancement only: the reader is entirely usable with
// JavaScript disabled, and every link is a real server-rendered URL.
(function () {
  "use strict";

  var filter = document.getElementById("geist-filter");
  var list = document.getElementById("geist-toc-list");
  if (filter && list) {
    var items = Array.prototype.slice.call(list.querySelectorAll("li"));
    filter.addEventListener("input", function () {
      var needle = filter.value.toLowerCase();
      items.forEach(function (li) {
        li.hidden = needle !== "" &&
          li.textContent.toLowerCase().indexOf(needle) === -1;
      });
    });
  }

  // Keep the selected topic visible when the contents are long.
  var current = list && list.querySelector('a[aria-current="page"]');
  if (current && current.scrollIntoView) {
    current.scrollIntoView({ block: "center" });
  }
})();
