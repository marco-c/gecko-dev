const { HttpServer } = ChromeUtils.import("resource://testing-common/httpd.js");

var httpServer = null;
// Need to randomize, because apparently no one clears our cache
var randomPath = "/redirect/" + Math.random();

XPCOMUtils.defineLazyGetter(this, "randomURI", function() {
  return "http://localhost:" + httpServer.identity.primaryPort + randomPath;
});

var cacheUpdateObserver = null;

function make_channel(url, callback, ctx) {
  return NetUtil.newChannel({ uri: url, loadUsingSystemPrincipal: true });
}

function make_uri(url) {
  var ios = Cc["@mozilla.org/network/io-service;1"].getService(Ci.nsIIOService);
  return ios.newURI(url);
}

var responseBody = "Content body";

// start the test with loading this master entry referencing the manifest
function masterEntryHandler(metadata, response) {
  var masterEntryContent = "<html manifest='/manifest'></html>";
  response.setHeader("Content-Type", "text/html");
  response.bodyOutputStream.write(
    masterEntryContent,
    masterEntryContent.length
  );
}

// manifest defines fallback namespace from any /redirect path to /content
function manifestHandler(metadata, response) {
  var manifestContent = "CACHE MANIFEST\nFALLBACK:\nredirect /content\n";
  response.setHeader("Content-Type", "text/cache-manifest");
  response.bodyOutputStream.write(manifestContent, manifestContent.length);
}

// content handler correctly returns some plain text data
function contentHandler(metadata, response) {
  response.setHeader("Content-Type", "text/plain");
  response.bodyOutputStream.write(responseBody, responseBody.length);
}

// redirect handler returns redirect
function redirectHandler(metadata, response) {
  response.setStatusLine(metadata.httpVersion, 301, "Moved");
  response.setHeader("Location", "http://example.com/", false);
}

// finally check we got fallback content
function finish_test(request, buffer) {
  Assert.equal(buffer, "");
  do_test_finished();
}

function run_test() {
  httpServer = new HttpServer();
  httpServer.registerPathHandler("/masterEntry", masterEntryHandler);
  httpServer.registerPathHandler("/manifest", manifestHandler);
  httpServer.registerPathHandler("/content", contentHandler);
  httpServer.start(-1);

  var pm = Cc["@mozilla.org/permissionmanager;1"].getService(
    Ci.nsIPermissionManager
  );
  var ssm = Cc["@mozilla.org/scriptsecuritymanager;1"].getService(
    Ci.nsIScriptSecurityManager
  );
  var uri = make_uri("http://localhost:" + httpServer.identity.primaryPort);
  var principal = ssm.createContentPrincipal(uri, {});

  if (pm.testPermissionFromPrincipal(principal, "offline-app") != 0) {
    dump(
      "Previous test failed to clear offline-app permission!  Expect failures.\n"
    );
  }
  pm.addFromPrincipal(
    principal,
    "offline-app",
    Ci.nsIPermissionManager.ALLOW_ACTION
  );

  var ps = Cc["@mozilla.org/preferences-service;1"].getService(
    Ci.nsIPrefBranch
  );
  dump(ps.getBoolPref("browser.cache.offline.enable"));
  ps.setBoolPref("browser.cache.offline.enable", true);
  ps.setComplexValue(
    "browser.cache.offline.parent_directory",
    Ci.nsIFile,
    do_get_profile()
  );

  cacheUpdateObserver = {
    observe() {
      dump("got offline-cache-update-completed\n");
      // offline cache update completed.

      // doing this to eval the lazy getter, otherwise the make_channel call would hang
      randomURI;
      httpServer.stop(function() {
        // Now shut the server down to have an error in onStartRequest
        var chan = make_channel(randomURI);
        chan.notificationCallbacks = new ChannelEventSink(ES_ABORT_REDIRECT);
        var chanac = chan.QueryInterface(Ci.nsIApplicationCacheChannel);
        chanac.chooseApplicationCache = true;
        chan.asyncOpen(
          new ChannelListener(finish_test, null, CL_EXPECT_FAILURE)
        );
      });
    },
  };

  var os = Cc["@mozilla.org/observer-service;1"].getService(
    Ci.nsIObserverService
  );
  os.addObserver(cacheUpdateObserver, "offline-cache-update-completed");

  var us = Cc["@mozilla.org/offlinecacheupdate-service;1"].getService(
    Ci.nsIOfflineCacheUpdateService
  );
  us.scheduleUpdate(
    make_uri(
      "http://localhost:" + httpServer.identity.primaryPort + "/manifest"
    ),
    make_uri(
      "http://localhost:" + httpServer.identity.primaryPort + "/masterEntry"
    ),
    Services.scriptSecurityManager.getSystemPrincipal(),
    null
  );

  do_test_pending();
}
