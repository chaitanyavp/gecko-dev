add_task(async function testCompletePatchApplyFailure() {
  let patchProps = {state: STATE_PENDING};
  let patches = getLocalPatchString(patchProps);
  let updates = getLocalUpdateString({}, patches);

  await runUpdateProcessingTest(updates, [
    {
      // if we have only an invalid patch, then something's wrong and we don't
      // have an automatic way to fix it, so show the manual update
      // doorhanger.
      notificationId: "update-manual",
      button: "button",
      beforeClick() {
        checkWhatsNewLink(window, "update-manual-whats-new");
      },
      async cleanup() {
        await BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser);
        is(gBrowser.selectedBrowser.currentURI.spec,
           URL_MANUAL_UPDATE, "Landed on manual update page.");
        gBrowser.removeTab(gBrowser.selectedTab);
      },
    },
  ]);
});
