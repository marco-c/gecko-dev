const URL =
  "http://mochi.test:8888/browser/browser/base/content/test/trackingUI/file_trackingUI_fetch.html";

add_task(async function test_fetch() {
  await SpecialPowers.pushPrefEnv({
    set: [["privacy.trackingprotection.enabled", true]],
  });

  await BrowserTestUtils.withNewTab({ gBrowser, url: URL }, async function(
    newTabBrowser
  ) {
    let contentBlockingEvent = waitForContentBlockingEvent();
    await ContentTask.spawn(newTabBrowser, null, async function() {
      await content.wrappedJSObject
        .test_fetch()
        .then(response => Assert.ok(false, "should have denied the request"))
        .catch(e => Assert.ok(true, `Caught exception: ${e}`));
    });
    await contentBlockingEvent;

    let gProtectionsHandler = newTabBrowser.ownerGlobal.gProtectionsHandler;
    ok(gProtectionsHandler, "got CB object");

    ok(
      gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
      "has detected content blocking"
    );
    ok(
      gProtectionsHandler.iconBox.hasAttribute("active"),
      "icon box is active"
    );
    is(
      gProtectionsHandler.iconBox.getAttribute("tooltiptext"),
      gNavigatorBundle.getString("trackingProtection.icon.activeTooltip"),
      "correct tooltip"
    );
  });
});
