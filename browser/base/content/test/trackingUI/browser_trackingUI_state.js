/*
 * Test that the Tracking Protection section is visible in the Control Center
 * and has the correct state for the cases when:
 *
 * In a normal window as well as a private window,
 *   With TP enabled
 *     1) A page with no tracking elements is loaded.
 *     2) A page with tracking elements is loaded and they are blocked.
 *     3) A page with tracking elements is loaded and they are not blocked.
 *   With TP disabled
 *     1) A page with no tracking elements is loaded.
 *     2) A page with tracking elements is loaded.
 *
 * See also Bugs 1175327, 1043801, 1178985
 */

const TP_PREF = "privacy.trackingprotection.enabled";
const TP_PB_PREF = "privacy.trackingprotection.pbmode.enabled";
const TPC_PREF = "network.cookie.cookieBehavior";
const DTSCBN_PREF = "dom.testing.sync-content-blocking-notifications";
const BENIGN_PAGE =
  "http://tracking.example.org/browser/browser/base/content/test/trackingUI/benignPage.html";
const TRACKING_PAGE =
  "http://tracking.example.org/browser/browser/base/content/test/trackingUI/trackingPage.html";
const COOKIE_PAGE =
  "http://not-tracking.example.com/browser/browser/base/content/test/trackingUI/cookiePage.html";
var gProtectionsHandler = null;
var TrackingProtection = null;
var ThirdPartyCookies = null;
var tabbrowser = null;
var gTrackingPageURL = TRACKING_PAGE;

registerCleanupFunction(function() {
  TrackingProtection = gProtectionsHandler = ThirdPartyCookies = tabbrowser = null;
  UrlClassifierTestUtils.cleanupTestTrackers();
  Services.prefs.clearUserPref(TP_PREF);
  Services.prefs.clearUserPref(TP_PB_PREF);
  Services.prefs.clearUserPref(TPC_PREF);
  Services.prefs.clearUserPref(DTSCBN_PREF);
});

// This is a special version of "hidden" that doesn't check for item
// visibility and just asserts the display and opacity attributes.
// That way we can test elements even when their panel is hidden...
function hidden(sel) {
  let win = tabbrowser.ownerGlobal;
  let el = win.document.querySelector(sel);
  let display = win.getComputedStyle(el).getPropertyValue("display", null);
  let opacity = win.getComputedStyle(el).getPropertyValue("opacity", null);
  return display === "none" || opacity === "0";
}

function clickButton(sel) {
  let win = tabbrowser.ownerGlobal;
  let el = win.document.querySelector(sel);
  el.doCommand();
}

function testBenignPage() {
  info("Non-tracking content must not be blocked");
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "no trackers are detected"
  );
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("hasException"),
    "content shows no exception"
  );

  ok(
    !gProtectionsHandler.iconBox.hasAttribute("active"),
    "shield is not active"
  );
  ok(
    !gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "icon box shows no exception"
  );
  ok(
    !gProtectionsHandler.iconBox.hasAttribute("tooltiptext"),
    "icon box has no tooltip"
  );

  ok(
    BrowserTestUtils.is_hidden(gProtectionsHandler.iconBox),
    "icon box is hidden"
  );

  ok(
    hidden("#protections-popup-category-cookies"),
    "Not showing cookie restrictions category"
  );
  ok(
    hidden("#protections-popup-category-tracking-protection"),
    "Not showing trackers category"
  );
}

function testBenignPageWithException() {
  info("Non-tracking content must not be blocked");
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "no trackers are detected"
  );
  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("hasException"),
    "content shows exception"
  );

  ok(
    !gProtectionsHandler.iconBox.hasAttribute("active"),
    "shield is not active"
  );
  ok(
    gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "shield shows exception"
  );
  is(
    gProtectionsHandler.iconBox.getAttribute("tooltiptext"),
    gNavigatorBundle.getString("trackingProtection.icon.disabledTooltip"),
    "correct tooltip"
  );

  ok(
    !BrowserTestUtils.is_hidden(gProtectionsHandler.iconBox),
    "icon box is not hidden"
  );

  ok(
    hidden("#protections-popup-category-cookies"),
    "Not showing cookie restrictions category"
  );
  ok(
    hidden("#protections-popup-category-tracking-protection"),
    "Not showing trackers category"
  );
}

function areTrackersBlocked(isPrivateBrowsing) {
  let blockedByTP = Services.prefs.getBoolPref(
    isPrivateBrowsing ? TP_PB_PREF : TP_PREF
  );
  let blockedByTPC =
    Services.prefs.getIntPref(TPC_PREF) ==
    Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER;
  return blockedByTP || blockedByTPC;
}

function testTrackingPage(window) {
  info("Tracking content must be blocked");
  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are detected"
  );
  ok(
    !gProtectionsHandler._protectionsPopup.hasAttribute("hasException"),
    "content shows no exception"
  );

  let isWindowPrivate = PrivateBrowsingUtils.isWindowPrivate(window);
  let blockedByTP = areTrackersBlocked(isWindowPrivate);
  is(
    BrowserTestUtils.is_visible(gProtectionsHandler.iconBox),
    blockedByTP,
    "icon box is" + (blockedByTP ? "" : " not") + " visible"
  );
  is(
    gProtectionsHandler.iconBox.hasAttribute("active"),
    blockedByTP,
    "shield is" + (blockedByTP ? "" : " not") + " active"
  );
  ok(
    !gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "icon box shows no exception"
  );
  is(
    gProtectionsHandler.iconBox.getAttribute("tooltiptext"),
    blockedByTP
      ? gNavigatorBundle.getString("trackingProtection.icon.activeTooltip")
      : "",
    "correct tooltip"
  );

  ok(
    !hidden("#protections-popup-category-tracking-protection"),
    "Showing trackers category"
  );
  if (gTrackingPageURL == COOKIE_PAGE) {
    ok(
      !hidden("#protections-popup-category-cookies"),
      "Showing cookie restrictions category"
    );
  } else {
    ok(
      hidden("#protections-popup-category-cookies"),
      "Not showing cookie restrictions category"
    );
  }
}

function testTrackingPageUnblocked(blockedByTP, window) {
  info("Tracking content must be white-listed and not blocked");
  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("detected"),
    "trackers are detected"
  );
  ok(
    gProtectionsHandler._protectionsPopup.hasAttribute("hasException"),
    "content shows exception"
  );

  ok(
    !gProtectionsHandler.iconBox.hasAttribute("active"),
    "shield is not active"
  );
  ok(
    gProtectionsHandler.iconBox.hasAttribute("hasException"),
    "shield shows exception"
  );
  is(
    gProtectionsHandler.iconBox.getAttribute("tooltiptext"),
    gNavigatorBundle.getString("trackingProtection.icon.disabledTooltip"),
    "correct tooltip"
  );

  ok(
    BrowserTestUtils.is_visible(gProtectionsHandler.iconBox),
    "icon box is visible"
  );

  ok(
    !hidden("#protections-popup-category-tracking-protection"),
    "Showing trackers category"
  );
  if (gTrackingPageURL == COOKIE_PAGE) {
    ok(
      !hidden("#protections-popup-category-cookies"),
      "Showing cookie restrictions category"
    );
  } else {
    ok(
      hidden("#protections-popup-category-cookies"),
      "Not showing cookie restrictions category"
    );
  }
}

async function testContentBlocking(tab) {
  info("Testing with Tracking Protection ENABLED.");

  info("Load a test page not containing tracking elements");
  await promiseTabLoadEvent(tab, BENIGN_PAGE);
  testBenignPage();

  info(
    "Load a test page not containing tracking elements which has an exception."
  );

  await promiseTabLoadEvent(tab, "https://example.org/?round=1");

  ContentBlockingAllowList.add(tab.linkedBrowser);
  // Load another page from the same origin to ensure there is an onlocationchange
  // notification which would trigger an oncontentblocking notification for us.
  await promiseTabLoadEvent(tab, "https://example.org/?round=2");

  testBenignPageWithException();

  ContentBlockingAllowList.remove(tab.linkedBrowser);

  info("Load a test page containing tracking elements");
  await promiseTabLoadEvent(tab, gTrackingPageURL);
  testTrackingPage(tab.ownerGlobal);

  info("Disable CB for the page (which reloads the page)");
  let tabReloadPromise = promiseTabLoadEvent(tab);
  tab.ownerGlobal.gProtectionsHandler.disableForCurrentPage();
  await tabReloadPromise;
  let isPrivateBrowsing = PrivateBrowsingUtils.isWindowPrivate(tab.ownerGlobal);
  let blockedByTP = areTrackersBlocked(isPrivateBrowsing);
  testTrackingPageUnblocked(blockedByTP, tab.ownerGlobal);

  info("Re-enable TP for the page (which reloads the page)");
  tabReloadPromise = promiseTabLoadEvent(tab);
  tab.ownerGlobal.gProtectionsHandler.enableForCurrentPage();
  await tabReloadPromise;
  testTrackingPage(tab.ownerGlobal);
}

add_task(async function testNormalBrowsing() {
  await UrlClassifierTestUtils.addTestTrackers();

  Services.prefs.setBoolPref(DTSCBN_PREF, true);

  tabbrowser = gBrowser;
  let tab = (tabbrowser.selectedTab = BrowserTestUtils.addTab(tabbrowser));

  gProtectionsHandler = gBrowser.ownerGlobal.gProtectionsHandler;
  ok(
    gProtectionsHandler,
    "gProtectionsHandler is attached to the browser window"
  );
  TrackingProtection = gBrowser.ownerGlobal.TrackingProtection;
  ok(TrackingProtection, "TP is attached to the browser window");
  is(
    TrackingProtection.enabled,
    Services.prefs.getBoolPref(TP_PREF),
    "TP.enabled is based on the original pref value"
  );

  Services.prefs.setIntPref(TPC_PREF, Ci.nsICookieService.BEHAVIOR_ACCEPT);

  await testContentBlocking(tab);

  Services.prefs.setBoolPref(TP_PREF, true);
  ok(TrackingProtection.enabled, "TP is enabled after setting the pref");

  await testContentBlocking(tab);

  gBrowser.removeCurrentTab();

  Services.prefs.clearUserPref(TPC_PREF);
});

add_task(async function testPrivateBrowsing() {
  let privateWin = await BrowserTestUtils.openNewBrowserWindow({
    private: true,
  });
  tabbrowser = privateWin.gBrowser;
  let tab = (tabbrowser.selectedTab = BrowserTestUtils.addTab(tabbrowser));

  // Set the normal mode pref to false to check the pbmode pref.
  Services.prefs.setBoolPref(TP_PREF, false);

  Services.prefs.setIntPref(TPC_PREF, Ci.nsICookieService.BEHAVIOR_ACCEPT);

  gProtectionsHandler = tabbrowser.ownerGlobal.gProtectionsHandler;
  ok(
    gProtectionsHandler,
    "gProtectionsHandler is attached to the private window"
  );
  TrackingProtection = tabbrowser.ownerGlobal.TrackingProtection;
  ok(TrackingProtection, "TP is attached to the private window");
  is(
    TrackingProtection.enabled,
    Services.prefs.getBoolPref(TP_PB_PREF),
    "TP.enabled is based on the pb pref value"
  );

  await testContentBlocking(tab);

  Services.prefs.setBoolPref(TP_PB_PREF, true);
  ok(TrackingProtection.enabled, "TP is enabled after setting the pref");

  await testContentBlocking(tab);

  privateWin.close();

  Services.prefs.clearUserPref(TPC_PREF);
});

add_task(async function testThirdPartyCookies() {
  await UrlClassifierTestUtils.addTestTrackers();
  gTrackingPageURL = COOKIE_PAGE;

  tabbrowser = gBrowser;
  let tab = (tabbrowser.selectedTab = BrowserTestUtils.addTab(tabbrowser));

  gProtectionsHandler = gBrowser.ownerGlobal.gProtectionsHandler;
  ok(
    gProtectionsHandler,
    "gProtectionsHandler is attached to the browser window"
  );
  ThirdPartyCookies = gBrowser.ownerGlobal.ThirdPartyCookies;
  ok(ThirdPartyCookies, "TP is attached to the browser window");
  is(
    ThirdPartyCookies.enabled,
    Services.prefs.getIntPref(TPC_PREF) ==
      Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER,
    "TPC.enabled is based on the original pref value"
  );

  await testContentBlocking(tab);

  Services.prefs.setIntPref(
    TPC_PREF,
    Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER
  );
  ok(ThirdPartyCookies.enabled, "TPC is enabled after setting the pref");

  await testContentBlocking(tab);

  Services.prefs.clearUserPref(TPC_PREF);
  gBrowser.removeCurrentTab();
});
