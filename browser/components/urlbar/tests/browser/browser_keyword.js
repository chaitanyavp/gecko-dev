/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

/**
 * This tests that keywords are displayed and handled correctly.
 */

async function promise_first_result(inputText) {
  await promiseAutocompleteResultPopup(inputText);

  return UrlbarTestUtils.getDetailsOfResultAt(window, 0);
}

function assertURL(result, expectedUrl, input, postData) {
  if (UrlbarPrefs.get("quantumbar")) {
    Assert.equal(result.url, expectedUrl, "Should have the correct URL");
    if (postData) {
      Assert.equal(NetUtil.readInputStreamToString(result.postData, result.postData.available()),
        postData, "Should have the correct postData");
    }
  } else {
    // We need to make a real URI out of this to ensure it's normalised for
    // comparison.
    Assert.equal(result.url, PlacesUtils.mozActionURI("keyword",
      {url: expectedUrl, input, postData}),
      "Expect correct url");
  }
}

const TEST_PATH = getRootDirectory(gTestPath)
  .replace("chrome://mochitests/content", "http://mochi.test:8888");
const TEST_URL = `${TEST_PATH}print_postdata.sjs`;

add_task(async function setup() {
  await PlacesUtils.keywords.insert({ keyword: "get",
                                      url: TEST_URL + "?q=%s" });
  await PlacesUtils.keywords.insert({ keyword: "post",
                                      url: TEST_URL,
                                      postData: "q=%s" });
  registerCleanupFunction(async function() {
    await PlacesUtils.keywords.remove("get");
    await PlacesUtils.keywords.remove("post");
    while (gBrowser.tabs.length > 1) {
      BrowserTestUtils.removeTab(gBrowser.selectedTab);
    }
  });
});

add_task(async function test_display_keyword_without_query() {
  await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:mozilla");

  // Test a keyword that also has blank spaces to ensure they are ignored as well.
  let result = await promise_first_result("get  ");

  Assert.equal(result.type, UrlbarUtils.RESULT_TYPE.KEYWORD,
    "Should have a keyword result");
  Assert.equal(result.displayed.title, "mochi.test:8888",
    "Node should contain the name of the bookmark");
  Assert.equal(result.displayed.action,
    Services.strings.createBundle("chrome://global/locale/autocomplete.properties")
            .GetStringFromName("visit"),
     "Should have visit indicated");
});

add_task(async function test_keyword_using_get() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:mozilla");

  let result = await promise_first_result("get something");

  Assert.equal(result.type, UrlbarUtils.RESULT_TYPE.KEYWORD,
    "Should have a keyword result");
  Assert.equal(result.displayed.title, "mochi.test:8888: something",
     "Node should contain the name of the bookmark and query");
  Assert.ok(!result.displayed.action, "Should have an empty action");

  assertURL(result, TEST_URL + "?q=something", "get something");

  let element = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);

  if (!UrlbarPrefs.get("quantumbar")) {
    // QuantumBar doesn't have separate boxes for items.
    let urlHbox = element._urlText.parentNode.parentNode;
    Assert.ok(urlHbox.classList.contains("ac-url"), "URL hbox element sanity check");
    is_element_hidden(urlHbox, "URL element should be hidden");
  }

  // Click on the result
  info("Normal click on result");
  let tabPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  EventUtils.synthesizeMouseAtCenter(element, {});
  await tabPromise;
  Assert.equal(tab.linkedBrowser.currentURI.spec, TEST_URL + "?q=something",
     "Tab should have loaded from clicking on result");

  // Middle-click on the result
  info("Middle-click on result");
  result = await promise_first_result("get somethingmore");

  Assert.equal(result.type, UrlbarUtils.RESULT_TYPE.KEYWORD,
    "Should have a keyword result");

  assertURL(result, TEST_URL + "?q=somethingmore", "get somethingmore");

  tabPromise = BrowserTestUtils.waitForEvent(gBrowser.tabContainer, "TabOpen");
  element = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
  EventUtils.synthesizeMouseAtCenter(element, {button: 1});
  let tabOpenEvent = await tabPromise;
  let newTab = tabOpenEvent.target;
  await BrowserTestUtils.browserLoaded(newTab.linkedBrowser);
  Assert.equal(newTab.linkedBrowser.currentURI.spec,
     TEST_URL + "?q=somethingmore",
     "Tab should have loaded from middle-clicking on result");
});


add_task(async function test_keyword_using_post() {
  let tab = await BrowserTestUtils.openNewForegroundTab(gBrowser, "about:mozilla");

  let result = await promise_first_result("post something");

  Assert.equal(result.type, UrlbarUtils.RESULT_TYPE.KEYWORD,
    "Should have a keyword result");
  Assert.equal(result.displayed.title, "mochi.test:8888: something",
     "Node should contain the name of the bookmark and query");
  Assert.ok(!result.displayed.action, "Should have an empty action");

  assertURL(result, TEST_URL, "post something", "q=something");

  // Click on the result
  info("Normal click on result");
  let tabPromise = BrowserTestUtils.browserLoaded(tab.linkedBrowser);
  let element = await UrlbarTestUtils.waitForAutocompleteResultAt(window, 0);
  EventUtils.synthesizeMouseAtCenter(element, {});
  info("waiting for tab");
  await tabPromise;
  Assert.equal(tab.linkedBrowser.currentURI.spec, TEST_URL,
               "Tab should have loaded from clicking on result");

  let postData = await ContentTask.spawn(tab.linkedBrowser, null, async function() {
    return content.document.body.textContent;
  });
  Assert.equal(postData, "q=something", "post data was submitted correctly");
});
