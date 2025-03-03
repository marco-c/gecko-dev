/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

add_task(async function() {
  const generatedStubs = await generateNetworkEventStubs();

  const repoStubFilePath = getTestFilePath("../stubs/networkEvent.js");
  const repoStubFileContent = await OS.File.read(repoStubFilePath, {
    encoding: "utf-8",
  });

  is(
    generatedStubs,
    repoStubFileContent,
    "Generated stub has the expected content"
  );
  if (generatedStubs != repoStubFileContent) {
    ok(
      false,
      "The networkEvent stubs file needs to be updated by running " +
        "`mach test devtools/client/webconsole/test/fixtures/" +
        "stub-generators/browser_webconsole_update_stubs_network_event.js`"
    );
  } else {
    ok(true, "The networkEvent stubs file is up to date");
  }
});
