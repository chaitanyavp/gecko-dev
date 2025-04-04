/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// This file defines static prefs, i.e. those that are defined at startup and
// used entirely or mostly from C++ code.
//
// If a pref is listed here and also in a prefs data file such as all.js, the
// value from the latter will override the value given here. For vanilla
// browser builds such overrides are discouraged, but they are necessary for
// some configurations (e.g. Thunderbird).
//
// The file is separated into sections, where the sections are determined by
// the first segment of the prefnames within (e.g. "network.predictor.enabled"
// is within the "Network" section). Sections should be kept in alphabetical
// order, but prefs within sections need not be.
//
// Normal prefs
// ------------
// Definitions of normal prefs in this file have the following form.
//
//   PREF(<pref-name-string>, <cpp-type>, <default-value>)
//
// - <pref-name-string> is the name of the pref, as it appears in about:config.
//   It is used in most libpref API functions (from both C++ and JS code).
//
// - <cpp-type> is one of bool, int32_t, float, or String (which is just a
//   typedef for `const char*` in StaticPrefs.h). Note that float prefs are
//   stored internally as strings.
//
// - <default-value> is the default value. Its type should match <cpp-type>.
//
// VarCache prefs
// --------------
// A VarCache pref is a special type of pref. It can be accessed via the normal
// pref hash table lookup functions, but it also has an associated global
// variable (the VarCache) that mirrors the pref value in the prefs hash table,
// and a getter function that reads that global variable. Using the getter to
// read the pref's value has the two following advantages over the normal API
// functions.
//
// - A direct global variable access is faster than a hash table lookup.
//
// - A global variable can be accessed off the main thread. If a pref *is*
//   accessed off the main thread, it should use an atomic type. (But note that
//   many VarCaches that should be atomic are not, in particular because
//   Atomic<float> is not available, alas.)
//
// Definitions of VarCache prefs in this file has the following form.
//
//   VARCACHE_PREF(
//     <pref-name-string>,
//     <pref-name-id>,
//     <cpp-type>, <default-value>
//   )
//
// - <pref-name-string> is the same as for normal prefs.
//
// - <pref-name-id> is the name of the static getter function generated within
//   the StaticPrefs class. For consistency, the identifier for every pref
//   should be created by starting with <pref-name-string> and converting any
//   '.' or '-' chars to '_'. For example, "foo.bar_baz" becomes
//   |foo_bar_baz|. This is arguably ugly, but clear, and you can search for
//   both using the regexp /foo.bar.baz/.
//
// - <cpp-type> is one of bool, int32_t, uint32_t, float, or an Atomic version
//   of one of those. The C++ preprocessor doesn't like template syntax in a
//   macro argument, so use the typedefs defines in StaticPrefs.h; for example,
//   use `ReleaseAcquireAtomicBool` instead of `Atomic<bool, ReleaseAcquire>`.
//
// - <default-value> is the same as for normal prefs.
//
// Note that Rust code must access the global variable directly, rather than via
// the getter.

// clang-format off

//---------------------------------------------------------------------------
// Accessibility prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "accessibility.monoaudio.enable",
   accessibility_monoaudio_enable,
  RelaxedAtomicBool, false
)

//---------------------------------------------------------------------------
// Fuzzing prefs. It's important that these can only be checked in fuzzing
// builds (when FUZZING is defined), otherwise you could enable the fuzzing
// stuff on your regular build which would be bad :)
//---------------------------------------------------------------------------

#ifdef FUZZING
VARCACHE_PREF(
  "fuzzing.enabled",
   fuzzing_enabled,
  bool, false
)
#endif

//---------------------------------------------------------------------------
// Clipboard prefs
//---------------------------------------------------------------------------

#if !defined(ANDROID) && !defined(XP_MACOSX) && defined(XP_UNIX)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "clipboard.autocopy",
   clipboard_autocopy,
  bool, PREF_VALUE
)
#undef PREF_VALUE

//---------------------------------------------------------------------------
// DOM prefs
//---------------------------------------------------------------------------

// Is support for composite operations from the Web Animations API enabled?
#ifdef RELEASE_OR_BETA
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "dom.animations-api.compositing.enabled",
   dom_animations_api_compositing_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Is support for Document.getAnimations() and Element.getAnimations()
// supported?
//
// Before enabling this by default, make sure also CSSPseudoElement interface
// has been spec'ed properly, or we should add a separate pref for
// CSSPseudoElement interface. See Bug 1174575 for further details.
#ifdef RELEASE_OR_BETA
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "dom.animations-api.getAnimations.enabled",
   dom_animations_api_getAnimations_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Is support for animations from the Web Animations API without 0%/100%
// keyframes enabled?
#ifdef RELEASE_OR_BETA
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "dom.animations-api.implicit-keyframes.enabled",
   dom_animations_api_implicit_keyframes_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Is support for timelines from the Web Animations API enabled?
#ifdef RELEASE_OR_BETA
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "dom.animations-api.timelines.enabled",
   dom_animations_api_timelines_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Whether Mozilla specific "text" event should be dispatched only in the
// system group or not in content.
VARCACHE_PREF(
  "dom.compositionevent.text.dispatch_only_system_group_in_content",
   dom_compositionevent_text_dispatch_only_system_group_in_content,
   bool, true
)

// How long a content process can take before closing its IPC channel
// after shutdown is initiated.  If the process exceeds the timeout,
// we fear the worst and kill it.
#if !defined(DEBUG) && !defined(MOZ_ASAN) && !defined(MOZ_VALGRIND) && \
    !defined(MOZ_TSAN)
# define PREF_VALUE 5
#else
# define PREF_VALUE 0
#endif
VARCACHE_PREF(
  "dom.ipc.tabs.shutdownTimeoutSecs",
   dom_ipc_tabs_shutdownTimeoutSecs,
  RelaxedAtomicUint32, PREF_VALUE
)
#undef PREF_VALUE

// If this is true, "keypress" event's keyCode value and charCode value always
// become same if the event is not created/initialized by JS.
VARCACHE_PREF(
  "dom.keyboardevent.keypress.set_keycode_and_charcode_to_same_value",
   dom_keyboardevent_keypress_set_keycode_and_charcode_to_same_value,
  bool, true
)

// Whether we conform to Input Events Level 1 or Input Events Level 2.
// true:  conforming to Level 1
// false: conforming to Level 2
VARCACHE_PREF(
  "dom.input_events.conform_to_level_1",
   dom_input_events_conform_to_level_1,
  bool, true
)

// NOTE: This preference is used in unit tests. If it is removed or its default
// value changes, please update test_sharedMap_var_caches.js accordingly.
VARCACHE_PREF(
  "dom.webcomponents.shadowdom.report_usage",
   dom_webcomponents_shadowdom_report_usage,
  bool, false
)

// Whether we disable triggering mutation events for changes to style
// attribute via CSSOM.
// NOTE: This preference is used in unit tests. If it is removed or its default
// value changes, please update test_sharedMap_var_caches.js accordingly.
VARCACHE_PREF(
  "dom.mutation-events.cssom.disabled",
   dom_mutation_events_cssom_disabled,
  bool, true
)

VARCACHE_PREF(
  "dom.performance.enable_scheduler_timing",
  dom_performance_enable_scheduler_timing,
  RelaxedAtomicBool, true
)

// Should we defer timeouts and intervals while loading a page.  Released
// on Idle or when the page is loaded.
VARCACHE_PREF(
  "dom.timeout.defer_during_load",
  dom_timeout_defer_during_load,
  bool, true
)

// Maximum deferral time for setTimeout/Interval in milliseconds
VARCACHE_PREF(
  "dom.timeout.max_idle_defer_ms",
  dom_timeout_max_idle_defer_ms,
  uint32_t, 10*1000
)

VARCACHE_PREF(
  "dom.performance.children_results_ipc_timeout",
  dom_performance_children_results_ipc_timeout,
  uint32_t, 1000
)

// If true. then the service worker interception and the ServiceWorkerManager
// will live in the parent process.  This only takes effect on browser start.
// Note, this is not currently safe to use for normal browsing yet.
PREF("dom.serviceWorkers.parent_intercept", bool, false)

// Enable/disable the PaymentRequest API
VARCACHE_PREF(
  "dom.payments.request.enabled",
   dom_payments_request_enabled,
  bool, false
)

// Whether a user gesture is required to call PaymentRequest.prototype.show().
VARCACHE_PREF(
  "dom.payments.request.user_interaction_required",
  dom_payments_request_user_interaction_required,
  bool, true
)

// Time in milliseconds for PaymentResponse to wait for
// the Web page to call complete().
VARCACHE_PREF(
  "dom.payments.response.timeout",
   dom_payments_response_timeout,
  uint32_t, 5000
)

// SW Cache API
VARCACHE_PREF(
  "dom.caches.enabled",
   dom_caches_enabled,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "dom.caches.testing.enabled",
   dom_caches_testing_enabled,
  RelaxedAtomicBool, false
)

// Enable printing performance marks/measures to log
VARCACHE_PREF(
  "dom.performance.enable_user_timing_logging",
   dom_performance_enable_user_timing_logging,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "dom.webnotifications.enabled",
   dom_webnotifications_enabled,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "dom.webnotifications.allowinsecure",
   dom_webnotifications_allowinsecure,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "dom.webnotifications.serviceworker.enabled",
   dom_webnotifications_serviceworker_enabled,
  RelaxedAtomicBool, true
)

#ifdef NIGHTLY_BUILD
# define PREF_VALUE  true
#else
# define PREF_VALUE  false
#endif
VARCACHE_PREF(
  "dom.webnotifications.requireinteraction.enabled",
   dom_webnotifications_requireinteraction_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "dom.serviceWorkers.enabled",
   dom_serviceWorkers_enabled,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "dom.serviceWorkers.testing.enabled",
   dom_serviceWorkers_testing_enabled,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "dom.testing.structuredclonetester.enabled",
  dom_testing_structuredclonetester_enabled,
  RelaxedAtomicBool, false
)

// Enable Storage API for all platforms except Android.
#if !defined(MOZ_WIDGET_ANDROID)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "dom.storageManager.enabled",
   dom_storageManager_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

// WHATWG promise rejection events. See
// https://html.spec.whatwg.org/multipage/webappapis.html#promiserejectionevent
// TODO: Enable the event interface once actually firing it (bug 1362272).
VARCACHE_PREF(
  "dom.promise_rejection_events.enabled",
   dom_promise_rejection_events_enabled,
  RelaxedAtomicBool, false
)

// Push
VARCACHE_PREF(
  "dom.push.enabled",
   dom_push_enabled,
  RelaxedAtomicBool, false
)

#if !defined(MOZ_WIDGET_ANDROID)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "dom.webkitBlink.dirPicker.enabled",
   dom_webkitBlink_dirPicker_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

// Network Information API
#if defined(MOZ_WIDGET_ANDROID)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "dom.netinfo.enabled",
   dom_netinfo_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "dom.fetchObserver.enabled",
   dom_fetchObserver_enabled,
  RelaxedAtomicBool, false
)

// Enable Performance Observer API
VARCACHE_PREF(
  "dom.enable_performance_observer",
   dom_enable_performance_observer,
  RelaxedAtomicBool, true
)

// Enable passing the "storage" option to indexedDB.open.
VARCACHE_PREF(
  "dom.indexedDB.storageOption.enabled",
   dom_indexedDB_storageOption_enabled,
  RelaxedAtomicBool, false
)

#ifdef JS_BUILD_BINAST
VARCACHE_PREF(
  "dom.script_loader.binast_encoding.enabled",
   dom_script_loader_binast_encoding_enabled,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "dom.script_loader.binast_encoding.domain.restrict",
   dom_script_loader_binast_encoding_domain_restrict,
  bool, true
)
#endif

// IMPORTANT: Keep this in condition in sync with all.js. The value
// of MOZILLA_OFFICIAL is different between full and artifact builds, so without
// it being specified there, dump is disabled in artifact builds (see Bug 1490412).
#ifdef MOZILLA_OFFICIAL
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "browser.dom.window.dump.enabled",
   browser_dom_window_dump_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "dom.worker.canceling.timeoutMilliseconds",
   dom_worker_canceling_timeoutMilliseconds,
  RelaxedAtomicUint32, 30000 /* 30 seconds */
)

// Enable content type normalization of XHR uploads via MIME Sniffing standard
// Disabled for now in bz1499136
VARCACHE_PREF(
  "dom.xhr.standard_content_type_normalization",
   dom_xhr_standard_content_type_normalization,
  RelaxedAtomicBool, false
)

// Block multiple external protocol URLs in iframes per single event.
VARCACHE_PREF(
  "dom.block_external_protocol_in_iframes",
   dom_block_external_protocol_in_iframes,
  bool, true
)

// Any how many seconds we allow external protocol URLs in iframe when not in
// single events
VARCACHE_PREF(
  "dom.delay.block_external_protocol_in_iframes",
   dom_delay_block_external_protocol_in_iframes,
  uint32_t, 10 // in seconds
)

// Block multiple window.open() per single event.
VARCACHE_PREF(
  "dom.block_multiple_popups",
   dom_block_multiple_popups,
  bool, true
)

// For area and anchor elements with target=_blank and no rel set to
// opener/noopener, this pref sets noopener by default.
VARCACHE_PREF(
  "dom.targetBlankNoOpener.enabled",
   dom_targetBlankNoOpener_enabled,
  bool, true
)

VARCACHE_PREF(
  "dom.disable_open_during_load",
   dom_disable_open_during_load,
  bool, false
)

// Storage-access API.
VARCACHE_PREF(
  "dom.storage_access.enabled",
   dom_storage_access_enabled,
  bool, false
)

//---------------------------------------------------------------------------
// Extension prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "extensions.allowPrivateBrowsingByDefault",
   extensions_allowPrivateBrowsingByDefault,
  bool, true
)

//---------------------------------------------------------------------------
// Full-screen prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "full-screen-api.unprefix.enabled",
   full_screen_api_unprefix_enabled,
  bool, true
)

//---------------------------------------------------------------------------
// Graphics prefs
//---------------------------------------------------------------------------

// In theory: 0 = never, 1 = quick, 2 = always, though we always just use it as
// a bool!
VARCACHE_PREF(
  "browser.display.use_document_fonts",
   browser_display_use_document_fonts,
  RelaxedAtomicInt32, 1
)

VARCACHE_PREF(
  "gfx.font_rendering.opentype_svg.enabled",
   gfx_font_rendering_opentype_svg_enabled,
  bool, true
)

VARCACHE_PREF(
  "gfx.offscreencanvas.enabled",
   gfx_offscreencanvas_enabled,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "gfx.font_ahem_antialias_none",
   gfx_font_ahem_antialias_none,
  RelaxedAtomicBool, false
)

#ifdef RELEASE_OR_BETA
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "gfx.omta.background-color",
   gfx_omta_background_color,
  bool, PREF_VALUE
)
#undef PREF_VALUE

//---------------------------------------------------------------------------
// HTML5 parser prefs
//---------------------------------------------------------------------------

// Toggle which thread the HTML5 parser uses for stream parsing.
VARCACHE_PREF(
  "html5.offmainthread",
   html5_offmainthread,
  bool, true
)

// Time in milliseconds between the time a network buffer is seen and the timer
// firing when the timer hasn't fired previously in this parse in the
// off-the-main-thread HTML5 parser.
VARCACHE_PREF(
  "html5.flushtimer.initialdelay",
   html5_flushtimer_initialdelay,
  RelaxedAtomicInt32, 16
)

// Time in milliseconds between the time a network buffer is seen and the timer
// firing when the timer has already fired previously in this parse.
VARCACHE_PREF(
  "html5.flushtimer.subsequentdelay",
   html5_flushtimer_subsequentdelay,
  RelaxedAtomicInt32, 16
)

//---------------------------------------------------------------------------
// Layout prefs
//---------------------------------------------------------------------------

// Whether to block large cursors intersecting UI.
VARCACHE_PREF(
  "layout.cursor.block.enabled",
   layout_cursor_block_enabled,
  bool, true
)

// The maximum width or height of the cursor we should allow when intersecting
// the UI, in CSS pixels.
VARCACHE_PREF(
  "layout.cursor.block.max-size",
   layout_cursor_block_max_size,
  uint32_t, 64
)

// Debug-only pref to force enable the AccessibleCaret. If you want to
// control AccessibleCaret by mouse, you'll need to set
// "layout.accessiblecaret.hide_carets_for_mouse_input" to false.
VARCACHE_PREF(
  "layout.accessiblecaret.enabled",
   layout_accessiblecaret_enabled,
  bool, false
)

// Enable the accessible caret on platforms/devices
// that we detect have touch support. Note that this pref is an
// additional way to enable the accessible carets, rather than
// overriding the layout.accessiblecaret.enabled pref.
VARCACHE_PREF(
  "layout.accessiblecaret.enabled_on_touch",
   layout_accessiblecaret_enabled_on_touch,
  bool, true
)

// By default, carets become tilt only when they are overlapping.
VARCACHE_PREF(
  "layout.accessiblecaret.always_tilt",
   layout_accessiblecaret_always_tilt,
  bool, false
)

// Show caret in cursor mode when long tapping on an empty content. This
// also changes the default update behavior in cursor mode, which is based
// on the emptiness of the content, into something more heuristic. See
// AccessibleCaretManager::UpdateCaretsForCursorMode() for the details.
VARCACHE_PREF(
  "layout.accessiblecaret.caret_shown_when_long_tapping_on_empty_content",
   layout_accessiblecaret_caret_shown_when_long_tapping_on_empty_content,
  bool, false
)

// 0 = by default, always hide carets for selection changes due to JS calls.
// 1 = update any visible carets for selection changes due to JS calls,
//     but don't show carets if carets are hidden.
// 2 = always show carets for selection changes due to JS calls.
VARCACHE_PREF(
  "layout.accessiblecaret.script_change_update_mode",
   layout_accessiblecaret_script_change_update_mode,
  int32_t, 0
)

// Allow one caret to be dragged across the other caret without any limitation.
// This matches the built-in convention for all desktop platforms.
VARCACHE_PREF(
  "layout.accessiblecaret.allow_dragging_across_other_caret",
   layout_accessiblecaret_allow_dragging_across_other_caret,
  bool, true
)

// Optionally provide haptic feedback on long-press selection events.
VARCACHE_PREF(
  "layout.accessiblecaret.hapticfeedback",
   layout_accessiblecaret_hapticfeedback,
  bool, false
)

// Smart phone-number selection on long-press is not enabled by default.
VARCACHE_PREF(
  "layout.accessiblecaret.extend_selection_for_phone_number",
   layout_accessiblecaret_extend_selection_for_phone_number,
  bool, false
)

// Keep the accessible carets hidden when the user is using mouse input (as
// opposed to touch/pen/etc.).
VARCACHE_PREF(
  "layout.accessiblecaret.hide_carets_for_mouse_input",
   layout_accessiblecaret_hide_carets_for_mouse_input,
  bool, true
)

// CSS attributes (width, height, margin-left) of the AccessibleCaret in CSS
// pixels.
VARCACHE_PREF(
  "layout.accessiblecaret.width",
   layout_accessiblecaret_width,
  float, 34.0f
)

VARCACHE_PREF(
  "layout.accessiblecaret.height",
   layout_accessiblecaret_height,
  float, 36.0f
)

VARCACHE_PREF(
  "layout.accessiblecaret.margin-left",
   layout_accessiblecaret_margin_left,
  float, -18.5f
)

// Simulate long tap events to select words. Mainly used in manual testing
// with mouse.
VARCACHE_PREF(
  "layout.accessiblecaret.use_long_tap_injector",
   layout_accessiblecaret_use_long_tap_injector,
  bool, false
)

// Is parallel CSS parsing enabled?
VARCACHE_PREF(
  "layout.css.parsing.parallel",
   layout_css_parsing_parallel,
  bool, true
)

// Are style system use counters enabled?
#ifdef RELEASE_OR_BETA
#define PREF_VALUE false
#else
#define PREF_VALUE true
#endif
VARCACHE_PREF(
  "layout.css.use-counters.enabled",
   layout_css_use_counters_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Is CSS error reporting enabled?
VARCACHE_PREF(
  "layout.css.report_errors",
  layout_css_report_errors,
  bool, true
)

// Is support for the font-display @font-face descriptor enabled?
VARCACHE_PREF(
  "layout.css.font-display.enabled",
   layout_css_font_display_enabled,
  bool, true
)

// Are webkit-prefixed properties & property-values supported?
VARCACHE_PREF(
  "layout.css.prefixes.webkit",
   layout_css_prefixes_webkit,
  bool, true
)

// Are "-webkit-{min|max}-device-pixel-ratio" media queries supported? (Note:
// this pref has no effect if the master 'layout.css.prefixes.webkit' pref is
// set to false.)
VARCACHE_PREF(
  "layout.css.prefixes.device-pixel-ratio-webkit",
   layout_css_prefixes_device_pixel_ratio_webkit,
  bool, true
)

// Are -moz-prefixed gradient functions enabled?
VARCACHE_PREF(
  "layout.css.prefixes.gradients",
   layout_css_prefixes_gradients,
  bool, true
)

// Whether the offset-* logical property aliases are enabled.
VARCACHE_PREF(
  "layout.css.offset-logical-properties.enabled",
   layout_css_offset_logical_properties_enabled,
  bool, false
)

// Should stray control characters be rendered visibly?
#ifdef RELEASE_OR_BETA
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "layout.css.control-characters.visible",
   layout_css_control_characters_visible,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Should the :visited selector ever match (otherwise :link matches instead)?
VARCACHE_PREF(
  "layout.css.visited_links_enabled",
   layout_css_visited_links_enabled,
  bool, true
)

// Is the '-webkit-appearance' alias for '-moz-appearance' enabled?
VARCACHE_PREF(
  "layout.css.webkit-appearance.enabled",
   layout_css_webkit_appearance_enabled,
  bool, true
)

// Pref to control whether @-moz-document rules are enabled in content pages.
VARCACHE_PREF(
  "layout.css.moz-document.content.enabled",
   layout_css_moz_document_content_enabled,
  bool, false
)

#ifdef NIGHTLY_BUILD
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "layout.css.supports-selector.enabled",
   layout_css_supports_selector_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Pref to control whether @-moz-document url-prefix() is parsed in content
// pages. Only effective when layout.css.moz-document.content.enabled is false.
#ifdef EARLY_BETA_OR_EARLIER
#define PREF_VALUE false
#else
#define PREF_VALUE true
#endif
VARCACHE_PREF(
  "layout.css.moz-document.url-prefix-hack.enabled",
   layout_css_moz_document_url_prefix_hack_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "layout.css.xul-display-values.content.enabled",
   layout_css_xul_display_values_content_enabled,
  bool, false
)

// Pref to control whether display: -moz-box and display: -moz-inline-box are
// parsed in content pages.
VARCACHE_PREF(
  "layout.css.xul-box-display-values.content.enabled",
   layout_css_xul_box_display_values_content_enabled,
  bool, false
)

// Pref to control whether ::xul-tree-* pseudo-elements are parsed in content
// pages.
VARCACHE_PREF(
  "layout.css.xul-tree-pseudos.content.enabled",
   layout_css_xul_tree_pseudos_content_enabled,
  bool, false
)

// Is support for CSS "grid-template-{columns,rows}: subgrid X" enabled?
VARCACHE_PREF(
  "layout.css.grid-template-subgrid-value.enabled",
   layout_css_grid_template_subgrid_value_enabled,
  bool, false
)

// Is support for variation fonts enabled?
VARCACHE_PREF(
  "layout.css.font-variations.enabled",
   layout_css_font_variations_enabled,
  RelaxedAtomicBool, true
)

// Are we emulating -moz-{inline}-box layout using CSS flexbox?
VARCACHE_PREF(
  "layout.css.emulate-moz-box-with-flex",
   layout_css_emulate_moz_box_with_flex,
  bool, false
)

// Does arbitrary ::-webkit-* pseudo-element parsed?
VARCACHE_PREF(
  "layout.css.unknown-webkit-pseudo-element",
   layout_css_unknown_webkit_pseudo_element,
  bool, true
)

// Is path() supported in clip-path?
VARCACHE_PREF(
  "layout.css.clip-path-path.enabled",
   layout_css_clip_path_path_enabled,
  bool, false
)

// Is support for CSS column-span enabled?
VARCACHE_PREF(
  "layout.css.column-span.enabled",
   layout_css_column_span_enabled,
  bool, false
)

// Is steps(jump-*) supported in easing functions?
VARCACHE_PREF(
  "layout.css.step-position-jump.enabled",
   layout_css_step_position_jump_enabled,
  bool, true
)

// Are dynamic reflow roots enabled?
VARCACHE_PREF(
   "layout.dynamic-reflow-roots.enabled",
   layout_dynamic_reflow_roots_enabled,
  bool, true
)

VARCACHE_PREF(
   "layout.lower_priority_refresh_driver_during_load",
   layout_lower_priority_refresh_driver_during_load,
  bool, true
)

// Pref to control enabling scroll anchoring.
VARCACHE_PREF(
  "layout.css.scroll-anchoring.enabled",
   layout_css_scroll_anchoring_enabled,
  bool, true
)

VARCACHE_PREF(
  "layout.css.scroll-anchoring.highlight",
   layout_css_scroll_anchoring_highlight,
  bool, false
)

// Is the CSS Scroll Snap Module Level 1 enabled?
VARCACHE_PREF(
  "layout.css.scroll-snap-v1.enabled",
   layout_css_scroll_snap_v1_enabled,
  RelaxedAtomicBool, false
)

//---------------------------------------------------------------------------
// JavaScript prefs
//---------------------------------------------------------------------------

// nsJSEnvironmentObserver observes the memory-pressure notifications and
// forces a garbage collection and cycle collection when it happens, if the
// appropriate pref is set.
#ifdef ANDROID
  // Disable the JS engine's GC on memory pressure, since we do one in the
  // mobile browser (bug 669346).
  // XXX: this value possibly should be changed, or the pref removed entirely.
  //      See bug 1450787.
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "javascript.options.gc_on_memory_pressure",
   javascript_options_gc_on_memory_pressure,
  bool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "javascript.options.compact_on_user_inactive",
   javascript_options_compact_on_user_inactive,
  bool, true
)

// The default amount of time to wait from the user being idle to starting a
// shrinking GC.
#ifdef NIGHTLY_BUILD
# define PREF_VALUE  15000  // ms
#else
# define PREF_VALUE 300000  // ms
#endif
VARCACHE_PREF(
  "javascript.options.compact_on_user_inactive_delay",
   javascript_options_compact_on_user_inactive_delay,
   uint32_t, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "javascript.options.mem.log",
   javascript_options_mem_log,
  bool, false
)

VARCACHE_PREF(
  "javascript.options.mem.notify",
   javascript_options_mem_notify,
  bool, false
)

// Streams API
VARCACHE_PREF(
  "javascript.options.streams",
   javascript_options_streams,
  RelaxedAtomicBool, false
)

// BigInt API
VARCACHE_PREF(
  "javascript.options.bigint",
   javascript_options_bigint,
  RelaxedAtomicBool, false
)


//---------------------------------------------------------------------------
// Media prefs
//---------------------------------------------------------------------------

// These prefs use camel case instead of snake case for the getter because one
// reviewer had an unshakeable preference for that.

// File-backed MediaCache size.
#ifdef ANDROID
# define PREF_VALUE  32768  // Measured in KiB
#else
# define PREF_VALUE 512000  // Measured in KiB
#endif
VARCACHE_PREF(
  "media.cache_size",
   MediaCacheSize,
  RelaxedAtomicUint32, PREF_VALUE
)
#undef PREF_VALUE

// If a resource is known to be smaller than this size (in kilobytes), a
// memory-backed MediaCache may be used; otherwise the (single shared global)
// file-backed MediaCache is used.
VARCACHE_PREF(
  "media.memory_cache_max_size",
   MediaMemoryCacheMaxSize,
  uint32_t, 8192      // Measured in KiB
)

// Don't create more memory-backed MediaCaches if their combined size would go
// above this absolute size limit.
VARCACHE_PREF(
  "media.memory_caches_combined_limit_kb",
   MediaMemoryCachesCombinedLimitKb,
  uint32_t, 524288
)

// Don't create more memory-backed MediaCaches if their combined size would go
// above this relative size limit (a percentage of physical memory).
VARCACHE_PREF(
  "media.memory_caches_combined_limit_pc_sysmem",
   MediaMemoryCachesCombinedLimitPcSysmem,
  uint32_t, 5         // A percentage
)

// When a network connection is suspended, don't resume it until the amount of
// buffered data falls below this threshold (in seconds).
#ifdef ANDROID
# define PREF_VALUE 10  // Use a smaller limit to save battery.
#else
# define PREF_VALUE 30
#endif
VARCACHE_PREF(
  "media.cache_resume_threshold",
   MediaCacheResumeThreshold,
  RelaxedAtomicInt32, PREF_VALUE
)
#undef PREF_VALUE

// Stop reading ahead when our buffered data is this many seconds ahead of the
// current playback position. This limit can stop us from using arbitrary
// amounts of network bandwidth prefetching huge videos.
#ifdef ANDROID
# define PREF_VALUE 30  // Use a smaller limit to save battery.
#else
# define PREF_VALUE 60
#endif
VARCACHE_PREF(
  "media.cache_readahead_limit",
   MediaCacheReadaheadLimit,
  RelaxedAtomicInt32, PREF_VALUE
)
#undef PREF_VALUE

// AudioSink
VARCACHE_PREF(
  "media.resampling.enabled",
   MediaResamplingEnabled,
  RelaxedAtomicBool, false
)

#if defined(XP_WIN) || defined(XP_DARWIN) || defined(MOZ_PULSEAUDIO)
// libcubeb backend implement .get_preferred_channel_layout
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "media.forcestereo.enabled",
   MediaForcestereoEnabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

// VideoSink
VARCACHE_PREF(
  "media.ruin-av-sync.enabled",
   MediaRuinAvSyncEnabled,
  RelaxedAtomicBool, false
)

// Encrypted Media Extensions
#if defined(ANDROID)
# if defined(NIGHTLY_BUILD)
#  define PREF_VALUE true
# else
#  define PREF_VALUE false
# endif
#elif defined(XP_LINUX)
  // On Linux EME is visible but disabled by default. This is so that the "Play
  // DRM content" checkbox in the Firefox UI is unchecked by default. DRM
  // requires downloading and installing proprietary binaries, which users on
  // an open source operating systems didn't opt into. The first time a site
  // using EME is encountered, the user will be prompted to enable DRM,
  // whereupon the EME plugin binaries will be downloaded if permission is
  // granted.
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "media.eme.enabled",
   MediaEmeEnabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "media.clearkey.persistent-license.enabled",
   MediaClearkeyPersistentLicenseEnabled,
  bool, false
)

#if defined(XP_LINUX) && defined(MOZ_GMP_SANDBOX)
// Whether to allow, on a Linux system that doesn't support the necessary
// sandboxing features, loading Gecko Media Plugins unsandboxed.  However, EME
// CDMs will not be loaded without sandboxing even if this pref is changed.
VARCACHE_PREF(
  "media.gmp.insecure.allow",
   MediaGmpInsecureAllow,
  bool, false
)
#endif

// Specifies whether the PDMFactory can create a test decoder that just outputs
// blank frames/audio instead of actually decoding. The blank decoder works on
// all platforms.
VARCACHE_PREF(
  "media.use-blank-decoder",
   MediaUseBlankDecoder,
  RelaxedAtomicBool, false
)

#if defined(XP_WIN)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.gpu-process-decoder",
   MediaGpuProcessDecoder,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

#if defined(XP_WIN) && !defined(_ARM64_)
# define PREF_VALUE true
#elif defined(XP_MACOSX)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.rdd-process.enabled",
   MediaRddProcessEnabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "media.rdd-process.startup_timeout_ms",
   MediaRddProcessStartupTimeoutMs,
  RelaxedAtomicInt32, 5000
)

VARCACHE_PREF(
  "media.rdd-vorbis.enabled",
   MediaRddVorbisEnabled,
  RelaxedAtomicBool, false
)

#ifdef ANDROID

// Enable the MediaCodec PlatformDecoderModule by default.
VARCACHE_PREF(
  "media.android-media-codec.enabled",
   MediaAndroidMediaCodecEnabled,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "media.android-media-codec.preferred",
   MediaAndroidMediaCodecPreferred,
  RelaxedAtomicBool, true
)

#endif // ANDROID

// WebRTC
#ifdef MOZ_WEBRTC
#ifdef ANDROID

VARCACHE_PREF(
  "media.navigator.hardware.vp8_encode.acceleration_remote_enabled",
   MediaNavigatorHardwareVp8encodeAccelerationRemoteEnabled,
  bool, true
)

PREF("media.navigator.hardware.vp8_encode.acceleration_enabled", bool, true)

PREF("media.navigator.hardware.vp8_decode.acceleration_enabled", bool, false)

#endif // ANDROID

// Use MediaDataDecoder API for VP8/VP9 in WebRTC. This includes hardware
// acceleration for decoding.
// disable on android bug 1509316
#if defined(NIGHTLY_BUILD) && !defined(ANDROID)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.navigator.mediadatadecoder_vpx_enabled",
   MediaNavigatorMediadatadecoderVPXEnabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

// Use MediaDataDecoder API for H264 in WebRTC. This includes hardware
// acceleration for decoding.
# if defined(ANDROID)
#  define PREF_VALUE false // Bug 1509316
# else
#  define PREF_VALUE true
# endif

VARCACHE_PREF(
  "media.navigator.mediadatadecoder_h264_enabled",
   MediaNavigatorMediadatadecoderH264Enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

#endif // MOZ_WEBRTC

#ifdef MOZ_OMX
VARCACHE_PREF(
  "media.omx.enabled",
   MediaOmxEnabled,
  bool, false
)
#endif

#ifdef MOZ_FFMPEG

# if defined(XP_MACOSX)
#  define PREF_VALUE false
# else
#  define PREF_VALUE true
# endif
VARCACHE_PREF(
  "media.ffmpeg.enabled",
   MediaFfmpegEnabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "media.libavcodec.allow-obsolete",
   MediaLibavcodecAllowObsolete,
  bool, false
)

#endif // MOZ_FFMPEG

#ifdef MOZ_FFVPX
VARCACHE_PREF(
  "media.ffvpx.enabled",
   MediaFfvpxEnabled,
  RelaxedAtomicBool, true
)
#endif

#if defined(MOZ_FFMPEG) || defined(MOZ_FFVPX)
VARCACHE_PREF(
  "media.ffmpeg.low-latency.enabled",
   MediaFfmpegLowLatencyEnabled,
  RelaxedAtomicBool, false
)
#endif

#ifdef MOZ_WMF

VARCACHE_PREF(
  "media.wmf.enabled",
   MediaWmfEnabled,
  RelaxedAtomicBool, true
)

// Whether DD should consider WMF-disabled a WMF failure, useful for testing.
VARCACHE_PREF(
  "media.decoder-doctor.wmf-disabled-is-failure",
   MediaDecoderDoctorWmfDisabledIsFailure,
  bool, false
)

VARCACHE_PREF(
  "media.wmf.vp9.enabled",
   MediaWmfVp9Enabled,
  RelaxedAtomicBool, true
)

#endif // MOZ_WMF

// Whether to check the decoder supports recycling.
#ifdef ANDROID
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.decoder.recycle.enabled",
   MediaDecoderRecycleEnabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

// Should MFR try to skip to the next key frame?
VARCACHE_PREF(
  "media.decoder.skip-to-next-key-frame.enabled",
   MediaDecoderSkipToNextKeyFrameEnabled,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "media.gmp.decoder.enabled",
   MediaGmpDecoderEnabled,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "media.eme.audio.blank",
   MediaEmeAudioBlank,
  RelaxedAtomicBool, false
)
VARCACHE_PREF(
  "media.eme.video.blank",
   MediaEmeVideoBlank,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "media.eme.chromium-api.video-shmems",
   MediaEmeChromiumApiVideoShmems,
  RelaxedAtomicUint32, 6
)

// Whether to suspend decoding of videos in background tabs.
VARCACHE_PREF(
  "media.suspend-bkgnd-video.enabled",
   MediaSuspendBkgndVideoEnabled,
  RelaxedAtomicBool, true
)

// Delay, in ms, from time window goes to background to suspending
// video decoders. Defaults to 10 seconds.
VARCACHE_PREF(
  "media.suspend-bkgnd-video.delay-ms",
   MediaSuspendBkgndVideoDelayMs,
  RelaxedAtomicUint32, 10000
)

VARCACHE_PREF(
  "media.dormant-on-pause-timeout-ms",
   MediaDormantOnPauseTimeoutMs,
  RelaxedAtomicInt32, 5000
)

VARCACHE_PREF(
  "media.webspeech.synth.force_global_queue",
   MediaWebspeechSynthForceGlobalQueue,
  bool, false
)

VARCACHE_PREF(
  "media.webspeech.test.enable",
   MediaWebspeechTestEnable,
  bool, false
)

VARCACHE_PREF(
  "media.webspeech.test.fake_fsm_events",
   MediaWebspeechTextFakeFsmEvents,
  bool, false
)

VARCACHE_PREF(
  "media.webspeech.test.fake_recognition_service",
   MediaWebspeechTextFakeRecognitionService,
  bool, false
)

#ifdef MOZ_WEBSPEECH
VARCACHE_PREF(
  "media.webspeech.recognition.enable",
   MediaWebspeechRecognitionEnable,
  bool, false
)
#endif

VARCACHE_PREF(
  "media.webspeech.recognition.force_enable",
   MediaWebspeechRecognitionForceEnable,
  bool, false
)

#if defined(MOZ_WEBM_ENCODER)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.encoder.webm.enabled",
   MediaEncoderWebMEnabled,
  RelaxedAtomicBool, true
)
#undef PREF_VALUE

#if defined(RELEASE_OR_BETA)
# define PREF_VALUE 3
#else
  // Zero tolerance in pre-release builds to detect any decoder regression.
# define PREF_VALUE 0
#endif
VARCACHE_PREF(
  "media.audio-max-decode-error",
   MediaAudioMaxDecodeError,
  uint32_t, PREF_VALUE
)
#undef PREF_VALUE

#if defined(RELEASE_OR_BETA)
# define PREF_VALUE 2
#else
  // Zero tolerance in pre-release builds to detect any decoder regression.
# define PREF_VALUE 0
#endif
VARCACHE_PREF(
  "media.video-max-decode-error",
   MediaVideoMaxDecodeError,
  uint32_t, PREF_VALUE
)
#undef PREF_VALUE

// Opus
VARCACHE_PREF(
  "media.opus.enabled",
   MediaOpusEnabled,
  RelaxedAtomicBool, true
)

// Wave
VARCACHE_PREF(
  "media.wave.enabled",
   MediaWaveEnabled,
  RelaxedAtomicBool, true
)

// Ogg
VARCACHE_PREF(
  "media.ogg.enabled",
   MediaOggEnabled,
  RelaxedAtomicBool, true
)

// WebM
VARCACHE_PREF(
  "media.webm.enabled",
   MediaWebMEnabled,
  RelaxedAtomicBool, true
)

// AV1
#if defined(XP_WIN) && !defined(_ARM64_)
# define PREF_VALUE true
#elif defined(XP_MACOSX)
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.av1.enabled",
   MediaAv1Enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE
VARCACHE_PREF(
  "media.av1.use-dav1d",
   MediaAv1UseDav1d,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "media.flac.enabled",
   MediaFlacEnabled,
  bool, true
)

// Hls
#ifdef ANDROID
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.hls.enabled",
   MediaHlsEnabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Max number of HLS players that can be created concurrently. Used only on
// Android and when "media.hls.enabled" is true.
#ifdef ANDROID
VARCACHE_PREF(
  "media.hls.max-allocations",
   MediaHlsMaxAllocations,
  uint32_t, 20
)
#endif

#ifdef MOZ_FMP4
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.mp4.enabled",
   MediaMp4Enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

// Error/warning handling, Decoder Doctor.
//
// Set to true to force demux/decode warnings to be treated as errors.
VARCACHE_PREF(
  "media.playback.warnings-as-errors",
   MediaPlaybackWarningsAsErrors,
  RelaxedAtomicBool, false
)

// Resume video decoding when the cursor is hovering on a background tab to
// reduce the resume latency and improve the user experience.
VARCACHE_PREF(
  "media.resume-bkgnd-video-on-tabhover",
   MediaResumeBkgndVideoOnTabhover,
  bool, true
)

#ifdef ANDROID
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "media.videocontrols.lock-video-orientation",
   MediaVideocontrolsLockVideoOrientation,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Media Seamless Looping
VARCACHE_PREF(
  "media.seamless-looping",
   MediaSeamlessLooping,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "media.autoplay.block-event.enabled",
   MediaBlockEventEnabled,
  bool, false
)

VARCACHE_PREF(
  "media.media-capabilities.enabled",
   MediaCapabilitiesEnabled,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "media.media-capabilities.screen.enabled",
   MediaCapabilitiesScreenEnabled,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "media.benchmark.vp9.fps",
   MediaBenchmarkVp9Fps,
  RelaxedAtomicUint32, 0
)

VARCACHE_PREF(
  "media.benchmark.vp9.threshold",
   MediaBenchmarkVp9Threshold,
  RelaxedAtomicUint32, 150
)

VARCACHE_PREF(
  "media.benchmark.vp9.versioncheck",
   MediaBenchmarkVp9Versioncheck,
  RelaxedAtomicUint32, 0
)

VARCACHE_PREF(
  "media.benchmark.frames",
   MediaBenchmarkFrames,
  RelaxedAtomicUint32, 300
)

VARCACHE_PREF(
  "media.benchmark.timeout",
   MediaBenchmarkTimeout,
  RelaxedAtomicUint32, 1000
)

VARCACHE_PREF(
  "media.test.video-suspend",
   MediaTestVideoSuspend,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "media.autoplay.allow-muted",
   MediaAutoplayAllowMuted,
  RelaxedAtomicBool, true
)

VARCACHE_PREF(
  "media.autoplay.blackList-override-default",
   MediaAutoplayBlackListOverrideDefault,
  RelaxedAtomicBool, true
)

//---------------------------------------------------------------------------
// Network prefs
//---------------------------------------------------------------------------

// Sub-resources HTTP-authentication:
//   0 - don't allow sub-resources to open HTTP authentication credentials
//       dialogs
//   1 - allow sub-resources to open HTTP authentication credentials dialogs,
//       but don't allow it for cross-origin sub-resources
//   2 - allow the cross-origin authentication as well.
VARCACHE_PREF(
  "network.auth.subresource-http-auth-allow",
   network_auth_subresource_http_auth_allow,
  uint32_t, 2
)

// Sub-resources HTTP-authentication for cross-origin images:
// - true: It is allowed to present http auth. dialog for cross-origin images.
// - false: It is not allowed.
// If network.auth.subresource-http-auth-allow has values 0 or 1 this pref does
// not have any effect.
VARCACHE_PREF(
  "network.auth.subresource-img-cross-origin-http-auth-allow",
   network_auth_subresource_img_cross_origin_http_auth_allow,
  bool, false
)

// Resources that are triggered by some non-web-content:
// - true: They are allow to present http auth. dialog
// - false: They are not allow to present http auth. dialog.
VARCACHE_PREF(
  "network.auth.non-web-content-triggered-resources-http-auth-allow",
   network_auth_non_web_content_triggered_resources_http_auth_allow,
  bool, false
)

// 0-Accept, 1-dontAcceptForeign, 2-dontAcceptAny, 3-limitForeign,
// 4-rejectTracker
// Keep the old default of accepting all cookies
VARCACHE_PREF(
  "network.cookie.cookieBehavior",
  network_cookie_cookieBehavior,
  RelaxedAtomicInt32, 0
)

// Enables the predictive service.
VARCACHE_PREF(
  "network.predictor.enabled",
   network_predictor_enabled,
  bool, true
)

VARCACHE_PREF(
  "network.predictor.enable-hover-on-ssl",
   network_predictor_enable_hover_on_ssl,
  bool, false
)

VARCACHE_PREF(
  "network.predictor.enable-prefetch",
   network_predictor_enable_prefetch,
  bool, false
)

VARCACHE_PREF(
  "network.predictor.page-degradation.day",
   network_predictor_page_degradation_day,
  int32_t, 0
)
VARCACHE_PREF(
  "network.predictor.page-degradation.week",
   network_predictor_page_degradation_week,
  int32_t, 5
)
VARCACHE_PREF(
  "network.predictor.page-degradation.month",
   network_predictor_page_degradation_month,
  int32_t, 10
)
VARCACHE_PREF(
  "network.predictor.page-degradation.year",
   network_predictor_page_degradation_year,
  int32_t, 25
)
VARCACHE_PREF(
  "network.predictor.page-degradation.max",
   network_predictor_page_degradation_max,
  int32_t, 50
)

VARCACHE_PREF(
  "network.predictor.subresource-degradation.day",
   network_predictor_subresource_degradation_day,
  int32_t, 1
)
VARCACHE_PREF(
  "network.predictor.subresource-degradation.week",
   network_predictor_subresource_degradation_week,
  int32_t, 10
)
VARCACHE_PREF(
  "network.predictor.subresource-degradation.month",
   network_predictor_subresource_degradation_month,
  int32_t, 25
)
VARCACHE_PREF(
  "network.predictor.subresource-degradation.year",
   network_predictor_subresource_degradation_year,
  int32_t, 50
)
VARCACHE_PREF(
  "network.predictor.subresource-degradation.max",
   network_predictor_subresource_degradation_max,
  int32_t, 100
)

VARCACHE_PREF(
  "network.predictor.prefetch-rolling-load-count",
   network_predictor_prefetch_rolling_load_count,
  int32_t, 10
)

VARCACHE_PREF(
  "network.predictor.prefetch-min-confidence",
   network_predictor_prefetch_min_confidence,
  int32_t, 100
)
VARCACHE_PREF(
  "network.predictor.preconnect-min-confidence",
   network_predictor_preconnect_min_confidence,
  int32_t, 90
)
VARCACHE_PREF(
  "network.predictor.preresolve-min-confidence",
   network_predictor_preresolve_min_confidence,
  int32_t, 60
)

VARCACHE_PREF(
  "network.predictor.prefetch-force-valid-for",
   network_predictor_prefetch_force_valid_for,
  int32_t, 10
)

VARCACHE_PREF(
  "network.predictor.max-resources-per-entry",
   network_predictor_max_resources_per_entry,
  int32_t, 100
)

// This is selected in concert with max-resources-per-entry to keep memory
// usage low-ish. The default of the combo of the two is ~50k.
VARCACHE_PREF(
  "network.predictor.max-uri-length",
   network_predictor_max_uri_length,
  uint32_t, 500
)

PREF("network.predictor.cleaned-up", bool, false)

// A testing flag.
VARCACHE_PREF(
  "network.predictor.doing-tests",
   network_predictor_doing_tests,
  bool, false
)

//---------------------------------------------------------------------------
// ContentSessionStore prefs
//---------------------------------------------------------------------------
// Maximum number of bytes of DOMSessionStorage data we collect per origin.
VARCACHE_PREF(
  "browser.sessionstore.dom_storage_limit",
  browser_sessionstore_dom_storage_limit,
  uint32_t, 2048
)

//---------------------------------------------------------------------------
// Preferences prefs
//---------------------------------------------------------------------------

PREF("preferences.allow.omt-write", bool, true)

//---------------------------------------------------------------------------
// Privacy prefs
//---------------------------------------------------------------------------

// Whether Content Blocking Third-Party Cookies UI has been enabled.
VARCACHE_PREF(
  "browser.contentblocking.allowlist.storage.enabled",
   browser_contentblocking_allowlist_storage_enabled,
  bool, false
)

VARCACHE_PREF(
  "browser.contentblocking.allowlist.annotations.enabled",
   browser_contentblocking_allowlist_annotations_enabled,
  bool, true
)

// How many recent block/unblock actions per origins we remember in the
// Content Blocking log for each top-level window.
VARCACHE_PREF(
  "browser.contentblocking.originlog.length",
   browser_contentblocking_originlog_length,
  uint32_t, 32
)

// Annotate channels based on the tracking protection list in all modes
VARCACHE_PREF(
  "privacy.trackingprotection.annotate_channels",
   privacy_trackingprotection_annotate_channels,
  bool, true
)

// Block 3rd party fingerprinting resources.
# define PREF_VALUE false
VARCACHE_PREF(
  "privacy.trackingprotection.fingerprinting.enabled",
   privacy_trackingprotection_fingerprinting_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Block 3rd party cryptomining resources.
# define PREF_VALUE false
VARCACHE_PREF(
  "privacy.trackingprotection.cryptomining.enabled",
   privacy_trackingprotection_cryptomining_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Lower the priority of network loads for resources on the tracking protection
// list.  Note that this requires the
// privacy.trackingprotection.annotate_channels pref to be on in order to have
// any effect.
#ifdef NIGHTLY_BUILD
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "privacy.trackingprotection.lower_network_priority",
   privacy_trackingprotection_lower_network_priority,
  bool, PREF_VALUE
)
#undef PREF_VALUE

// Anti-tracking permission expiration
VARCACHE_PREF(
  "privacy.restrict3rdpartystorage.expiration",
   privacy_restrict3rdpartystorage_expiration,
  uint32_t, 2592000 // 30 days (in seconds)
)

// Anti-tracking user-interaction expiration
VARCACHE_PREF(
  "privacy.userInteraction.expiration",
   privacy_userInteraction_expiration,
  uint32_t, 2592000 // 30 days (in seconds)
)

// Anti-tracking user-interaction document interval
VARCACHE_PREF(
  "privacy.userInteraction.document.interval",
   privacy_userInteraction_document_interval,
  uint32_t, 1800 // 30 minutes (in seconds)
)

// Maximum client-side cookie life-time cap
VARCACHE_PREF(
  "privacy.documentCookies.maxage",
   privacy_documentCookies_maxage,
  uint32_t, 0 // Disabled (in seconds, set to 0 to disable)
)

// Anti-fingerprinting, disabled by default
VARCACHE_PREF(
  "privacy.resistFingerprinting",
   privacy_resistFingerprinting,
  RelaxedAtomicBool, false
)

VARCACHE_PREF(
  "privacy.resistFingerprinting.autoDeclineNoUserInputCanvasPrompts",
   privacy_resistFingerprinting_autoDeclineNoUserInputCanvasPrompts,
  RelaxedAtomicBool, false
)

// Password protection
VARCACHE_PREF(
  "browser.safebrowsing.passwords.enabled",
   browser_safebrowsing_passwords_enabled,
  bool, false
)

// Malware protection
VARCACHE_PREF(
  "browser.safebrowsing.malware.enabled",
   browser_safebrowsing_malware_enabled,
  bool, true
)

// Phishing protection
VARCACHE_PREF(
  "browser.safebrowsing.phishing.enabled",
   browser_safebrowsing_phishing_enabled,
  bool, true
)

// Blocked plugin content
VARCACHE_PREF(
  "browser.safebrowsing.blockedURIs.enabled",
   browser_safebrowsing_blockedURIs_enabled,
  bool, true
)

// Prevent system colors from being exposed to CSS or canvas.
VARCACHE_PREF(
  "ui.use_standins_for_native_colors",
   ui_use_standins_for_native_colors,
   RelaxedAtomicBool, false
)

//---------------------------------------------------------------------------
// ChannelClassifier prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "channelclassifier.allowlist_example",
   channelclassifier_allowlist_example,
  bool, false
)

//---------------------------------------------------------------------------
// Security prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "security.csp.enable",
   security_csp_enable,
  bool, true
)

VARCACHE_PREF(
  "security.csp.experimentalEnabled",
   security_csp_experimentalEnabled,
  bool, false
)

VARCACHE_PREF(
  "security.csp.enableStrictDynamic",
   security_csp_enableStrictDynamic,
  bool, true
)

VARCACHE_PREF(
  "security.csp.reporting.script-sample.max-length",
   security_csp_reporting_script_sample_max_length,
  int32_t, 40
)

//---------------------------------------------------------------------------
// View source prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "view_source.editor.external",
   view_source_editor_external,
  bool, false
)

//---------------------------------------------------------------------------
// DevTools prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "devtools.enabled",
   devtools_enabled,
  RelaxedAtomicBool, false
)

#ifdef MOZILLA_OFFICIAL
# define PREF_VALUE false
#else
# define PREF_VALUE true
#endif
VARCACHE_PREF(
  "devtools.console.stdout.chrome",
   devtools_console_stdout_chrome,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "devtools.console.stdout.content",
   devtools_console_stdout_content,
  RelaxedAtomicBool, false
)

//---------------------------------------------------------------------------
// Feature-Policy prefs
//---------------------------------------------------------------------------

#ifdef NIGHTLY_BUILD
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
// This pref enables FeaturePolicy logic and the parsing of 'allow' attribute in
// HTMLIFrameElement objects.
VARCACHE_PREF(
  "dom.security.featurePolicy.enabled",
   dom_security_featurePolicy_enabled,
  bool, PREF_VALUE
)

// This pref enables the featurePolicy header support.
VARCACHE_PREF(
  "dom.security.featurePolicy.header.enabled",
   dom_security_featurePolicy_header_enabled,
  bool, PREF_VALUE
)

// Expose the 'policy' attribute in document and HTMLIFrameElement
VARCACHE_PREF(
  "dom.security.featurePolicy.webidl.enabled",
   dom_security_featurePolicy_webidl_enabled,
  bool, PREF_VALUE
)
#undef PREF_VALUE

//---------------------------------------------------------------------------
// Plugins prefs
//---------------------------------------------------------------------------

VARCACHE_PREF(
  "plugins.flashBlock.enabled",
   plugins_flashBlock_enabled,
  bool, false
)

VARCACHE_PREF(
  "plugins.http_https_only",
   plugins_http_https_only,
  bool, true
)

//---------------------------------------------------------------------------
// Reporting API
//---------------------------------------------------------------------------

#ifdef NIGHTLY_BUILD
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "dom.reporting.enabled",
   dom_reporting_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "dom.reporting.testing.enabled",
   dom_reporting_testing_enabled,
  RelaxedAtomicBool, false
)

#ifdef NIGHTLY_BUILD
# define PREF_VALUE true
#else
# define PREF_VALUE false
#endif
VARCACHE_PREF(
  "dom.reporting.featurePolicy.enabled",
   dom_reporting_featurePolicy_enabled,
  RelaxedAtomicBool, PREF_VALUE
)
#undef PREF_VALUE

VARCACHE_PREF(
  "dom.reporting.header.enabled",
   dom_reporting_header_enabled,
  RelaxedAtomicBool, false
)

// In seconds. The timeout to remove not-active report-to endpoints.
VARCACHE_PREF(
  "dom.reporting.cleanup.timeout",
   dom_reporting_cleanup_timeout,
  uint32_t, 3600
)

// Any X seconds the reports are dispatched to endpoints.
VARCACHE_PREF(
  "dom.reporting.delivering.timeout",
   dom_reporting_delivering_timeout,
  uint32_t, 5
)

// How many times the delivering of a report should be tried.
VARCACHE_PREF(
  "dom.reporting.delivering.maxFailures",
   dom_reporting_delivering_maxFailures,
  uint32_t, 3
)

// How many reports should be stored in the report queue before being delivered.
VARCACHE_PREF(
  "dom.reporting.delivering.maxReports",
   dom_reporting_delivering_maxReports,
  uint32_t, 100
)

VARCACHE_PREF(
  "medium_high_event_queue.enabled",
   medium_high_event_queue_enabled,
  RelaxedAtomicBool, true
)

//---------------------------------------------------------------------------
// End of prefs
//---------------------------------------------------------------------------

// clang-format on
