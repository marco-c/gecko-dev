/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-shadow, max-nested-callbacks */

"use strict";

/**
 * Make sure that setting a breakpoint in a line with bytecodes in multiple
 * scripts, sets the breakpoint in all of them (bug 793214).
 */

add_task(
  threadFrontTest(({ threadFront, debuggee }) => {
    return new Promise(resolve => {
      threadFront.once("paused", async function(packet) {
        const source = await getSourceById(
          threadFront,
          packet.frame.where.actor
        );
        const location = {
          sourceUrl: source.url,
          line: debuggee.line0 + 2,
          column: 8,
        };

        threadFront.setBreakpoint(location, {});

        threadFront.once("paused", function(packet) {
          // Check the return value.
          Assert.equal(packet.why.type, "breakpoint");
          // Check that the breakpoint worked.
          Assert.equal(debuggee.a, undefined);

          // Remove the breakpoint.
          threadFront.removeBreakpoint(location);

          const location2 = {
            sourceUrl: source.url,
            line: debuggee.line0 + 2,
            column: 32,
          };

          threadFront.setBreakpoint(location2, {});

          threadFront.once("paused", function(packet) {
            // Check the return value.
            Assert.equal(packet.why.type, "breakpoint");
            // Check that the breakpoint worked.
            Assert.equal(debuggee.a.b, 1);
            Assert.equal(debuggee.res, undefined);

            // Remove the breakpoint.
            threadFront.removeBreakpoint(location2);

            threadFront.resume().then(resolve);
          });

          // Continue until the breakpoint is hit again.
          threadFront.resume();
        });

        // Continue until the breakpoint is hit.
        threadFront.resume();
      });

      /* eslint-disable */
      Cu.evalInSandbox("var line0 = Error().lineNumber;\n" +
                       "debugger;\n" +                      // line0 + 1
                       "var a = { b: 1, f: function() { return 2; } };\n" + // line0+2
                       "var res = a.f();\n",               // line0 + 3
                       debuggee);
      /* eslint-enable */
    });
  })
);
