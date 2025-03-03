/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/* eslint-env mozilla/frame-script */

const formatter = new Intl.DateTimeFormat("default");

// The following parameters are parsed from the error URL:
//   e - the error code
//   s - custom CSS class to allow alternate styling/favicons
//   d - error description
//   captive - "true" to indicate we're behind a captive portal.
//             Any other value is ignored.

// Note that this file uses document.documentURI to get
// the URL (with the format from above). This is because
// document.location.href gets the current URI off the docshell,
// which is the URL displayed in the location bar, i.e.
// the URI that the user attempted to load.

let searchParams = new URLSearchParams(document.documentURI.split("?")[1]);

// Set to true on init if the error code is nssBadCert.
let gIsCertError;

function getErrorCode() {
  return searchParams.get("e");
}

function getCSSClass() {
  return searchParams.get("s");
}

function getDescription() {
  return searchParams.get("d");
}

function isCaptive() {
  return searchParams.get("captive") == "true";
}

function retryThis(buttonEl) {
  // Note: The application may wish to handle switching off "offline mode"
  // before this event handler runs, but using a capturing event handler.

  // Session history has the URL of the page that failed
  // to load, not the one of the error page. So, just call
  // reload(), which will also repost POST data correctly.
  try {
    location.reload();
  } catch (e) {
    // We probably tried to reload a URI that caused an exception to
    // occur;  e.g. a nonexistent file.
  }

  buttonEl.disabled = true;
}

function toggleDisplay(node) {
  const toggle = {
    "": "block",
    none: "block",
    block: "none",
  };
  return (node.style.display = toggle[node.style.display]);
}

function showCertificateErrorReporting() {
  // Display error reporting UI
  document.getElementById("certificateErrorReporting").style.display = "block";
}

function showPrefChangeContainer() {
  const panel = document.getElementById("prefChangeContainer");
  panel.style.display = "block";
  document.getElementById("netErrorButtonContainer").style.display = "none";
  document
    .getElementById("prefResetButton")
    .addEventListener("click", function resetPreferences(e) {
      const event = new CustomEvent("AboutNetErrorResetPreferences", {
        bubbles: true,
      });
      document.dispatchEvent(event);
    });
  addAutofocus("#prefResetButton", "beforeend");
}

function setupAdvancedButton() {
  // Get the hostname and add it to the panel
  var panel = document.getElementById("badCertAdvancedPanel");
  for (var span of panel.querySelectorAll("span.hostname")) {
    span.textContent = document.location.hostname;
  }

  // Register click handler for the weakCryptoAdvancedPanel
  document
    .getElementById("advancedButton")
    .addEventListener("click", togglePanelVisibility);

  function togglePanelVisibility() {
    toggleDisplay(panel);
    if (gIsCertError) {
      // Toggling the advanced panel must ensure that the debugging
      // information panel is hidden as well, since it's opened by the
      // error code link in the advanced panel.
      var div = document.getElementById("certificateErrorDebugInformation");
      div.style.display = "none";
    }

    if (panel.style.display == "block") {
      // send event to trigger telemetry ping
      var event = new CustomEvent("AboutNetErrorUIExpanded", { bubbles: true });
      document.dispatchEvent(event);
    }
  }

  if (!gIsCertError) {
    return;
  }

  if (getCSSClass() == "expertBadCert") {
    toggleDisplay(document.getElementById("badCertAdvancedPanel"));
    // Toggling the advanced panel must ensure that the debugging
    // information panel is hidden as well, since it's opened by the
    // error code link in the advanced panel.
    var div = document.getElementById("certificateErrorDebugInformation");
    div.style.display = "none";
  }

  disallowCertOverridesIfNeeded();
}

function disallowCertOverridesIfNeeded() {
  var cssClass = getCSSClass();
  // Disallow overrides if this is a Strict-Transport-Security
  // host and the cert is bad (STS Spec section 7.3) or if the
  // certerror is in a frame (bug 633691).
  if (cssClass == "badStsCert" || window != top) {
    document
      .getElementById("exceptionDialogButton")
      .setAttribute("hidden", "true");
  }
  if (cssClass == "badStsCert") {
    document.getElementById("badStsCertExplanation").removeAttribute("hidden");

    let stsReturnButtonText = document.getElementById("stsReturnButtonText")
      .textContent;
    document.getElementById("returnButton").textContent = stsReturnButtonText;
    document.getElementById(
      "advancedPanelReturnButton"
    ).textContent = stsReturnButtonText;

    let stsMitmWhatCanYouDoAboutIt3 = document.getElementById(
      "stsMitmWhatCanYouDoAboutIt3"
    ).innerHTML;
    // eslint-disable-next-line no-unsanitized/property
    document.getElementById(
      "mitmWhatCanYouDoAboutIt3"
    ).innerHTML = stsMitmWhatCanYouDoAboutIt3;
  }
}

function initPage() {
  var err = getErrorCode();
  // List of error pages with an illustration.
  let illustratedErrors = [
    "malformedURI",
    "dnsNotFound",
    "connectionFailure",
    "netInterrupt",
    "netTimeout",
    "netReset",
    "netOffline",
  ];
  if (illustratedErrors.includes(err)) {
    document.body.classList.add("illustrated", err);
  }
  if (err == "blockedByPolicy") {
    document.body.classList.add("blocked");
  }

  gIsCertError = err == "nssBadCert";
  // Only worry about captive portals if this is a cert error.
  let showCaptivePortalUI = isCaptive() && gIsCertError;
  if (showCaptivePortalUI) {
    err = "captivePortal";
  }

  let l10nErrId = err;
  let className = getCSSClass();
  if (className) {
    document.body.classList.add(className);
  }

  if (gIsCertError && (window !== window.top || className == "badStsCert")) {
    l10nErrId += "_sts";
  }

  let pageTitle = document.getElementById("ept_" + l10nErrId);
  if (pageTitle) {
    document.title = pageTitle.textContent;
  }

  // if it's an unknown error or there's no title or description
  // defined, get the generic message
  var errTitle = document.getElementById("et_" + l10nErrId);
  var errDesc = document.getElementById("ed_" + l10nErrId);
  if (!errTitle || !errDesc) {
    errTitle = document.getElementById("et_generic");
    errDesc = document.getElementById("ed_generic");
  }

  // eslint-disable-next-line no-unsanitized/property
  document.querySelector(".title-text").innerHTML = errTitle.innerHTML;

  var sd = document.getElementById("errorShortDescText");
  if (sd) {
    if (gIsCertError) {
      // eslint-disable-next-line no-unsanitized/property
      sd.innerHTML = errDesc.innerHTML;
    } else {
      sd.textContent = getDescription();
    }
  }
  if (showCaptivePortalUI) {
    initPageCaptivePortal();
    return;
  }
  if (gIsCertError) {
    initPageCertError();
    updateContainerPosition();
    return;
  }
  addAutofocus("#netErrorButtonContainer > .try-again");

  document.body.classList.add("neterror");

  var ld = document.getElementById("errorLongDesc");
  if (ld) {
    // eslint-disable-next-line no-unsanitized/property
    ld.innerHTML = errDesc.innerHTML;
  }

  if (err == "sslv3Used") {
    document.getElementById("learnMoreContainer").style.display = "block";
    document.body.className = "certerror";
  }

  // remove undisplayed errors to avoid bug 39098
  var errContainer = document.getElementById("errorContainer");
  errContainer.remove();

  if (err == "remoteXUL") {
    // Remove the "Try again" button for remote XUL errors given that
    // it is useless.
    document.getElementById("netErrorButtonContainer").style.display = "none";
  }

  if (err == "cspBlocked") {
    // Remove the "Try again" button for CSP violations, since it's
    // almost certainly useless. (Bug 553180)
    document.getElementById("netErrorButtonContainer").style.display = "none";
  }

  window.addEventListener(
    "AboutNetErrorOptions",
    function(evt) {
      // Pinning errors are of type nssFailure2
      if (getErrorCode() == "nssFailure2") {
        let shortDesc = document.getElementById("errorShortDescText")
          .textContent;
        document.getElementById("learnMoreContainer").style.display = "block";
        var options = JSON.parse(evt.detail);
        if (options && options.enabled) {
          var checkbox = document.getElementById("automaticallyReportInFuture");
          showCertificateErrorReporting();
          if (options.automatic) {
            // set the checkbox
            checkbox.checked = true;
          }

          checkbox.addEventListener("change", function(changeEvt) {
            var event = new CustomEvent("AboutNetErrorSetAutomatic", {
              bubbles: true,
              detail: changeEvt.target.checked,
            });
            document.dispatchEvent(event);
          });
        }
        const hasPrefStyleError = [
          "interrupted", // This happens with subresources that are above the max tls
          "SSL_ERROR_PROTOCOL_VERSION_ALERT",
          "SSL_ERROR_UNSUPPORTED_VERSION",
          "SSL_ERROR_NO_CYPHER_OVERLAP",
          "SSL_ERROR_NO_CIPHERS_SUPPORTED",
        ].some(substring => shortDesc.includes(substring));
        // If it looks like an error that is user config based
        if (
          getErrorCode() == "nssFailure2" &&
          hasPrefStyleError &&
          options &&
          options.changedCertPrefs
        ) {
          showPrefChangeContainer();
        }
      }
      if (getErrorCode() == "sslv3Used") {
        document.getElementById("advancedButton").style.display = "none";
      }
    },
    true,
    true
  );

  var event = new CustomEvent("AboutNetErrorLoad", { bubbles: true });
  document.dispatchEvent(event);

  if (err == "inadequateSecurityError" || err == "blockedByPolicy") {
    // Remove the "Try again" button from pages that don't need it.
    // For HTTP/2 inadequate security or pages blocked by policy, trying
    // again won't help.
    document.getElementById("netErrorButtonContainer").style.display = "none";

    var container = document.getElementById("errorLongDesc");
    for (var span of container.querySelectorAll("span.hostname")) {
      span.textContent = document.location.hostname;
    }
  }
}

// This function centers the error container after its content updates.
// It is currently duplicated in NetErrorChild.jsm to avoid having to do
// async communication to the page that would result in flicker.
// TODO(johannh): Get rid of this duplication.
function updateContainerPosition() {
  let textContainer = document.getElementById("text-container");
  // Using the vh CSS property our margin adapts nicely to window size changes.
  // Unfortunately, this doesn't work correctly in iframes, which is why we need
  // to manually compute the height there.
  if (window.parent == window) {
    textContainer.style.marginTop = `calc(50vh - ${textContainer.clientHeight /
      2}px)`;
  } else {
    let offset =
      document.documentElement.clientHeight / 2 -
      textContainer.clientHeight / 2;
    if (offset > 0) {
      textContainer.style.marginTop = `${offset}px`;
    }
  }
}

function initPageCaptivePortal() {
  document.body.className = "captiveportal";
  document
    .getElementById("openPortalLoginPageButton")
    .addEventListener("click", () => {
      RPMSendAsyncMessage("Browser:OpenCaptivePortalPage");
    });

  addAutofocus("#openPortalLoginPageButton");
  setupAdvancedButton();

  // When the portal is freed, an event is sent by the parent process
  // that we can pick up and attempt to reload the original page.
  RPMAddMessageListener("AboutNetErrorCaptivePortalFreed", () => {
    document.location.reload();
  });
}

function initPageCertError() {
  document.body.classList.add("certerror");
  for (let host of document.querySelectorAll(".hostname")) {
    host.textContent = document.location.hostname;
  }

  addAutofocus("#returnButton");
  setupAdvancedButton();

  document.getElementById("learnMoreContainer").style.display = "block";

  let checkbox = document.getElementById("automaticallyReportInFuture");
  checkbox.addEventListener("change", function({ target: { checked } }) {
    document.dispatchEvent(
      new CustomEvent("AboutNetErrorSetAutomatic", {
        detail: checked,
        bubbles: true,
      })
    );
  });

  let errorReportingEnabled = RPMGetBoolPref(
    "security.ssl.errorReporting.enabled"
  );
  if (errorReportingEnabled) {
    document.getElementById("certificateErrorReporting").style.display =
      "block";
    let errorReportingAutomatic = RPMGetBoolPref(
      "security.ssl.errorReporting.automatic"
    );
    checkbox.checked = !!errorReportingAutomatic;
  }
  let hideAddExceptionButton = RPMGetBoolPref(
    "security.certerror.hideAddException"
  );
  if (hideAddExceptionButton) {
    document.querySelector(".exceptionDialogButtonContainer").hidden = true;
  }

  let failedCertInfo = document.getFailedCertSecurityInfo();
  RPMSendAsyncMessage("RecordCertErrorLoad", {
    // Telemetry values for events are max. 80 bytes.
    errorCode: failedCertInfo.errorCodeString.substring(0, 40),
    has_sts: getCSSClass() == "badStsCert",
    is_frame: window.parent != window,
  });

  let certErrorButtons = ["advancedButton", "copyToClipboard"];
  for (let button of certErrorButtons) {
    let elem = document.getElementById(button);
    elem.addEventListener("click", onClickHandler);
  }

  setCertErrorDetails();
  setTechnicalDetailsOnCertError();

  // Dispatch this event only for tests.
  let event = new CustomEvent("AboutNetErrorLoad", { bubbles: true });
  document.dispatchEvent(event);
}

async function onClickHandler(e) {
  switch (e.target.id) {
    case "advancedButton":
      setCertErrorDetails();
      break;
    case "copyToClipboard":
      let details = await getCertErrorInfo();
      navigator.clipboard.writeText(details);
      break;
  }
}

async function getCertErrorInfo() {
  let location = document.location.href;
  let failedCertInfo = document.getFailedCertSecurityInfo();
  let errorMessage = failedCertInfo.errorMessage;
  let hasHSTS = failedCertInfo.hasHSTS.toString();
  let hasHPKP = failedCertInfo.hasHPKP.toString();
  let [hstsLabel] = await document.l10n.formatValues([
    { id: "cert-error-details-hsts-label", args: { hasHSTS } },
  ]);
  let [hpkpLabel] = await document.l10n.formatValues([
    { id: "cert-error-details-key-pinning-label", args: { hasHPKP } },
  ]);

  let certStrings = failedCertInfo.certChainStrings;
  let failedChainCertificates = "";
  for (let der64 of certStrings) {
    let wrapped = der64.replace(/(\S{64}(?!$))/g, "$1\r\n");
    failedChainCertificates +=
      "-----BEGIN CERTIFICATE-----\r\n" +
      wrapped +
      "\r\n-----END CERTIFICATE-----\r\n";
  }
  let [failedChainLabel] = await document.l10n.formatValues([
    { id: "cert-error-details-cert-chain-label" },
  ]);

  let details =
    location +
    "\r\n\r\n" +
    errorMessage +
    "\r\n\r\n" +
    hstsLabel +
    "\r\n" +
    hpkpLabel +
    "\r\n\r\n" +
    failedChainLabel +
    "\r\n\r\n" +
    failedChainCertificates;
  return details;
}

async function setCertErrorDetails(event) {
  // Check if the connection is being man-in-the-middled. When the parent
  // detects an intercepted connection, the page may be reloaded with a new
  // error code (MOZILLA_PKIX_ERROR_MITM_DETECTED).
  let failedCertInfo = document.getFailedCertSecurityInfo();
  let mitmPrimingEnabled = RPMGetBoolPref(
    "security.certerrors.mitm.priming.enabled"
  );
  if (
    mitmPrimingEnabled &&
    failedCertInfo.errorCodeString == "SEC_ERROR_UNKNOWN_ISSUER" &&
    // Only do this check for top-level failures.
    window.parent == window
  ) {
    RPMSendAsyncMessage("Browser:PrimeMitm");
  }

  let div = document.getElementById("certificateErrorText");
  div.textContent = await getCertErrorInfo();
  let learnMoreLink = document.getElementById("learnMoreLink");
  let baseURL = RPMGetFormatURLPref("app.support.baseURL");
  learnMoreLink.setAttribute("href", baseURL + "connection-not-secure");
  let errWhatToDo = document.getElementById(
    "es_nssBadCert_" + failedCertInfo.errorCodeString
  );
  let es = document.getElementById("errorWhatToDoText");
  let errWhatToDoTitle = document.getElementById("edd_nssBadCert");
  let est = document.getElementById("errorWhatToDoTitleText");
  let error = getErrorCode();

  if (error == "sslv3Used") {
    learnMoreLink.setAttribute("href", baseURL + "sslv3-error-messages");
  }

  if (error == "nssFailure2") {
    let shortDesc = document.getElementById("errorShortDescText").textContent;
    // nssFailure2 also gets us other non-overrideable errors. Choose
    // a "learn more" link based on description:
    if (shortDesc.includes("MOZILLA_PKIX_ERROR_KEY_PINNING_FAILURE")) {
      learnMoreLink.setAttribute(
        "href",
        baseURL + "certificate-pinning-reports"
      );
    }
  }

  // This is set to true later if the user's system clock is at fault for this error.
  let clockSkew = false;
  document.body.setAttribute("code", failedCertInfo.errorCodeString);

  let desc;
  switch (failedCertInfo.errorCodeString) {
    case "SSL_ERROR_BAD_CERT_DOMAIN":
    case "SEC_ERROR_OCSP_INVALID_SIGNING_CERT":
    case "SEC_ERROR_UNKNOWN_ISSUER":
      if (es) {
        // eslint-disable-next-line no-unsanitized/property
        es.innerHTML = errWhatToDo.innerHTML;
      }
      if (est) {
        // eslint-disable-next-line no-unsanitized/property
        est.innerHTML = errWhatToDoTitle.innerHTML;
      }
      updateContainerPosition();
      break;

    // This error code currently only exists for the Symantec distrust
    // in Firefox 63, so we add copy explaining that to the user.
    // In case of future distrusts of that scale we might need to add
    // additional parameters that allow us to identify the affected party
    // without replicating the complex logic from certverifier code.
    case "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED":
      desc = document.getElementById("errorShortDescText2");
      let hostname = document.location.hostname;
      document.l10n.setAttributes(
        desc,
        "cert-error-symantec-distrust-description",
        {
          hostname,
        }
      );

      let adminDesc = document.createElement("p");
      document.l10n.setAttributes(
        adminDesc,
        "cert-error-symantec-distrust-admin"
      );

      learnMoreLink.href = baseURL + "symantec-warning";
      updateContainerPosition();
      break;

    case "MOZILLA_PKIX_ERROR_MITM_DETECTED":
      let autoEnabledEnterpriseRoots = RPMGetBoolPref(
        "security.enterprise_roots.auto-enabled"
      );
      if (mitmPrimingEnabled && autoEnabledEnterpriseRoots) {
        RPMSendAsyncMessage("Browser:ResetEnterpriseRootsPref");
      }

      // We don't actually know what the MitM is called (since we don't
      // maintain a list), so we'll try and display the common name of the
      // root issuer to the user. In the worst case they are as clueless as
      // before, in the best case this gives them an actionable hint.
      // This may be revised in the future.
      let names = document.querySelectorAll(".mitm-name");
      for (let span of names) {
        span.textContent = failedCertInfo.issuerCommonName;
      }

      learnMoreLink.href = baseURL + "security-error";

      let title = document.getElementById("et_mitm");
      desc = document.getElementById("ed_mitm");
      document.querySelector(".title-text").textContent = title.textContent;
      // eslint-disable-next-line no-unsanitized/property
      document.getElementById("errorShortDescText").innerHTML = desc.innerHTML;

      // eslint-disable-next-line no-unsanitized/property
      es.innerHTML = errWhatToDo.innerHTML;
      // eslint-disable-next-line no-unsanitized/property
      est.innerHTML = errWhatToDoTitle.innerHTML;

      updateContainerPosition();
      break;

    case "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT":
      learnMoreLink.href = baseURL + "security-error";
      break;

    // In case the certificate expired we make sure the system clock
    // matches the remote-settings service (blocklist via Kinto) ping time
    // and is not before the build date.
    case "SEC_ERROR_EXPIRED_CERTIFICATE":
    case "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE":
    case "MOZILLA_PKIX_ERROR_NOT_YET_VALID_CERTIFICATE":
    case "MOZILLA_PKIX_ERROR_NOT_YET_VALID_ISSUER_CERTIFICATE":
      learnMoreLink.href = baseURL + "time-errors";
      // We check against the remote-settings server time first if available, because that allows us
      // to give the user an approximation of what the correct time is.
      let difference = RPMGetIntPref("services.settings.clock_skew_seconds");
      let lastFetched =
        RPMGetIntPref("services.settings.last_update_seconds") * 1000;

      let now = Date.now();
      let certRange = {
        notBefore: failedCertInfo.certValidityRangeNotBefore,
        notAfter: failedCertInfo.certValidityRangeNotAfter,
      };
      let approximateDate = now - difference * 1000;
      // If the difference is more than a day, we last fetched the date in the last 5 days,
      // and adjusting the date per the interval would make the cert valid, warn the user:
      if (
        Math.abs(difference) > 60 * 60 * 24 &&
        now - lastFetched <= 60 * 60 * 24 * 5 &&
        certRange.notBefore < approximateDate &&
        certRange.notAfter > approximateDate
      ) {
        clockSkew = true;
        // If there is no clock skew with Kinto servers, check against the build date.
        // (The Kinto ping could have happened when the time was still right, or not at all)
      } else {
        let appBuildID = RPMGetAppBuildID();
        let year = parseInt(appBuildID.substr(0, 4), 10);
        let month = parseInt(appBuildID.substr(4, 2), 10) - 1;
        let day = parseInt(appBuildID.substr(6, 2), 10);

        let buildDate = new Date(year, month, day);
        let systemDate = new Date();

        // We don't check the notBefore of the cert with the build date,
        // as it is of course almost certain that it is now later than the build date,
        // so we shouldn't exclude the possibility that the cert has become valid
        // since the build date.
        if (
          buildDate > systemDate &&
          new Date(certRange.notAfter) > buildDate
        ) {
          clockSkew = true;
        }
      }

      let systemDate = formatter.format(new Date());
      document.getElementById(
        "wrongSystemTime_systemDate1"
      ).textContent = systemDate;
      if (clockSkew) {
        document.body.classList.add("illustrated", "clockSkewError");
        let clockErrTitle = document.getElementById("et_clockSkewError");
        let clockErrDesc = document.getElementById("ed_clockSkewError");
        // eslint-disable-next-line no-unsanitized/property
        document.querySelector(".title-text").textContent =
          clockErrTitle.textContent;
        desc = document.getElementById("errorShortDescText");
        document.getElementById("errorShortDesc").style.display = "block";
        document.getElementById("certificateErrorReporting").style.display =
          "none";
        if (desc) {
          // eslint-disable-next-line no-unsanitized/property
          desc.innerHTML = clockErrDesc.innerHTML;
        }
        let errorPageContainer = document.getElementById("errorPageContainer");
        let textContainer = document.getElementById("text-container");
        errorPageContainer.style.backgroundPosition = `left top calc(50vh - ${textContainer.clientHeight /
          2}px)`;
      } else {
        let targetElems = document.querySelectorAll(
          "#wrongSystemTime_systemDate2"
        );
        for (let elem of targetElems) {
          elem.textContent = systemDate;
        }

        let errDesc = document.getElementById(
          "ed_nssBadCert_SEC_ERROR_EXPIRED_CERTIFICATE"
        );
        let sd = document.getElementById("errorShortDescText");
        // eslint-disable-next-line no-unsanitized/property
        sd.innerHTML = errDesc.innerHTML;

        let span = sd.querySelector(".hostname");
        span.textContent = document.location.hostname;

        // The secondary description mentions expired certificates explicitly
        // and should only be shown if the certificate has actually expired
        // instead of being not yet valid.
        if (failedCertInfo.errorCodeString == "SEC_ERROR_EXPIRED_CERTIFICATE") {
          let cssClass = getCSSClass();
          let stsSuffix = cssClass == "badStsCert" ? "_sts" : "";
          let errDesc2 = document.getElementById(
            `ed2_nssBadCert_SEC_ERROR_EXPIRED_CERTIFICATE${stsSuffix}`
          );
          let sd2 = document.getElementById("errorShortDescText2");
          // eslint-disable-next-line no-unsanitized/property
          sd2.innerHTML = errDesc2.innerHTML;
        }

        if (es) {
          // eslint-disable-next-line no-unsanitized/property
          es.innerHTML = errWhatToDo.innerHTML;
        }
        if (est) {
          // eslint-disable-next-line no-unsanitized/property
          est.textContent = errWhatToDoTitle.textContent;
          est.style.fontWeight = "bold";
        }
        updateContainerPosition();
      }
      break;
  }

  // Add slightly more alarming UI unless there are indicators that
  // show that the error is harmless or can not be skipped anyway.
  let cssClass = getCSSClass();
  // Don't alarm users when they can't continue to the website anyway...
  if (
    cssClass != "badStsCert" &&
    // Errors in iframes can't be skipped either...
    window.parent == window &&
    // Also don't bother if it's just the user's clock being off...
    !clockSkew &&
    // Symantec distrust is likely harmless as well.
    failedCertInfo.erroCodeString !=
      "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED"
  ) {
    document.body.classList.add("caution");
  }
}

function setTechnicalDetailsOnCertError() {
  let technicalInfo = document.getElementById("badCertTechnicalInfo");

  function setL10NLabel(l10nId, args = {}, attrs = {}, rewrite = true) {
    let elem = document.createElement("label");
    if (rewrite) {
      technicalInfo.textContent = "";
    }
    technicalInfo.appendChild(elem);

    let newLines = document.createTextNode("\n \n");
    technicalInfo.appendChild(newLines);

    if (attrs) {
      let link = document.createElement("a");
      for (let attr of Object.keys(attrs)) {
        link.setAttribute(attr, attrs[attr]);
      }
      elem.appendChild(link);
    }

    if (args) {
      document.l10n.setAttributes(elem, l10nId, args);
    } else {
      document.l10n.setAttributes(elem, l10nId);
    }
  }

  let cssClass = getCSSClass();
  let error = getErrorCode();

  let hostString = document.location.hostname;
  let port = document.location.port;
  if (port && port != 443) {
    hostString += ":" + port;
  }

  let l10nId;
  let args = {
    hostname: hostString,
  };
  let failedCertInfo = document.getFailedCertSecurityInfo();
  if (failedCertInfo.isUntrusted) {
    switch (failedCertInfo.errorCodeString) {
      case "MOZILLA_PKIX_ERROR_MITM_DETECTED":
        setL10NLabel("cert-error-mitm-intro");
        setL10NLabel("cert-error-mitm-mozilla", {}, {}, false);
        setL10NLabel("cert-error-mitm-connection", {}, {}, false);
        break;
      case "SEC_ERROR_UNKNOWN_ISSUER":
        setL10NLabel("cert-error-trust-unknown-issuer-intro");
        setL10NLabel("cert-error-trust-unknown-issuer", args, {}, false);
        break;
      case "SEC_ERROR_CA_CERT_INVALID":
        setL10NLabel("cert-error-intro", args);
        setL10NLabel("cert-error-trust-cert-invalid", {}, {}, false);
        break;
      case "SEC_ERROR_UNTRUSTED_ISSUER":
        setL10NLabel("cert-error-intro", args);
        setL10NLabel("cert-error-trust-untrusted-issuer", {}, {}, false);
        break;
      case "SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED":
        setL10NLabel("cert-error-intro", args);
        setL10NLabel(
          "cert-error-trust-signature-algorithm-disabled",
          {},
          {},
          false
        );
        break;
      case "SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE":
        setL10NLabel("cert-error-intro", args);
        setL10NLabel("cert-error-trust-expired-issuer", {}, {}, false);
        break;
      case "MOZILLA_PKIX_ERROR_SELF_SIGNED_CERT":
        setL10NLabel("cert-error-intro", args);
        setL10NLabel("cert-error-trust-self-signed", {}, {}, false);
        break;
      case "MOZILLA_PKIX_ERROR_ADDITIONAL_POLICY_CONSTRAINT_FAILED":
        setL10NLabel("cert-error-intro", args);
        setL10NLabel("cert-error-trust-symantec", {}, {}, false);
        break;
      default:
        setL10NLabel("cert-error-intro", args);
        setL10NLabel("cert-error-untrusted-default", {}, {}, false);
    }
  }

  if (failedCertInfo.isDomainMismatch) {
    let subjectAltNames = failedCertInfo.subjectAltNames.split(",");
    subjectAltNames = subjectAltNames.filter(name => name.length > 0);
    let numSubjectAltNames = subjectAltNames.length;

    if (numSubjectAltNames != 0) {
      if (numSubjectAltNames == 1) {
        args["alt-name"] = subjectAltNames[0];

        // Let's check if we want to make this a link.
        let okHost = failedCertInfo.subjectAltNames;
        let href = "";
        let thisHost = document.location.hostname;
        let proto = document.location.protocol + "//";
        // If okHost is a wildcard domain ("*.example.com") let's
        // use "www" instead.  "*.example.com" isn't going to
        // get anyone anywhere useful. bug 432491
        okHost = okHost.replace(/^\*\./, "www.");
        /* case #1:
         * example.com uses an invalid security certificate.
         *
         * The certificate is only valid for www.example.com
         *
         * Make sure to include the "." ahead of thisHost so that
         * a MitM attack on paypal.com doesn't hyperlink to "notpaypal.com"
         *
         * We'd normally just use a RegExp here except that we lack a
         * library function to escape them properly (bug 248062), and
         * domain names are famous for having '.' characters in them,
         * which would allow spurious and possibly hostile matches.
         */

        if (okHost.endsWith("." + thisHost)) {
          href = proto + okHost;
        }
        /* case #2:
         * browser.garage.maemo.org uses an invalid security certificate.
         *
         * The certificate is only valid for garage.maemo.org
         */
        if (thisHost.endsWith("." + okHost)) {
          href = proto + okHost;
        }

        // If we set a link, meaning there's something helpful for
        // the user here, expand the section by default
        if (href && cssClass != "expertBadCert") {
          document.getElementById("badCertAdvancedPanel").style.display =
            "block";
          if (error == "nssBadCert") {
            // Toggling the advanced panel must ensure that the debugging
            // information panel is hidden as well, since it's opened by the
            // error code link in the advanced panel.
            let div = document.getElementById(
              "certificateErrorDebugInformation"
            );
            div.style.display = "none";
          }
        }

        // Set the link if we want it.
        if (href) {
          setL10NLabel("cert-error-domain-mismatch-single", args, {
            href,
            "data-l10n-name": "domain-mismatch-link",
            id: "cert_domain_link",
          });
        } else {
          setL10NLabel("cert-error-domain-mismatch-single-nolink", args);
        }
      } else {
        let names = subjectAltNames.join(", ");
        args["subject-alt-names"] = names;
        setL10NLabel("cert-error-domain-mismatch-multiple", args);
      }
    } else {
      setL10NLabel("cert-error-domain-mismatch", { hostname: hostString });
    }
  }

  if (failedCertInfo.isNotValidAtThisTime) {
    let notBefore = failedCertInfo.validNotBefore;
    let notAfter = failedCertInfo.validNotAfter;
    args = {
      hostname: hostString,
    };
    if (notBefore && Date.now() < notAfter) {
      let notBeforeLocalTime = formatter.format(new Date(notBefore));
      l10nId = "cert-error-not-yet-valid-now";
      args["not-before-local-time"] = notBeforeLocalTime;
    } else {
      let notAfterLocalTime = formatter.format(new Date(notAfter));
      l10nId = "cert-error-expired-now";
      args["not-after-local-time"] = notAfterLocalTime;
    }
    setL10NLabel(l10nId, args);
  }

  setL10NLabel(
    "cert-error-code-prefix-link",
    { error: failedCertInfo.errorCodeString },
    {
      title: failedCertInfo.errorCodeString,
      id: "errorCode",
      "data-l10n-name": "error-code-link",
      "data-telemetry-id": "error_code_link",
    },
    false
  );
  let errorCodeLink = document.getElementById("errorCode");
  if (errorCodeLink) {
    // We're attaching the event listener to the parent element and not on
    // the errorCodeLink itself because event listeners cannot be attached
    // to fluent DOM overlays.
    technicalInfo.addEventListener("click", handleErrorCodeClick);
  }
}

function handleErrorCodeClick(event) {
  if (event.target.id !== "errorCode") {
    return;
  }

  let debugInfo = document.getElementById("certificateErrorDebugInformation");
  debugInfo.style.display = "block";
  debugInfo.scrollIntoView({ block: "start", behavior: "smooth" });
}

/* Only do autofocus if we're the toplevel frame; otherwise we
   don't want to call attention to ourselves!  The key part is
   that autofocus happens on insertion into the tree, so we
   can remove the button, add @autofocus, and reinsert the
   button.
*/
function addAutofocus(selector, position = "afterbegin") {
  if (window.top == window) {
    var button = document.querySelector(selector);
    var parent = button.parentNode;
    button.remove();
    button.setAttribute("autofocus", "true");
    parent.insertAdjacentElement(position, button);
  }
}

for (let button of document.querySelectorAll(".try-again")) {
  button.addEventListener("click", function() {
    retryThis(this);
  });
}

// Note: It is important to run the script this way, instead of using
// an onload handler. This is because error pages are loaded as
// LOAD_BACKGROUND, which means that onload handlers will not be executed.
initPage();
