/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */
/* eslint-disable no-shadow, max-nested-callbacks */

"use strict";

/**
 * Check that we can set breakpoints in columns, not just lines.
 */

add_task(
  threadFrontTest(({ threadFront, debuggee, client }) => {
    return new Promise(resolve => {
      // Debugger statement
      threadFront.once("paused", async function(packet) {
        const source = await getSourceById(
          threadFront,
          packet.frame.where.actor
        );
        const location = {
          sourceUrl: source.url,
          line: debuggee.line0 + 1,
          column: 55,
        };
        let timesBreakpointHit = 0;

        threadFront.setBreakpoint(location, {});

        threadFront.on("paused", function onPaused(packet) {
          Assert.equal(packet.why.type, "breakpoint");
          Assert.equal(packet.frame.where.actor, source.actor);
          Assert.equal(packet.frame.where.line, location.line);
          Assert.equal(packet.frame.where.column, location.column);

          Assert.equal(debuggee.acc, timesBreakpointHit);
          Assert.equal(
            packet.frame.environment.bindings.variables.i.value,
            timesBreakpointHit
          );

          if (++timesBreakpointHit === 3) {
            threadFront.off("paused", onPaused);
            threadFront.removeBreakpoint(location);
            threadFront.resume().then(resolve);
          } else {
            threadFront.resume();
          }
        });

        // Continue until the breakpoint is hit.
        threadFront.resume();
      });

      /* eslint-disable */
      Cu.evalInSandbox(
        "var line0 = Error().lineNumber;\n" +
        "(function () { debugger; this.acc = 0; for (var i = 0; i < 3; i++) this.acc++; }());",
        debuggee
      );
      /* eslint-enable */
    });
  })
);
