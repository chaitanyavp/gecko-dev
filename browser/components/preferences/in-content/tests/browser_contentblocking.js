/* eslint-env webextensions */

ChromeUtils.defineModuleGetter(this, "Preferences",
                               "resource://gre/modules/Preferences.jsm");

const TP_PREF = "privacy.trackingprotection.enabled";
const TP_PBM_PREF = "privacy.trackingprotection.pbmode.enabled";
const TP_LIST_PREF = "urlclassifier.trackingTable";
const NCB_PREF = "network.cookie.cookieBehavior";
const CAT_PREF = "browser.contentblocking.category";
const FP_PREF = "privacy.trackingprotection.fingerprinting.enabled";
const CM_PREF = "privacy.trackingprotection.cryptomining.enabled";

const {
  EnterprisePolicyTesting,
  PoliciesPrefTracker,
} = ChromeUtils.import("resource://testing-common/EnterprisePolicyTesting.jsm", null);

registerCleanupFunction(async function policies_headjs_finishWithCleanSlate() {
  if (Services.policies.status != Ci.nsIEnterprisePolicies.INACTIVE) {
    await EnterprisePolicyTesting.setupPolicyEngineWithJson("");
  }
  is(Services.policies.status, Ci.nsIEnterprisePolicies.INACTIVE, "Engine is inactive at the end of the test");

  EnterprisePolicyTesting.resetRunOnceState();
  PoliciesPrefTracker.stop();
});

requestLongerTimeout(2);

// Tests that the content blocking main category checkboxes have the correct default state.
add_task(async function testContentBlockingMainCategory() {
  let prefs = [
    [TP_PREF, false],
    [TP_PBM_PREF, true],
    [NCB_PREF, Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER],
  ];

  for (let pref of prefs) {
    switch (typeof pref[1]) {
      case "boolean":
        SpecialPowers.setBoolPref(pref[0], pref[1]);
        break;
      case "number":
        SpecialPowers.setIntPref(pref[0], pref[1]);
        break;
    }
  }

  let checkboxes = [
    "#contentBlockingTrackingProtectionCheckbox",
    "#contentBlockingBlockCookiesCheckbox",
  ];

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});
  let doc = gBrowser.contentDocument;

  for (let selector of checkboxes) {
    let element = doc.querySelector(selector);
    ok(element, "checkbox " + selector + " exists");
    is(element.getAttribute("checked"), "true",
       "checkbox " + selector + " is checked");
  }

  // Ensure the dependent controls of the tracking protection subsection behave properly.
  let tpCheckbox = doc.querySelector(checkboxes[0]);

  let dependentControls = [
    "#trackingProtectionMenu",
  ];
  let alwaysEnabledControls = [
    "#trackingProtectionMenuDesc",
    ".content-blocking-category-name",
    "#changeBlockListLink",
  ];

  tpCheckbox.checked = true;

  // Select "Always" under "All Detected Trackers".
  let menu = doc.querySelector("#trackingProtectionMenu");
  let always = doc.querySelector("#trackingProtectionMenu > menupopup > menuitem[value=always]");
  let private = doc.querySelector("#trackingProtectionMenu > menupopup > menuitem[value=private]");
  menu.selectedItem = always;
  ok(!private.selected, "The Only in private windows item should not be selected");
  ok(always.selected, "The Always item should be selected");

  // The first time, privacy-pane-tp-ui-updated won't be dispatched since the
  // assignment above is a no-op.

  // Ensure the dependent controls are enabled
  checkControlState(doc, dependentControls, true);
  checkControlState(doc, alwaysEnabledControls, true);

  let promise = TestUtils.topicObserved("privacy-pane-tp-ui-updated");
  tpCheckbox.click();

  await promise;
  ok(!tpCheckbox.checked, "The checkbox should now be unchecked");

  // Ensure the dependent controls are disabled
  checkControlState(doc, dependentControls, false);
  checkControlState(doc, alwaysEnabledControls, true);

  // Make sure the selection in the tracking protection submenu persists after
  // a few times of checking and unchecking All Detected Trackers.
  // Doing this in a loop in order to avoid typing in the unrolled version manually.
  // We need to go from the checked state of the checkbox to unchecked back to
  // checked again...
  for (let i = 0; i < 3; ++i) {
    promise = TestUtils.topicObserved("privacy-pane-tp-ui-updated");
    tpCheckbox.click();

    await promise;
    is(tpCheckbox.checked, i % 2 == 0, "The checkbox should now be unchecked");
    is(private.selected, i % 2 == 0, "The Only in private windows item should be selected by default, when the checkbox is checked");
    ok(!always.selected, "The Always item should no longer be selected");
  }

  gBrowser.removeCurrentTab();

  for (let pref of prefs) {
    SpecialPowers.clearUserPref(pref[0]);
  }
});

// Tests that the content blocking "Standard" category radio sets the prefs to their default values.
add_task(async function testContentBlockingStandardCategory() {
  let prefs = {
    [TP_LIST_PREF]: null,
    [TP_PREF]: null,
    [TP_PBM_PREF]: null,
    [NCB_PREF]: null,
    [FP_PREF]: null,
    [CM_PREF]: null,
  };

  for (let pref in prefs) {
    switch (Services.prefs.getPrefType(pref)) {
    case Services.prefs.PREF_BOOL:
      prefs[pref] = Services.prefs.getBoolPref(pref);
      break;
    case Services.prefs.PREF_INT:
      prefs[pref] = Services.prefs.getIntPref(pref);
      break;
    case Services.prefs.PREF_STRING:
      prefs[pref] = Services.prefs.getCharPref(pref);
      break;
    default:
      ok(false, `Unknown pref type for ${pref}`);
    }
  }

  Services.prefs.setStringPref(TP_LIST_PREF, "test-track-simple,base-track-digest256,content-track-digest256");
  Services.prefs.setBoolPref(TP_PREF, true);
  Services.prefs.setBoolPref(TP_PBM_PREF, false);
  Services.prefs.setIntPref(NCB_PREF, Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER);
  Services.prefs.setBoolPref(FP_PREF, true);
  Services.prefs.setBoolPref(CM_PREF, true);

  for (let pref in prefs) {
    switch (Services.prefs.getPrefType(pref)) {
    case Services.prefs.PREF_BOOL:
      // Account for prefs that may have retained their default value
      if (Services.prefs.getBoolPref(pref) != prefs[pref]) {
        ok(Services.prefs.prefHasUserValue(pref), `modified the pref ${pref}`);
      }
      break;
    case Services.prefs.PREF_INT:
      if (Services.prefs.getIntPref(pref) != prefs[pref]) {
        ok(Services.prefs.prefHasUserValue(pref), `modified the pref ${pref}`);
      }
      break;
    case Services.prefs.PREF_STRING:
      if (Services.prefs.getCharPref(pref) != prefs[pref]) {
        ok(Services.prefs.prefHasUserValue(pref), `modified the pref ${pref}`);
      }
      break;
    default:
      ok(false, `Unknown pref type for ${pref}`);
    }
  }

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});
  let doc = gBrowser.contentDocument;

  let standardRadioOption = doc.getElementById("standardRadio");
  standardRadioOption.click();

  // TP prefs are reset async to check for extensions controlling them.
  await TestUtils.waitForCondition(() => !Services.prefs.prefHasUserValue(TP_PREF));

  for (let pref in prefs) {
    ok(!Services.prefs.prefHasUserValue(pref), `reset the pref ${pref}`);
  }
  is(Services.prefs.getStringPref(CAT_PREF), "standard", `${CAT_PREF} has been set to standard`);

  gBrowser.removeCurrentTab();
});

// Tests that the content blocking "Strict" category radio sets the prefs to the expected values.
add_task(async function testContentBlockingStrictCategory() {
  Services.prefs.setBoolPref(TP_PREF, false);
  Services.prefs.setBoolPref(TP_PBM_PREF, false);
  Services.prefs.setIntPref(NCB_PREF, Ci.nsICookieService.BEHAVIOR_LIMIT_FOREIGN);
  Services.prefs.setStringPref(TP_LIST_PREF, "test-track-simple,base-track-digest256,content-track-digest256");

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});
  let doc = gBrowser.contentDocument;

  let strictRadioOption = doc.getElementById("strictRadio");
  strictRadioOption.click();

  // TP prefs are reset async to check for extensions controlling them.
  await TestUtils.waitForCondition(() => Services.prefs.prefHasUserValue(TP_PREF));

  is(Services.prefs.getStringPref(CAT_PREF), "strict", `${CAT_PREF} has been set to strict`);
  is(Services.prefs.getBoolPref(TP_PREF), true, `${TP_PREF} has been set to true`);
  is(Services.prefs.getBoolPref(TP_PBM_PREF), true, `${TP_PBM_PREF} has been set to true`);
  is(Services.prefs.getIntPref(NCB_PREF), Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER, `${NCB_PREF} has been set to ${Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER}`);
  ok(!Services.prefs.prefHasUserValue(TP_LIST_PREF), `reset the pref ${TP_LIST_PREF}`);
  ok(!Services.prefs.prefHasUserValue(FP_PREF), `reset the pref ${FP_PREF}`);
  ok(!Services.prefs.prefHasUserValue(CM_PREF), `reset the pref ${CM_PREF}`);

  gBrowser.removeCurrentTab();
});

// Tests that the content blocking "Custom" category behaves as expected.
add_task(async function testContentBlockingCustomCategory() {
  let prefs = [TP_LIST_PREF, TP_PREF, TP_PBM_PREF, NCB_PREF, FP_PREF, CM_PREF];

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});
  let doc = gBrowser.contentDocument;
  let strictRadioOption = doc.getElementById("strictRadio");
  let standardRadioOption = doc.getElementById("standardRadio");
  let customRadioOption = doc.getElementById("customRadio");
  let defaults = new Preferences({defaultBranch: true});

  standardRadioOption.click();
  await TestUtils.waitForCondition(() => !Services.prefs.prefHasUserValue(TP_PREF));

  customRadioOption.click();
  await TestUtils.waitForCondition(() => Services.prefs.getStringPref(CAT_PREF) == "custom");
  // The custom option does not force changes of any prefs, other than CAT_PREF, all other TP prefs should remain as they were for standard.
  for (let pref of prefs) {
    ok(!Services.prefs.prefHasUserValue(pref), `the pref ${pref} remains as default value`);
  }
  is(Services.prefs.getStringPref(CAT_PREF), "custom", `${CAT_PREF} has been set to custom`);

  strictRadioOption.click();
  await TestUtils.waitForCondition(() => Services.prefs.prefHasUserValue(TP_PREF));

  // Changing the TP_PREF should necessarily set CAT_PREF to "custom"
  Services.prefs.setBoolPref(TP_PREF, false);
  await TestUtils.waitForCondition(() => !Services.prefs.prefHasUserValue(TP_PREF));
  is(Services.prefs.getStringPref(CAT_PREF), "custom", `${CAT_PREF} has been set to custom`);

  strictRadioOption.click();
  await TestUtils.waitForCondition(() => Services.prefs.getStringPref(CAT_PREF) == "strict");

  // Changing the FP_PREF and CM_PREF should necessarily set CAT_PREF to "custom"
  for (let pref of [FP_PREF, CM_PREF]) {
    Services.prefs.setBoolPref(pref, true);
    await TestUtils.waitForCondition(() => Services.prefs.prefHasUserValue(pref));
    is(Services.prefs.getStringPref(CAT_PREF), "custom", `${CAT_PREF} has been set to custom`);

    strictRadioOption.click();
    await TestUtils.waitForCondition(() => Services.prefs.getStringPref(CAT_PREF) == "strict");
  }

  // Changing the NCB_PREF should necessarily set CAT_PREF to "custom"
  let defaultNCB = defaults.get(NCB_PREF);
  let nonDefaultNCB;
  switch (defaultNCB) {
  case Ci.nsICookieService.BEHAVIOR_ACCEPT:
    nonDefaultNCB = Ci.nsICookieService.BEHAVIOR_REJECT;
    break;
  case Ci.nsICookieService.BEHAVIOR_REJECT_TRACKER:
    nonDefaultNCB = Ci.nsICookieService.BEHAVIOR_ACCEPT;
    break;
  default:
    ok(false, "Unexpected default value found for " + NCB_PREF + ": " + defaultNCB);
    break;
  }
  Services.prefs.setIntPref(NCB_PREF, nonDefaultNCB);
  await TestUtils.waitForCondition(() => Services.prefs.prefHasUserValue(NCB_PREF));
  is(Services.prefs.getStringPref(CAT_PREF), "custom", `${CAT_PREF} has been set to custom`);

  for (let pref of prefs) {
    SpecialPowers.clearUserPref(pref);
  }

  gBrowser.removeCurrentTab();
});

function checkControlState(doc, controls, enabled) {
  for (let selector of controls) {
    for (let control of doc.querySelectorAll(selector)) {
      if (enabled) {
        ok(!control.hasAttribute("disabled"), `${selector} is enabled.`);
      } else {
        is(control.getAttribute("disabled"), "true", `${selector} is disabled.`);
      }
    }
  }
}

// Checks that the menulists for tracking protection and cookie blocking are disabled when all TP prefs are off.
add_task(async function testContentBlockingDependentTPControls() {
  SpecialPowers.pushPrefEnv({set: [
    [TP_PREF, false],
    [TP_PBM_PREF, false],
    [NCB_PREF, Ci.nsICookieService.BEHAVIOR_ACCEPT],
    [CAT_PREF, "custom"],
  ]});

  let disabledControls = [
    "#trackingProtectionMenu",
    "#blockCookiesMenu",
  ];

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});
  let doc = gBrowser.contentDocument;
  checkControlState(doc, disabledControls, false);

  gBrowser.removeCurrentTab();
});

// Checks that cryptomining and fingerprinting visibility can be controlled via pref.
add_task(async function testCustomOptionsVisibility() {
  Services.prefs.setBoolPref("browser.contentblocking.cryptomining.preferences.ui.enabled", false);
  Services.prefs.setBoolPref("browser.contentblocking.fingerprinting.preferences.ui.enabled", false);

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});

  let doc = gBrowser.contentDocument;
  let cryptominersOption = doc.getElementById("contentBlockingCryptominersOption");
  let fingerprintersOption = doc.getElementById("contentBlockingFingerprintersOption");

  ok(cryptominersOption.hidden, "Cryptomining is hidden");
  ok(fingerprintersOption.hidden, "Fingerprinting is hidden");

  gBrowser.removeCurrentTab();

  Services.prefs.setBoolPref("browser.contentblocking.cryptomining.preferences.ui.enabled", true);

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});

  doc = gBrowser.contentDocument;
  cryptominersOption = doc.getElementById("contentBlockingCryptominersOption");
  fingerprintersOption = doc.getElementById("contentBlockingFingerprintersOption");

  ok(!cryptominersOption.hidden, "Cryptomining is shown");
  ok(fingerprintersOption.hidden, "Fingerprinting is hidden");

  gBrowser.removeCurrentTab();

  Services.prefs.setBoolPref("browser.contentblocking.fingerprinting.preferences.ui.enabled", true);

  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});

  doc = gBrowser.contentDocument;
  cryptominersOption = doc.getElementById("contentBlockingCryptominersOption");
  fingerprintersOption = doc.getElementById("contentBlockingFingerprintersOption");

  ok(!cryptominersOption.hidden, "Cryptomining is shown");
  ok(!fingerprintersOption.hidden, "Fingerprinting is shown");

  gBrowser.removeCurrentTab();

  Services.prefs.clearUserPref("browser.contentblocking.cryptomining.preferences.ui.enabled");
  Services.prefs.clearUserPref("browser.contentblocking.fingerprinting.preferences.ui.enabled");
});

// Checks that adding a custom enterprise policy will put the user in the custom category.
// Other categories will be disabled.
add_task(async function testPolicyCategorization() {
  Services.prefs.setStringPref(CAT_PREF, "standard");
  is(Services.prefs.getStringPref(CAT_PREF), "standard", `${CAT_PREF} starts on standard`);
  is(Services.prefs.getBoolPref(TP_PREF), false, `${TP_PREF} starts on false`);
  PoliciesPrefTracker.start();

  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {"EnableTrackingProtection": {
        "Value": true,
      },
    },
  });
  EnterprisePolicyTesting.checkPolicyPref(TP_PREF, true, false);
  is(Services.prefs.getStringPref(CAT_PREF), "custom", `${CAT_PREF} has been set to custom`);

  Services.prefs.setStringPref(CAT_PREF, "standard");
  is(Services.prefs.getStringPref(CAT_PREF), "standard", `${CAT_PREF} starts on standard`);
  is(Services.prefs.getIntPref(NCB_PREF), 4, `${NCB_PREF} starts on 4`);

  let uiUpdatedPromise = TestUtils.topicObserved("privacy-pane-tp-ui-updated");
  await EnterprisePolicyTesting.setupPolicyEngineWithJson({
    policies: {"Cookies": {
        "AcceptThirdParty": "never",
        "Locked": true,
      },
    },
  });
  await openPreferencesViaOpenPreferencesAPI("privacy", {leaveOpen: true});
  await uiUpdatedPromise;

  EnterprisePolicyTesting.checkPolicyPref(NCB_PREF, 1, true);
  is(Services.prefs.getStringPref(CAT_PREF), "custom", `${CAT_PREF} has been set to custom`);

  let doc = gBrowser.contentDocument;
  let strictRadioOption = doc.getElementById("strictRadio");
  let standardRadioOption = doc.getElementById("standardRadio");
  is(strictRadioOption.disabled, true, "the strict option is disabled");
  is(standardRadioOption.disabled, true, "the standard option is disabled");

  gBrowser.removeCurrentTab();
});
