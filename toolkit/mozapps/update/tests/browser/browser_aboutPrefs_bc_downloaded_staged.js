/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Test for about:preferences background check for updates
// with the update downloaded and staged.
add_task(async function aboutPrefs_backgroundCheck_downloaded_staged() {
  await SpecialPowers.pushPrefEnv({
    set: [
      [PREF_APP_UPDATE_STAGING_ENABLED, true],
    ],
  });

  // Since the partial should be successful specify an invalid size for the
  // complete update.
  let updateParams = "&invalidCompleteSize=1";
  await runAboutPrefsUpdateTest(updateParams, true, [
    {
      panelId: "apply",
      checkActiveUpdate: {state: STATE_APPLIED},
      continueFile: null,
    },
  ]);
});
