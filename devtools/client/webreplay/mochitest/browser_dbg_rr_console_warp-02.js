/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-undef, no-unused-vars */

"use strict";

// Test basic console time warping functionality in web replay.
add_task(async function() {
  const dbg = await attachRecordingDebugger("doc_rr_logs.html", {
    waitForRecording: true,
  });

  const { tab, toolbox, threadFront } = dbg;
  const console = await getDebuggerSplitConsole(dbg);
  const hud = console.hud;

  let message = await warpToMessage(hud, dbg, "number: 1");
  // ok(message.classList.contains("paused-before"), "paused before message is shown");

  await stepOverToLine(threadFront, 18);
  await reverseStepOverToLine(threadFront, 17);

  message = findMessage(hud, "number: 1");
  // ok(message.classList.contains("paused-before"), "paused before message is shown");

  await toolbox.destroy();
  await gBrowser.removeTab(tab);
});
