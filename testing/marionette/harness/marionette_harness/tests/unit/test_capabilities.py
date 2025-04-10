# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function

from marionette_driver.errors import SessionNotCreatedException
from marionette_harness import MarionetteTestCase


class TestCapabilities(MarionetteTestCase):

    def setUp(self):
        super(TestCapabilities, self).setUp()
        self.caps = self.marionette.session_capabilities
        with self.marionette.using_context("chrome"):
            self.appinfo = self.marionette.execute_script("""
                return {
                  name: Services.appinfo.name,
                  version: Services.appinfo.version,
                  processID: Services.appinfo.processID,
                  buildID: Services.appinfo.appBuildID,
                }
                """)
            self.os_name = self.marionette.execute_script("""
                let name = Services.sysinfo.getProperty("name");
                switch (name) {
                  case "Windows_NT":
                    return "windows";
                  case "Darwin":
                    return "mac";
                  default:
                    return name.toLowerCase();
                }
                """)
            self.os_version = self.marionette.execute_script(
                "return Services.sysinfo.getProperty('version')")

    def get_fennec_profile(self):
        profile = self.marionette.instance.runner.device.app_ctx.remote_profile
        if self.caps["moz:profile"].lower() != profile.lower():
            # mozdevice may be using a symlink and readlink is not
            # universally available (missing from sdk 18).
            # Attempt to resolve the most common symlink cases by using
            # ls -l to determine if the root of the path (like /sdcard)
            # is a symlink.
            import posixpath
            import re
            device = self.marionette.instance.runner.device.app_ctx.device
            root = posixpath.sep.join(profile.split(posixpath.sep)[0:2])
            new_root = root
            match = True
            while match:
                ls_out = device.shell_output("ls -l %s" % new_root)
                match = re.match(r'.*->\s(.*)', ls_out)
                if match:
                    new_root = match.group(1)
            if new_root != root:
                profile = profile.replace(root, new_root)
        return profile

    def test_mandated_capabilities(self):
        self.assertIn("browserName", self.caps)
        self.assertIn("browserVersion", self.caps)
        self.assertIn("platformName", self.caps)
        self.assertIn("platformVersion", self.caps)
        self.assertIn("acceptInsecureCerts", self.caps)
        self.assertIn("setWindowRect", self.caps)
        self.assertIn("timeouts", self.caps)
        self.assertIn("strictFileInteractability", self.caps)

        self.assertEqual(self.caps["browserName"], self.appinfo["name"].lower())
        self.assertEqual(self.caps["browserVersion"], self.appinfo["version"])
        self.assertEqual(self.caps["platformName"], self.os_name)
        self.assertEqual(self.caps["platformVersion"], self.os_version)
        self.assertFalse(self.caps["acceptInsecureCerts"])
        if self.appinfo["name"] == "Firefox":
            self.assertTrue(self.caps["setWindowRect"])
        else:
            self.assertFalse(self.caps["setWindowRect"])
        self.assertDictEqual(self.caps["timeouts"],
                             {"implicit": 0,
                              "pageLoad": 300000,
                              "script": 30000})
        self.assertTrue(self.caps["strictFileInteractability"])

    def test_supported_features(self):
        self.assertIn("rotatable", self.caps)

    def test_additional_capabilities(self):
        self.assertIn("moz:processID", self.caps)
        self.assertEqual(self.caps["moz:processID"], self.appinfo["processID"])
        self.assertEqual(self.marionette.process_id, self.appinfo["processID"])

        self.assertIn("moz:profile", self.caps)
        if self.marionette.instance is not None:
            if self.caps["browserName"] == "fennec":
                current_profile = self.get_fennec_profile()
            else:
                current_profile = self.marionette.profile_path
            # Bug 1438461 - mozprofile uses lower-case letters even on case-sensitive filesystems
            self.assertEqual(self.caps["moz:profile"].lower(), current_profile.lower())

        self.assertIn("moz:accessibilityChecks", self.caps)
        self.assertFalse(self.caps["moz:accessibilityChecks"])

        self.assertIn("moz:buildID", self.caps)
        self.assertEqual(self.caps["moz:buildID"], self.appinfo["buildID"])
        self.assertIn("moz:useNonSpecCompliantPointerOrigin", self.caps)
        self.assertFalse(self.caps["moz:useNonSpecCompliantPointerOrigin"])

        self.assertIn("moz:webdriverClick", self.caps)
        self.assertTrue(self.caps["moz:webdriverClick"])

    def test_disable_webdriver_click(self):
        self.marionette.delete_session()
        self.marionette.start_session({"moz:webdriverClick": False})
        caps = self.marionette.session_capabilities
        self.assertFalse(caps["moz:webdriverClick"])

    def test_use_non_spec_compliant_pointer_origin(self):
        self.marionette.delete_session()
        self.marionette.start_session({"moz:useNonSpecCompliantPointerOrigin": True})
        caps = self.marionette.session_capabilities
        self.assertTrue(caps["moz:useNonSpecCompliantPointerOrigin"])

    def test_we_get_valid_uuid4_when_creating_a_session(self):
        self.assertNotIn("{", self.marionette.session_id,
                         "Session ID has {{}} in it: {}".format(
                             self.marionette.session_id))


class TestCapabilityMatching(MarionetteTestCase):

    def setUp(self):
        MarionetteTestCase.setUp(self)
        self.browser_name = self.marionette.session_capabilities["browserName"]
        self.delete_session()

    def delete_session(self):
        if self.marionette.session is not None:
            self.marionette.delete_session()

    def test_accept_insecure_certs(self):
        for value in ["", 42, {}, []]:
            print("  type {}".format(type(value)))
            with self.assertRaises(SessionNotCreatedException):
                self.marionette.start_session({"acceptInsecureCerts": value})

        self.delete_session()
        self.marionette.start_session({"acceptInsecureCerts": True})
        self.assertTrue(self.marionette.session_capabilities["acceptInsecureCerts"])

    def test_page_load_strategy(self):
        for strategy in ["none", "eager", "normal"]:
            print("valid strategy {}".format(strategy))
            self.delete_session()
            self.marionette.start_session({"pageLoadStrategy": strategy})
            self.assertEqual(self.marionette.session_capabilities["pageLoadStrategy"], strategy)

        for value in ["", "EAGER", True, 42, {}, [], None]:
            print("invalid strategy {}".format(value))
            with self.assertRaisesRegexp(SessionNotCreatedException, "InvalidArgumentError"):
                self.marionette.start_session({"pageLoadStrategy": value})

    def test_set_window_rect(self):
        if self.browser_name == "firefox":
            self.marionette.start_session({"setWindowRect": True})
            self.delete_session()
            with self.assertRaisesRegexp(SessionNotCreatedException, "InvalidArgumentError"):
                self.marionette.start_session({"setWindowRect": False})
        else:
            self.marionette.start_session({"setWindowRect": False})
            self.delete_session()
            with self.assertRaisesRegexp(SessionNotCreatedException, "InvalidArgumentError"):
                self.marionette.start_session({"setWindowRect": True})

    def test_timeouts(self):
        for value in ["", 2.5, {}, []]:
            print("  type {}".format(type(value)))
            with self.assertRaises(SessionNotCreatedException):
                self.marionette.start_session({"timeouts": {"pageLoad": value}})

        self.delete_session()

        timeouts = {"implicit": 0, "pageLoad": 2.0, "script": 2**53 - 1}
        self.marionette.start_session({"timeouts": timeouts})
        self.assertIn("timeouts", self.marionette.session_capabilities)
        self.assertDictEqual(self.marionette.session_capabilities["timeouts"], timeouts)
        self.assertDictEqual(self.marionette._send_message("WebDriver:GetTimeouts"), timeouts)

    def test_strict_file_interactability(self):
        for value in ["", 2.5, {}, []]:
            print("  type {}".format(type(value)))
            with self.assertRaises(SessionNotCreatedException):
                self.marionette.start_session({"strictFileInteractability": value})

        self.delete_session()

        self.marionette.start_session({"strictFileInteractability": True})
        self.assertIn("strictFileInteractability", self.marionette.session_capabilities)
        self.assertTrue(self.marionette.session_capabilities["strictFileInteractability"])

        self.delete_session()

        self.marionette.start_session({"strictFileInteractability": False})
        self.assertIn("strictFileInteractability", self.marionette.session_capabilities)
        self.assertFalse(self.marionette.session_capabilities["strictFileInteractability"])

    def test_unhandled_prompt_behavior(self):
        behaviors = [
            "accept",
            "accept and notify",
            "dismiss",
            "dismiss and notify",
            "ignore"
        ]

        for behavior in behaviors:
            print("valid unhandled prompt behavior {}".format(behavior))
            self.delete_session()
            self.marionette.start_session({"unhandledPromptBehavior": behavior})
            self.assertEqual(self.marionette.session_capabilities["unhandledPromptBehavior"],
                             behavior)

        # Default value
        self.delete_session()
        self.marionette.start_session()
        self.assertEqual(self.marionette.session_capabilities["unhandledPromptBehavior"],
                         "dismiss and notify")

        # Invalid values
        self.delete_session()
        for behavior in [None, "", "ACCEPT", True, 42, {}, []]:
            print("invalid unhandled prompt behavior {}".format(behavior))
            with self.assertRaisesRegexp(SessionNotCreatedException, "InvalidArgumentError"):
                self.marionette.start_session({"unhandledPromptBehavior": behavior})
