/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * For detailed information on our policies, and a documention on this format
 * and its possibilites, please check the Mozilla-Wiki at
 *
 * https://wiki.mozilla.org/Compatibility/Go_Faster_Addon/Override_Policies_and_Workflows#User_Agent_overrides
 */

"use strict";

/* globals filterOverrides, portsToAboutCompatTabs, UAOverrides */

let UAOverridesEnabled = true;

for (const override of [
  {
    id: "testoverride",
    platform: "all",
    domain: "webcompat-addon-testcases.schub.io",
    bug: "1287966",
    config: {
      matches: ["*://webcompat-addon-testcases.schub.io/*"],
      uaTransformer: originalUA => {
        return (
          UAHelpers.getPrefix(originalUA) +
          " AppleWebKit/537.36 (KHTML, like Gecko) Chrome/54.0.2840.98 Safari/537.36"
        );
      },
    },
  },
  {
    /*
     * Bug 1563839 - rolb.santanderbank.com - Build UA override
     * WebCompat issue #33462 - https://webcompat.com/issues/33462
     *
     * santanderbank expects UA to have 'like Gecko', otherwise it runs
     * xmlDoc.onload whose support has been dropped. It results in missing labels in forms
     * and some other issues.  Adding 'like Gecko' fixes those issues.
     */
    id: "bug1563839",
    platform: "all",
    domain: "rolb.santanderbank.com",
    bug: "1563839",
    config: {
      matches: ["*://rolb.santanderbank.com/*"],
      uaTransformer: originalUA => {
        return originalUA.replace("Gecko", "like Gecko");
      },
    },
  },
  {
    /*
     * Bug 1480710 - m.imgur.com - Build UA override
     * WebCompat issue #13154 - https://webcompat.com/issues/13154
     *
     * imgur returns a 404 for requests to CSS and JS file if requested with a Fennec
     * User Agent. By removing the Fennec identifies and adding Chrome Mobile's, we
     * receive the correct CSS and JS files.
     */
    id: "bug1480710",
    platform: "android",
    domain: "m.imgur.com",
    bug: "1480710",
    config: {
      matches: ["*://m.imgur.com/*"],
      uaTransformer: originalUA => {
        return (
          UAHelpers.getPrefix(originalUA) +
          " AppleWebKit/537.36 (KHTML, like Gecko) Chrome/68.0.3440.85 Mobile Safari/537.36"
        );
      },
    },
  },
  {
    /*
     * Bug 945963 - tieba.baidu.com serves simplified mobile content to Firefox Android
     * WebCompat issue #18455 - https://webcompat.com/issues/18455
     *
     * tieba.baidu.com and tiebac.baidu.com serve a heavily simplified and less functional
     * mobile experience to Firefox for Android users. Adding the AppleWebKit indicator
     * to the User Agent gets us the same experience.
     */
    id: "bug945963",
    platform: "android",
    domain: "tieba.baidu.com",
    bug: "945963",
    config: {
      matches: ["*://tieba.baidu.com/*", "*://tiebac.baidu.com/*"],
      uaTransformer: originalUA => {
        return originalUA + " AppleWebKit/537.36 (KHTML, like Gecko)";
      },
    },
  },
  {
    /*
     * Bug 1177298 - Write UA overrides for top Japanese Sites
     * (Imported from ua-update.json.in)
     *
     * To receive the proper mobile version instead of the desktop version or
     * a lower grade mobile experience, the UA is spoofed.
     */
    id: "bug1177298-2",
    platform: "android",
    domain: "lohaco.jp",
    bug: "1177298",
    config: {
      matches: ["*://*.lohaco.jp/*"],
      uaTransformer: _ => {
        return "Mozilla/5.0 (Linux; Android 5.0.2; Galaxy Nexus Build/IMM76B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/43.0.2357.93 Mobile Safari/537.36";
      },
    },
  },
  {
    /*
     * Bug 1177298 - Write UA overrides for top Japanese Sites
     * (Imported from ua-update.json.in)
     *
     * To receive the proper mobile version instead of the desktop version or
     * a lower grade mobile experience, the UA is spoofed.
     */
    id: "bug1177298-3",
    platform: "android",
    domain: "nhk.or.jp",
    bug: "1177298",
    config: {
      matches: ["*://*.nhk.or.jp/*"],
      uaTransformer: originalUA => {
        return originalUA + " AppleWebKit";
      },
    },
  },
  {
    /*
     * Bug 1338260 - Add UA override for directTV
     * (Imported from ua-update.json.in)
     *
     * DirectTV has issues with scrolling and cut-off images. Pretending to be
     * Chrome for Android fixes those issues.
     */
    id: "bug1338260",
    platform: "android",
    domain: "directv.com",
    bug: "1338260",
    config: {
      matches: ["*://*.directv.com/*"],
      uaTransformer: _ => {
        return "Mozilla/5.0 (Linux; Android 6.0.1; SM-G920F Build/MMB29K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.91 Mobile Safari/537.36";
      },
    },
  },
  {
    /*
     * Bug 1385206 - Create UA override for rakuten.co.jp on Firefox Android
     * (Imported from ua-update.json.in)
     *
     * rakuten.co.jp serves a Desktop version if Firefox is included in the UA.
     */
    id: "bug1385206",
    platform: "android",
    domain: "rakuten.co.jp",
    bug: "1385206",
    config: {
      matches: ["*://*.rakuten.co.jp/*"],
      uaTransformer: originalUA => {
        return originalUA.replace(/Firefox.+$/, "");
      },
    },
  },
  {
    /*
     * Bug 969844 - mobile.de sends desktop site to Firefox on Android
     *
     * mobile.de sends the desktop site to Fennec. Spooing as Chrome works fine.
     */
    id: "bug969844",
    platform: "android",
    domain: "mobile.de",
    bug: "969844",
    config: {
      matches: ["*://*.mobile.de/*"],
      uaTransformer: _ => {
        return "Mozilla/5.0 (Linux; Android 6.0.1; SM-G920F Build/MMB29K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.91 Mobile Safari/537.36";
      },
    },
  },
  {
    /*
     * Bug 1509831 - cc.com - Add UA override for CC.com
     * WebCompat issue #329 - https://webcompat.com/issues/329
     *
     * ComedyCentral blocks Firefox for not being able to play HLS, which was
     * true in previous versions, but no longer is. With a spoofed Chrome UA,
     * the site works just fine.
     */
    id: "bug1509831",
    platform: "android",
    domain: "cc.com",
    bug: "1509831",
    config: {
      matches: ["*://*.cc.com/*"],
      uaTransformer: _ => {
        return "Mozilla/5.0 (Linux; Android 6.0.1; SM-G920F Build/MMB29K) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.91 Mobile Safari/537.36";
      },
    },
  },
  {
    /*
     * Bug 1508516 - cineflix.com.br - Add UA override for cineflix.com.br/m/
     * WebCompat issue #21553 - https://webcompat.com/issues/21553
     *
     * The site renders a blank page with any Firefox snipped in the UA as it
     * is running into an exception. Spoofing as Chrome makes the site work
     * fine.
     */
    id: "bug1508516",
    platform: "android",
    domain: "cineflix.com.br",
    bug: "1508516",
    config: {
      matches: ["*://*.cineflix.com.br/m/*"],
      uaTransformer: originalUA => {
        return (
          UAHelpers.getPrefix(originalUA) +
          " AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.91 Mobile Safari/537.36"
        );
      },
    },
  },
  {
    /*
     * Bug 1509852 - redbull.com - Add UA override for redbull.com
     * WebCompat issue #21439 - https://webcompat.com/issues/21439
     *
     * Redbull.com blocks some features, for example the live video player, for
     * Fennec. Spoofing as Chrome results in us rendering the video just fine,
     * and everything else works as well.
     */
    id: "bug1509852",
    platform: "android",
    domain: "redbull.com",
    bug: "1509852",
    config: {
      matches: ["*://*.redbull.com/*"],
      uaTransformer: originalUA => {
        return (
          UAHelpers.getPrefix(originalUA) +
          " AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.91 Mobile Safari/537.36"
        );
      },
    },
  },
  {
    /*
     * Bug 1509873 - zmags.com - Add UA override for secure.viewer.zmags.com
     * WebCompat issue #21576 - https://webcompat.com/issues/21576
     *
     * The zmags viewer locks out Fennec with a "Browser unsupported" message,
     * but tests showed that it works just fine with a Chrome UA. Outreach
     * attempts were unsuccessful, and as the site has a relatively high rank,
     * we alter the UA.
     */
    id: "bug1509873",
    platform: "android",
    domain: "zmags.com",
    bug: "1509873",
    config: {
      matches: ["*://*.viewer.zmags.com/*"],
      uaTransformer: originalUA => {
        return (
          UAHelpers.getPrefix(originalUA) +
          " AppleWebKit/537.36 (KHTML, like Gecko) Chrome/55.0.2883.91 Mobile Safari/537.36"
        );
      },
    },
  },
]) {
  UAOverrides.push(override);
}

/* globals browser */

const UAHelpers = {
  getPrefix(originalUA) {
    return originalUA.substr(0, originalUA.indexOf(")") + 1);
  },
};

const ActiveListeners = new Map();

function enableOverride(override) {
  if (override.active) {
    return;
  }

  const { matches, uaTransformer } = override.config;
  const listener = details => {
    for (var header of details.requestHeaders) {
      if (header.name.toLowerCase() === "user-agent") {
        header.value = uaTransformer(header.value);
      }
    }
    return { requestHeaders: details.requestHeaders };
  };

  browser.webRequest.onBeforeSendHeaders.addListener(
    listener,
    { urls: matches },
    ["blocking", "requestHeaders"]
  );

  ActiveListeners.set(override, listener);
  override.active = true;
}

async function registerUAOverrides() {
  const platformMatches = ["all"];
  let platformInfo = await browser.runtime.getPlatformInfo();
  platformMatches.push(platformInfo.os == "android" ? "android" : "desktop");

  for (const override of UAOverrides) {
    if (platformMatches.includes(override.platform)) {
      override.availableOnPlatform = true;
      enableOverride(override);
    }
  }
  UAOverridesEnabled = true;
  portsToAboutCompatTabs.broadcast({
    overridesChanged: filterOverrides(UAOverrides),
  });
}

function unregisterUAOverrides() {
  for (const override of UAOverrides) {
    disableOverride(override);
  }
  UAOverridesEnabled = false;
  portsToAboutCompatTabs.broadcast({ overridesChanged: false });
}

function disableOverride(override) {
  if (!override.active) {
    return;
  }

  browser.webRequest.onBeforeSendHeaders.removeListener(
    ActiveListeners.get(override)
  );
  override.active = false;
  ActiveListeners.delete(override);
}

const OVERRIDE_PREF = "perform_ua_overrides";
function checkOverridePref() {
  browser.aboutConfigPrefs.getPref(OVERRIDE_PREF).then(value => {
    if (value === undefined) {
      browser.aboutConfigPrefs.setPref(OVERRIDE_PREF, true);
    } else if (value === false) {
      unregisterUAOverrides();
    } else {
      registerUAOverrides();
    }
  });
}
browser.aboutConfigPrefs.onPrefChange.addListener(
  checkOverridePref,
  OVERRIDE_PREF
);
checkOverridePref();
