const DIRECTORY_PATH = "/browser/toolkit/components/passwordmgr/test/browser/";

ChromeUtils.import("resource://gre/modules/LoginHelper.jsm", this);
ChromeUtils.import("resource://testing-common/LoginTestUtils.jsm", this);
ChromeUtils.import("resource://testing-common/ContentTaskUtils.jsm", this);
ChromeUtils.import("resource://testing-common/TelemetryTestUtils.jsm", this);

add_task(async function common_initialize() {
  await SpecialPowers.pushPrefEnv({ set: [["signon.rememberSignons", true]] });
});

registerCleanupFunction(
  async function cleanup_removeAllLoginsAndResetRecipes() {
    await SpecialPowers.popPrefEnv();

    Services.logins.removeAllLogins();
    clearHttpAuths();

    let recipeParent = LoginTestUtils.recipes.getRecipeParent();
    if (!recipeParent) {
      // No need to reset the recipes if the recipe module wasn't even loaded.
      return;
    }
    await recipeParent.then(recipeParentResult => recipeParentResult.reset());
  }
);

/**
 * Loads a test page in `DIRECTORY_URL` which automatically submits to formsubmit.sjs and returns a
 * promise resolving with the field values when the optional `aTaskFn` is done.
 *
 * @param {String} aPageFile - test page file name which auto-submits to formsubmit.sjs
 * @param {Function} aTaskFn - task which can be run before the tab closes.
 * @param {String} [aOrigin="http://example.com"] - origin of the server to use
 *                                                  to load `aPageFile`.
 */
function testSubmittingLoginForm(
  aPageFile,
  aTaskFn,
  aOrigin = "http://example.com"
) {
  return BrowserTestUtils.withNewTab(
    {
      gBrowser,
      url: aOrigin + DIRECTORY_PATH + aPageFile,
    },
    async function(browser) {
      ok(true, "loaded " + aPageFile);
      let fieldValues = await ContentTask.spawn(
        browser,
        undefined,
        async function() {
          await ContentTaskUtils.waitForCondition(() => {
            return (
              content.location.pathname.endsWith("/formsubmit.sjs") &&
              content.document.readyState == "complete"
            );
          }, "Wait for form submission load (formsubmit.sjs)");
          let username = content.document.getElementById("user").textContent;
          let password = content.document.getElementById("pass").textContent;
          return {
            username,
            password,
          };
        }
      );
      ok(true, "form submission loaded");
      if (aTaskFn) {
        await aTaskFn(fieldValues);
      }
      return fieldValues;
    }
  );
}

function checkOnlyLoginWasUsedTwice({ justChanged }) {
  // Check to make sure we updated the timestamps and use count on the
  // existing login that was submitted for the test.
  let logins = Services.logins.getAllLogins();
  is(logins.length, 1, "Should only have 1 login");
  ok(logins[0] instanceof Ci.nsILoginMetaInfo, "metainfo QI");
  is(logins[0].timesUsed, 2, "check .timesUsed for existing login submission");
  ok(logins[0].timeCreated < logins[0].timeLastUsed, "timeLastUsed bumped");
  if (justChanged) {
    is(
      logins[0].timeLastUsed,
      logins[0].timePasswordChanged,
      "timeLastUsed == timePasswordChanged"
    );
  } else {
    is(
      logins[0].timeCreated,
      logins[0].timePasswordChanged,
      "timeChanged not updated"
    );
  }
}

function clearHttpAuths() {
  let authMgr = Cc["@mozilla.org/network/http-auth-manager;1"].getService(
    Ci.nsIHttpAuthManager
  );
  authMgr.clearAll();
}

// Begin popup notification (doorhanger) functions //

const REMEMBER_BUTTON = "button";
const NEVER_MENUITEM = 0;

const CHANGE_BUTTON = "button";
const DONT_CHANGE_BUTTON = "secondaryButton";

/**
 * Checks if we have a password capture popup notification
 * of the right type and with the right label.
 *
 * @param {String} aKind The desired `passwordNotificationType`
 * @param {Object} [popupNotifications = PopupNotifications]
 * @param {Object} [browser = null] Optional browser whose notifications should be searched.
 * @return the found password popup notification.
 */
function getCaptureDoorhanger(
  aKind,
  popupNotifications = PopupNotifications,
  browser = null
) {
  ok(true, "Looking for " + aKind + " popup notification");
  let notification = popupNotifications.getNotification("password", browser);
  if (notification) {
    is(
      notification.options.passwordNotificationType,
      aKind,
      "Notification type matches."
    );
    if (aKind == "password-change") {
      is(
        notification.mainAction.label,
        "Update",
        "Main action label matches update doorhanger."
      );
    } else if (aKind == "password-save") {
      is(
        notification.mainAction.label,
        "Save",
        "Main action label matches save doorhanger."
      );
    }
  }
  return notification;
}

/**
 * Clicks the specified popup notification button.
 *
 * @param {Element} aPopup Popup Notification element
 * @param {Number} aButtonIndex Number indicating which button to click.
 *                              See the constants in this file.
 */
function clickDoorhangerButton(aPopup, aButtonIndex) {
  ok(true, "Looking for action at index " + aButtonIndex);

  let notifications = aPopup.owner.panel.children;
  ok(notifications.length > 0, "at least one notification displayed");
  ok(true, notifications.length + " notification(s)");
  let notification = notifications[0];

  if (aButtonIndex == "button") {
    ok(true, "Triggering main action");
    notification.button.doCommand();
  } else if (aButtonIndex == "secondaryButton") {
    ok(true, "Triggering secondary action");
    notification.secondaryButton.doCommand();
  } else {
    ok(true, "Triggering menuitem # " + aButtonIndex);
    notification.menupopup
      .querySelectorAll("menuitem")
      [aButtonIndex].doCommand();
  }
}

/**
 * Checks the doorhanger's username and password.
 *
 * @param {String} username The username.
 * @param {String} password The password.
 */
async function checkDoorhangerUsernamePassword(username, password) {
  await BrowserTestUtils.waitForCondition(() => {
    return (
      document.getElementById("password-notification-username").value ==
      username
    );
  }, "Wait for nsLoginManagerPrompter writeDataToUI()");
  is(
    document.getElementById("password-notification-username").value,
    username,
    "Check doorhanger username"
  );
  is(
    document.getElementById("password-notification-password").value,
    password,
    "Check doorhanger password"
  );
}

// End popup notification (doorhanger) functions //

async function waitForPasswordManagerDialog() {
  let win;
  await TestUtils.waitForCondition(() => {
    win = Services.wm.getMostRecentWindow("Toolkit:PasswordManager");
    return win && win.document.getElementById("filter");
  }, "Waiting for the password manager dialog to open");

  return win;
}

// Contextmenu functions //

/**
 * Synthesize mouse clicks to open the password manager context menu popup
 * for a target password input element.
 *
 * assertCallback should return true if we should continue or else false.
 */
async function openPasswordContextMenu(
  browser,
  passwordInput,
  assertCallback = null
) {
  const CONTEXT_MENU = document.getElementById("contentAreaContextMenu");
  const POPUP_HEADER = document.getElementById("fill-login");
  const LOGIN_POPUP = document.getElementById("fill-login-popup");

  let contextMenuShownPromise = BrowserTestUtils.waitForEvent(
    CONTEXT_MENU,
    "popupshown"
  );

  // Synthesize a right mouse click over the password input element, we have to trigger
  // both events because formfill code relies on this event happening before the contextmenu
  // (which it does for real user input) in order to not show the password autocomplete.
  let eventDetails = { type: "mousedown", button: 2 };
  await BrowserTestUtils.synthesizeMouseAtCenter(
    passwordInput,
    eventDetails,
    browser
  );
  // Synthesize a contextmenu event to actually open the context menu.
  eventDetails = { type: "contextmenu", button: 2 };
  await BrowserTestUtils.synthesizeMouseAtCenter(
    passwordInput,
    eventDetails,
    browser
  );

  await contextMenuShownPromise;

  if (assertCallback) {
    let shouldContinue = await assertCallback();
    if (!shouldContinue) {
      return;
    }
  }

  // Synthesize a mouse click over the fill login menu header.
  let popupShownPromise = BrowserTestUtils.waitForCondition(
    () => POPUP_HEADER.open && BrowserTestUtils.is_visible(LOGIN_POPUP)
  );
  EventUtils.synthesizeMouseAtCenter(POPUP_HEADER, {});
  await popupShownPromise;
}

/**
 * Use the contextmenu to fill a field with a generated password
 */
async function doFillGeneratedPasswordContextMenuItem(browser, passwordInput) {
  await SimpleTest.promiseFocus(browser);
  await openPasswordContextMenu(browser, passwordInput);

  let loginPopup = document.getElementById("fill-login-popup");
  let generatedPasswordItem = document.getElementById(
    "fill-login-generated-password"
  );
  let generatedPasswordSeparator = document.getElementById(
    "generated-password-separator"
  );

  // Check the content of the password manager popup
  ok(BrowserTestUtils.is_visible(loginPopup), "Popup is visible");
  ok(
    BrowserTestUtils.is_visible(generatedPasswordItem),
    "generated password item is visible"
  );
  ok(
    BrowserTestUtils.is_visible(generatedPasswordSeparator),
    "separator is visible"
  );

  let passwordChangedPromise = ContentTask.spawn(
    browser,
    [passwordInput],
    async function(passwordInput) {
      let input = content.document.querySelector(passwordInput);
      await ContentTaskUtils.waitForEvent(input, "input");
    }
  );

  generatedPasswordItem.doCommand();
  info("Waiting for input event");
  await passwordChangedPromise;
  document.getElementById("contentAreaContextMenu").hidePopup();
}
