/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

/**
 * portalminasnet.com - Override window.sidebar with something falsey
 * WebCompat issue #18143 - https://webcompat.com/issues/18143
 *
 * This site is blocking onmousedown and onclick if window.sidebar is something
 * that evaluates to true, rendering the login unusable. This patch overrides
 * window.sidebar with false, so the blocking event handlers won't get
 * registered.
 */

/* globals exportFunction */

console.info(
  "window.sidebar has been shimmed for compatibility reasons. See https://webcompat.com/issues/18143 for details."
);

Object.defineProperty(window.wrappedJSObject, "sidebar", {
  get: exportFunction(function() {
    return false;
  }, window),

  set: exportFunction(function() {}, window),
});
