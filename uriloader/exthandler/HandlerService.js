/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

const {OS} = ChromeUtils.import("resource://gre/modules/osfile.jsm");
const {XPCOMUtils} = ChromeUtils.import("resource://gre/modules/XPCOMUtils.jsm");
const {Services} = ChromeUtils.import("resource://gre/modules/Services.jsm");

ChromeUtils.defineModuleGetter(this, "FileUtils",
                               "resource://gre/modules/FileUtils.jsm");
ChromeUtils.defineModuleGetter(this, "JSONFile",
                               "resource://gre/modules/JSONFile.jsm");

XPCOMUtils.defineLazyServiceGetter(this, "gExternalProtocolService",
                                   "@mozilla.org/uriloader/external-protocol-service;1",
                                   "nsIExternalProtocolService");
XPCOMUtils.defineLazyServiceGetter(this, "gMIMEService",
                                   "@mozilla.org/mime;1",
                                   "nsIMIMEService");

function HandlerService() {
  // Observe handlersvc-json-replace so we can switch to the datasource
  Services.obs.addObserver(this, "handlersvc-json-replace", true);
}

HandlerService.prototype = {

  classID: Components.ID("{220cc253-b60f-41f6-b9cf-fdcb325f970f}"),
  QueryInterface: ChromeUtils.generateQI([
    Ci.nsISupportsWeakReference,
    Ci.nsIHandlerService,
    Ci.nsIObserver
  ]),

  __store: null,
  get _store() {
    if (!this.__store) {
      this.__store = new JSONFile({
        path: OS.Path.join(OS.Constants.Path.profileDir, "handlers.json"),
        dataPostProcessor: this._dataPostProcessor.bind(this),
      });
    }

    // Always call this even if this.__store was set, since it may have been
    // set by asyncInit, which might not have completed yet.
    this._ensureStoreInitialized();
    return this.__store;
  },

  __storeInitialized: false,
  _ensureStoreInitialized() {
    if (!this.__storeInitialized) {
      this.__storeInitialized = true;
      this.__store.ensureDataReady();

      this._injectDefaultProtocolHandlersIfNeeded();
      this._migrateProtocolHandlersIfNeeded();

      Services.obs.notifyObservers(null, "handlersvc-store-initialized");
    }
  },

  _dataPostProcessor(data) {
    return data.defaultHandlersVersion ? data : {
      defaultHandlersVersion: {},
      mimeTypes: {},
      schemes: {},
    };
  },

  /**
   * Injects new default protocol handlers if the version in the preferences is
   * newer than the one in the data store.
   */
  _injectDefaultProtocolHandlersIfNeeded() {
    let prefsDefaultHandlersVersion;
    try {
      prefsDefaultHandlersVersion = Services.prefs.getComplexValue(
        "gecko.handlerService.defaultHandlersVersion",
        Ci.nsIPrefLocalizedString);
    } catch (ex) {
      if (ex instanceof Components.Exception &&
          ex.result == Cr.NS_ERROR_UNEXPECTED) {
        // This platform does not have any default protocol handlers configured.
        return;
      }
      throw ex;
    }

    try {
      prefsDefaultHandlersVersion = Number(prefsDefaultHandlersVersion.data);
      let locale = Services.locale.appLocaleAsLangTag;

      let defaultHandlersVersion =
          this._store.data.defaultHandlersVersion[locale] || 0;
      if (defaultHandlersVersion < prefsDefaultHandlersVersion) {
        this._injectDefaultProtocolHandlers();
        this._store.data.defaultHandlersVersion[locale] =
          prefsDefaultHandlersVersion;
      }
    } catch (ex) {
      Cu.reportError(ex);
    }
  },

  _injectDefaultProtocolHandlers() {
    let schemesPrefBranch = Services.prefs.getBranch("gecko.handlerService.schemes.");
    let schemePrefList = schemesPrefBranch.getChildList("");

    let schemes = {};

    // read all the scheme prefs into a hash
    for (let schemePrefName of schemePrefList) {

      let [scheme, handlerNumber, attribute] = schemePrefName.split(".");

      try {
        let attrData =
          schemesPrefBranch.getComplexValue(schemePrefName,
                                            Ci.nsIPrefLocalizedString).data;
        if (!(scheme in schemes)) {
          schemes[scheme] = {};
        }

        if (!(handlerNumber in schemes[scheme])) {
          schemes[scheme][handlerNumber] = {};
        }

        schemes[scheme][handlerNumber][attribute] = attrData;
      } catch (ex) {}
    }

    for (let scheme of Object.keys(schemes)) {
      let protoInfo = gExternalProtocolService.getProtocolHandlerInfo(scheme);

      // cache the possible handlers to avoid extra xpconnect traversals.
      let possibleHandlers = protoInfo.possibleApplicationHandlers;

      for (let handlerNumber of Object.keys(schemes[scheme])) {
        let handlerApp = this.handlerAppFromSerializable(schemes[scheme][handlerNumber]);
        // If there is already a handler registered with the same template
        // URL, the newly added one will be ignored when saving.
        possibleHandlers.appendElement(handlerApp, false);
      }

      this.store(protoInfo);
    }
  },

  /**
   * Execute any migrations. Migrations are defined here for any changes or removals for
   * existing handlers. Additions are still handled via the localized prefs infrastructure.
   *
   * This depends on the browser.handlers.migrations pref being set by migrateUI in
   * nsBrowserGlue (for Fx Desktop) or similar mechanisms for other products.
   * This is a comma-separated list of identifiers of migrations that need running.
   * This avoids both re-running older migrations and keeping an additional
   * pref around permanently.
   */
  _migrateProtocolHandlersIfNeeded() {
    const kMigrations = {
      "30boxes": () => {
        const k30BoxesRegex = /^https?:\/\/(?:www\.)?30boxes.com\/external\/widget/i;
        let webcalHandler = gExternalProtocolService.getProtocolHandlerInfo("webcal");
        if (this.exists(webcalHandler)) {
          this.fillHandlerInfo(webcalHandler, "");
          let shouldStore = false;
          // First remove 30boxes from possible handlers.
          let handlers = webcalHandler.possibleApplicationHandlers;
          for (let i = handlers.length - 1; i >= 0; i--) {
            let app = handlers.queryElementAt(i, Ci.nsIHandlerApp);
            if (app instanceof Ci.nsIWebHandlerApp &&
                k30BoxesRegex.test(app.uriTemplate)) {
              shouldStore = true;
              handlers.removeElementAt(i);
            }
          }
          // Then remove as a preferred handler.
          if (webcalHandler.preferredApplicationHandler) {
            let app = webcalHandler.preferredApplicationHandler;
            if (app instanceof Ci.nsIWebHandlerApp &&
                k30BoxesRegex.test(app.uriTemplate)) {
              webcalHandler.preferredApplicationHandler = null;
              shouldStore = true;
            }
          }
          // Then store, if we changed anything.
          if (shouldStore) {
            this.store(webcalHandler);
          }
        }
      },
    };
    let migrationsToRun = Services.prefs.getCharPref("browser.handlers.migrations", "");
    migrationsToRun = migrationsToRun ? migrationsToRun.split(",") : [];
    for (let migration of migrationsToRun) {
      migration.trim();
      try {
        kMigrations[migration]();
      } catch (ex) {
        Cu.reportError(ex);
      }
    }

    if (migrationsToRun.length) {
      Services.prefs.clearUserPref("browser.handlers.migrations");
    }
  },

  _onDBChange() {
    return (async () => {
      if (this.__store) {
        await this.__store.finalize();
      }
      this.__store = null;
      this.__storeInitialized = false;
    })().catch(Cu.reportError);
  },

  // nsIObserver
  observe(subject, topic, data) {
    if (topic != "handlersvc-json-replace") {
      return;
    }
    let promise = this._onDBChange();
    promise.then(() => {
      Services.obs.notifyObservers(null, "handlersvc-json-replace-complete");
    });
  },

  // nsIHandlerService
  asyncInit() {
    if (!this.__store) {
      this.__store = new JSONFile({
        path: OS.Path.join(OS.Constants.Path.profileDir, "handlers.json"),
        dataPostProcessor: this._dataPostProcessor.bind(this),
      });
      this.__store.load().then(() => {
        // __store can be null if we called _onDBChange in the mean time.
        if (this.__store) {
          this._ensureStoreInitialized();
        }
      }).catch(Cu.reportError);
    }
  },

  // nsIHandlerService
  enumerate() {
    let handlers = Cc["@mozilla.org/array;1"]
                     .createInstance(Ci.nsIMutableArray);
    for (let type of Object.keys(this._store.data.mimeTypes)) {
      let handler = gMIMEService.getFromTypeAndExtension(type, null);
      handlers.appendElement(handler);
    }
    for (let type of Object.keys(this._store.data.schemes)) {
      // nsIExternalProtocolService.getProtocolHandlerInfo can be expensive
      // on Windows, so we return a proxy to delay retrieving the nsIHandlerInfo
      // until one of its properties is accessed.
      //
      // Note: our caller still needs to yield periodically when iterating
      // the enumerator and accessing handler properties to avoid monopolizing
      // the main thread.
      //
      let handler = new Proxy(
        {
          QueryInterface: ChromeUtils.generateQI([Ci.nsIHandlerInfo]),
          type: type,
          get _handlerInfo() {
            delete this._handlerInfo;
            return this._handlerInfo = gExternalProtocolService.getProtocolHandlerInfo(type);
          },
        },
        {
          get: function(target, name) {
            return target[name] || target._handlerInfo[name];
          },
          set: function(target, name, value) {
            target._handlerInfo[name] = value;
          },
        },
      );
      handlers.appendElement(handler);
    }
    return handlers.enumerate(Ci.nsIHandlerInfo);
  },

  // nsIHandlerService
  store(handlerInfo) {
    let handlerList = this._getHandlerListByHandlerInfoType(handlerInfo);

    // Retrieve an existing entry if present, instead of creating a new one, so
    // that we preserve unknown properties for forward compatibility.
    let storedHandlerInfo = handlerList[handlerInfo.type];
    if (!storedHandlerInfo) {
      storedHandlerInfo = {};
      handlerList[handlerInfo.type] = storedHandlerInfo;
    }

    // Only a limited number of preferredAction values is allowed.
    if (handlerInfo.preferredAction == Ci.nsIHandlerInfo.saveToDisk ||
        handlerInfo.preferredAction == Ci.nsIHandlerInfo.useSystemDefault ||
        handlerInfo.preferredAction == Ci.nsIHandlerInfo.handleInternally) {
      storedHandlerInfo.action = handlerInfo.preferredAction;
    } else {
      storedHandlerInfo.action = Ci.nsIHandlerInfo.useHelperApp;
    }

    if (handlerInfo.alwaysAskBeforeHandling) {
      storedHandlerInfo.ask = true;
    } else {
      delete storedHandlerInfo.ask;
    }

    // Build a list of unique nsIHandlerInfo instances to process later.
    let handlers = [];
    if (handlerInfo.preferredApplicationHandler) {
      handlers.push(handlerInfo.preferredApplicationHandler);
    }
    for (let handler of handlerInfo.possibleApplicationHandlers.enumerate(Ci.nsIHandlerApp)) {
      // If the caller stored duplicate handlers, we save them only once.
      if (!handlers.some(h => h.equals(handler))) {
        handlers.push(handler);
      }
    }

    // If any of the nsIHandlerInfo instances cannot be serialized, it is not
    // included in the final list. The first element is always the preferred
    // handler, or null if there is none.
    let serializableHandlers =
        handlers.map(h => this.handlerAppToSerializable(h)).filter(h => h);
    if (serializableHandlers.length) {
      if (!handlerInfo.preferredApplicationHandler) {
        serializableHandlers.unshift(null);
      }
      storedHandlerInfo.handlers = serializableHandlers;
    } else {
      delete storedHandlerInfo.handlers;
    }

    if (this._isMIMEInfo(handlerInfo)) {
      let extensions = storedHandlerInfo.extensions || [];
      for (let extension of handlerInfo.getFileExtensions()) {
        extension = extension.toLowerCase();
        // If the caller stored duplicate extensions, we save them only once.
        if (!extensions.includes(extension)) {
          extensions.push(extension);
        }
      }
      if (extensions.length) {
        storedHandlerInfo.extensions = extensions;
      } else {
        delete storedHandlerInfo.extensions;
      }
    }

    this._store.saveSoon();
  },

  // nsIHandlerService
  fillHandlerInfo(handlerInfo, overrideType) {
    let type = overrideType || handlerInfo.type;
    let storedHandlerInfo = this._getHandlerListByHandlerInfoType(handlerInfo)[type];
    if (!storedHandlerInfo) {
      throw new Components.Exception("handlerSvc fillHandlerInfo: don't know this type",
                                     Cr.NS_ERROR_NOT_AVAILABLE);
    }

    handlerInfo.preferredAction = storedHandlerInfo.action;
    handlerInfo.alwaysAskBeforeHandling = !!storedHandlerInfo.ask;

    // If the first item is not null, it is also the preferred handler. Since
    // we cannot modify the stored array, use a boolean to keep track of this.
    let isFirstItem = true;
    for (let handler of storedHandlerInfo.handlers || [null]) {
      let handlerApp = this.handlerAppFromSerializable(handler || {});
      if (isFirstItem) {
        isFirstItem = false;
        handlerInfo.preferredApplicationHandler = handlerApp;
      }
      if (handlerApp) {
        handlerInfo.possibleApplicationHandlers.appendElement(handlerApp);
      }
    }

    if (this._isMIMEInfo(handlerInfo) && storedHandlerInfo.extensions) {
      for (let extension of storedHandlerInfo.extensions) {
        handlerInfo.appendExtension(extension);
      }
    }
  },

  /**
   * @param handler
   *        A nsIHandlerApp handler app
   * @returns  Serializable representation of a handler app object.
   */
  handlerAppToSerializable(handler) {
    if (handler instanceof Ci.nsILocalHandlerApp) {
      return {
        name: handler.name,
        path: handler.executable.path,
      };
    } else if (handler instanceof Ci.nsIWebHandlerApp) {
      return {
        name: handler.name,
        uriTemplate: handler.uriTemplate,
      };
    } else if (handler instanceof Ci.nsIDBusHandlerApp) {
      return {
        name: handler.name,
        service: handler.service,
        method: handler.method,
        objectPath: handler.objectPath,
        dBusInterface: handler.dBusInterface,
      };
    } else if (handler instanceof Ci.nsIGIOMimeApp) {
      return {
        name: handler.name,
        command: handler.command,
      };
    }
    // If the handler is an unknown handler type, return null.
    // Android default application handler is the case.
    return null;
  },

  /**
   * @param handlerObj
   *        Serializable representation of a handler object.
   * @returns  {nsIHandlerApp}  the handler app, if any; otherwise null
   */
  handlerAppFromSerializable(handlerObj) {
    let handlerApp;
    if ("path" in handlerObj) {
      try {
        let file = new FileUtils.File(handlerObj.path);
        if (!file.exists()) {
          return null;
        }
        handlerApp = Cc["@mozilla.org/uriloader/local-handler-app;1"]
                       .createInstance(Ci.nsILocalHandlerApp);
        handlerApp.executable = file;
      } catch (ex) {
        return null;
      }
    } else if ("uriTemplate" in handlerObj) {
      handlerApp = Cc["@mozilla.org/uriloader/web-handler-app;1"]
                     .createInstance(Ci.nsIWebHandlerApp);
      handlerApp.uriTemplate = handlerObj.uriTemplate;
    } else if ("service" in handlerObj) {
      handlerApp = Cc["@mozilla.org/uriloader/dbus-handler-app;1"]
                     .createInstance(Ci.nsIDBusHandlerApp);
      handlerApp.service = handlerObj.service;
      handlerApp.method = handlerObj.method;
      handlerApp.objectPath = handlerObj.objectPath;
      handlerApp.dBusInterface = handlerObj.dBusInterface;
    } else if ("command" in handlerObj &&
               "@mozilla.org/gio-service;1" in Cc) {
      try {
        handlerApp = Cc["@mozilla.org/gio-service;1"]
                       .getService(Ci.nsIGIOService)
                       .createAppFromCommand(handlerObj.command, handlerObj.name);
      } catch (ex) {
        return null;
      }
    } else {
      return null;
    }

    handlerApp.name = handlerObj.name;
    return handlerApp;
  },

  /**
   * The function returns a reference to the "mimeTypes" or "schemes" object
   * based on which type of handlerInfo is provided.
   */
  _getHandlerListByHandlerInfoType(handlerInfo) {
    return this._isMIMEInfo(handlerInfo) ? this._store.data.mimeTypes
                                         : this._store.data.schemes;
  },

  /**
   * Determines whether an nsIHandlerInfo instance represents a MIME type.
   */
  _isMIMEInfo(handlerInfo) {
    // We cannot rely only on the instanceof check because on Android both MIME
    // types and protocols are instances of nsIMIMEInfo. We still do the check
    // so that properties of nsIMIMEInfo become available to the callers.
    return handlerInfo instanceof Ci.nsIMIMEInfo &&
           handlerInfo.type.includes("/");
  },

  // nsIHandlerService
  exists(handlerInfo) {
    return handlerInfo.type in this._getHandlerListByHandlerInfoType(handlerInfo);
  },

  // nsIHandlerService
  remove(handlerInfo) {
    delete this._getHandlerListByHandlerInfoType(handlerInfo)[handlerInfo.type];
    this._store.saveSoon();
  },

  // nsIHandlerService
  getTypeFromExtension(fileExtension) {
    let extension = fileExtension.toLowerCase();
    let mimeTypes = this._store.data.mimeTypes;
    for (let type of Object.keys(mimeTypes)) {
      if (mimeTypes[type].extensions &&
          mimeTypes[type].extensions.includes(extension)) {
        return type;
      }
    }
    return "";
  },
};

this.NSGetFactory = XPCOMUtils.generateNSGetFactory([HandlerService]);
