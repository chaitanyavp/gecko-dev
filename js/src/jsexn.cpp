/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * JS standard exception implementation.
 */

#include "jsexn.h"

#include "mozilla/ScopeExit.h"
#include "mozilla/Sprintf.h"

#include <string.h>
#include <utility>

#include "jsapi.h"
#include "jsnum.h"
#include "jstypes.h"
#include "jsutil.h"

#include "gc/FreeOp.h"
#include "gc/Marking.h"
#include "js/CharacterEncoding.h"
#include "js/PropertySpec.h"
#include "js/UniquePtr.h"
#include "js/Wrapper.h"
#include "util/StringBuffer.h"
#include "vm/ErrorObject.h"
#include "vm/GlobalObject.h"
#include "vm/JSContext.h"
#include "vm/JSFunction.h"
#include "vm/JSObject.h"
#include "vm/JSScript.h"
#include "vm/SavedStacks.h"
#include "vm/SelfHosting.h"
#include "vm/StringType.h"

#include "vm/ErrorObject-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/SavedStacks-inl.h"

using namespace js;

static void exn_finalize(FreeOp* fop, JSObject* obj);

static bool exn_toSource(JSContext* cx, unsigned argc, Value* vp);

#define IMPLEMENT_ERROR_PROTO_CLASS(name)                        \
  {                                                              \
    js_Object_str, JSCLASS_HAS_CACHED_PROTO(JSProto_##name),     \
        JS_NULL_CLASS_OPS,                                       \
        &ErrorObject::classSpecs[JSProto_##name - JSProto_Error] \
  }

const Class ErrorObject::protoClasses[JSEXN_ERROR_LIMIT] = {
    IMPLEMENT_ERROR_PROTO_CLASS(Error),

    IMPLEMENT_ERROR_PROTO_CLASS(InternalError),
    IMPLEMENT_ERROR_PROTO_CLASS(EvalError),
    IMPLEMENT_ERROR_PROTO_CLASS(RangeError),
    IMPLEMENT_ERROR_PROTO_CLASS(ReferenceError),
    IMPLEMENT_ERROR_PROTO_CLASS(SyntaxError),
    IMPLEMENT_ERROR_PROTO_CLASS(TypeError),
    IMPLEMENT_ERROR_PROTO_CLASS(URIError),

    IMPLEMENT_ERROR_PROTO_CLASS(DebuggeeWouldRun),
    IMPLEMENT_ERROR_PROTO_CLASS(CompileError),
    IMPLEMENT_ERROR_PROTO_CLASS(LinkError),
    IMPLEMENT_ERROR_PROTO_CLASS(RuntimeError)};

static const JSFunctionSpec error_methods[] = {
    JS_FN(js_toSource_str, exn_toSource, 0, 0),
    JS_SELF_HOSTED_FN(js_toString_str, "ErrorToString", 0, 0), JS_FS_END};

static const JSPropertySpec error_properties[] = {
    JS_STRING_PS("message", "", 0), JS_STRING_PS("name", "Error", 0),
    // Only Error.prototype has .stack!
    JS_PSGS("stack", ErrorObject::getStack, ErrorObject::setStack, 0),
    JS_PS_END};

#define IMPLEMENT_ERROR_PROPERTIES(name) \
  { JS_STRING_PS("message", "", 0), JS_STRING_PS("name", #name, 0), JS_PS_END }

static const JSPropertySpec other_error_properties[JSEXN_ERROR_LIMIT - 1][3] = {
    IMPLEMENT_ERROR_PROPERTIES(InternalError),
    IMPLEMENT_ERROR_PROPERTIES(EvalError),
    IMPLEMENT_ERROR_PROPERTIES(RangeError),
    IMPLEMENT_ERROR_PROPERTIES(ReferenceError),
    IMPLEMENT_ERROR_PROPERTIES(SyntaxError),
    IMPLEMENT_ERROR_PROPERTIES(TypeError),
    IMPLEMENT_ERROR_PROPERTIES(URIError),
    IMPLEMENT_ERROR_PROPERTIES(DebuggeeWouldRun),
    IMPLEMENT_ERROR_PROPERTIES(CompileError),
    IMPLEMENT_ERROR_PROPERTIES(LinkError),
    IMPLEMENT_ERROR_PROPERTIES(RuntimeError)};

#define IMPLEMENT_NATIVE_ERROR_SPEC(name)                                    \
  {                                                                          \
    ErrorObject::createConstructor, ErrorObject::createProto, nullptr,       \
        nullptr, nullptr,                                                    \
        other_error_properties[JSProto_##name - JSProto_Error - 1], nullptr, \
        JSProto_Error                                                        \
  }

#define IMPLEMENT_NONGLOBAL_ERROR_SPEC(name)                                 \
  {                                                                          \
    ErrorObject::createConstructor, ErrorObject::createProto, nullptr,       \
        nullptr, nullptr,                                                    \
        other_error_properties[JSProto_##name - JSProto_Error - 1], nullptr, \
        JSProto_Error | ClassSpec::DontDefineConstructor                     \
  }

const ClassSpec ErrorObject::classSpecs[JSEXN_ERROR_LIMIT] = {
    {ErrorObject::createConstructor, ErrorObject::createProto, nullptr, nullptr,
     error_methods, error_properties},

    IMPLEMENT_NATIVE_ERROR_SPEC(InternalError),
    IMPLEMENT_NATIVE_ERROR_SPEC(EvalError),
    IMPLEMENT_NATIVE_ERROR_SPEC(RangeError),
    IMPLEMENT_NATIVE_ERROR_SPEC(ReferenceError),
    IMPLEMENT_NATIVE_ERROR_SPEC(SyntaxError),
    IMPLEMENT_NATIVE_ERROR_SPEC(TypeError),
    IMPLEMENT_NATIVE_ERROR_SPEC(URIError),

    IMPLEMENT_NONGLOBAL_ERROR_SPEC(DebuggeeWouldRun),
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(CompileError),
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(LinkError),
    IMPLEMENT_NONGLOBAL_ERROR_SPEC(RuntimeError)};

#define IMPLEMENT_ERROR_CLASS(name)                                   \
  {                                                                   \
    js_Error_str, /* yes, really */                                   \
        JSCLASS_HAS_CACHED_PROTO(JSProto_##name) |                    \
            JSCLASS_HAS_RESERVED_SLOTS(ErrorObject::RESERVED_SLOTS) | \
            JSCLASS_BACKGROUND_FINALIZE,                              \
        &ErrorObjectClassOps,                                         \
        &ErrorObject::classSpecs[JSProto_##name - JSProto_Error]      \
  }

static const ClassOps ErrorObjectClassOps = {
    nullptr,               /* addProperty */
    nullptr,               /* delProperty */
    nullptr,               /* enumerate */
    nullptr,               /* newEnumerate */
    nullptr,               /* resolve */
    nullptr,               /* mayResolve */
    exn_finalize, nullptr, /* call        */
    nullptr,               /* hasInstance */
    nullptr,               /* construct   */
    nullptr,               /* trace       */
};

const Class ErrorObject::classes[JSEXN_ERROR_LIMIT] = {
    IMPLEMENT_ERROR_CLASS(Error), IMPLEMENT_ERROR_CLASS(InternalError),
    IMPLEMENT_ERROR_CLASS(EvalError), IMPLEMENT_ERROR_CLASS(RangeError),
    IMPLEMENT_ERROR_CLASS(ReferenceError), IMPLEMENT_ERROR_CLASS(SyntaxError),
    IMPLEMENT_ERROR_CLASS(TypeError), IMPLEMENT_ERROR_CLASS(URIError),
    // These Error subclasses are not accessible via the global object:
    IMPLEMENT_ERROR_CLASS(DebuggeeWouldRun),
    IMPLEMENT_ERROR_CLASS(CompileError), IMPLEMENT_ERROR_CLASS(LinkError),
    IMPLEMENT_ERROR_CLASS(RuntimeError)};

size_t ExtraMallocSize(JSErrorReport* report) {
  if (report->linebuf()) {
    /*
     * Count with null terminator and alignment.
     * See CopyExtraData for the details about alignment.
     */
    return (report->linebufLength() + 1) * sizeof(char16_t) + 1;
  }

  return 0;
}

size_t ExtraMallocSize(JSErrorNotes::Note* note) { return 0; }

bool CopyExtraData(JSContext* cx, uint8_t** cursor, JSErrorReport* copy,
                   JSErrorReport* report) {
  if (report->linebuf()) {
    /*
     * Make sure cursor is properly aligned for char16_t for platforms
     * which need it and it's at the end of the buffer on exit.
     */
    size_t alignment_backlog = 0;
    if (size_t(*cursor) % 2) {
      (*cursor)++;
    } else {
      alignment_backlog = 1;
    }

    size_t linebufSize = (report->linebufLength() + 1) * sizeof(char16_t);
    const char16_t* linebufCopy = (const char16_t*)(*cursor);
    js_memcpy(*cursor, report->linebuf(), linebufSize);
    *cursor += linebufSize + alignment_backlog;
    copy->initBorrowedLinebuf(linebufCopy, report->linebufLength(),
                              report->tokenOffset());
  }

  /* Copy non-pointer members. */
  copy->isMuted = report->isMuted;
  copy->exnType = report->exnType;

  /* Note that this is before it gets flagged with JSREPORT_EXCEPTION */
  copy->flags = report->flags;

  /* Deep copy notes. */
  if (report->notes) {
    auto copiedNotes = report->notes->copy(cx);
    if (!copiedNotes) {
      return false;
    }
    copy->notes = std::move(copiedNotes);
  } else {
    copy->notes.reset(nullptr);
  }

  return true;
}

bool CopyExtraData(JSContext* cx, uint8_t** cursor, JSErrorNotes::Note* copy,
                   JSErrorNotes::Note* report) {
  return true;
}

template <typename T>
static UniquePtr<T> CopyErrorHelper(JSContext* cx, T* report) {
  /*
   * We use a single malloc block to make a deep copy of JSErrorReport or
   * JSErrorNotes::Note, except JSErrorNotes linked from JSErrorReport with
   * the following layout:
   *   JSErrorReport or JSErrorNotes::Note
   *   char array with characters for message_
   *   char array with characters for filename
   *   char16_t array with characters for linebuf (only for JSErrorReport)
   * Such layout together with the properties enforced by the following
   * asserts does not need any extra alignment padding.
   */
  JS_STATIC_ASSERT(sizeof(T) % sizeof(const char*) == 0);
  JS_STATIC_ASSERT(sizeof(const char*) % sizeof(char16_t) == 0);

  size_t filenameSize = report->filename ? strlen(report->filename) + 1 : 0;
  size_t messageSize = 0;
  if (report->message()) {
    messageSize = strlen(report->message().c_str()) + 1;
  }

  /*
   * The mallocSize can not overflow since it represents the sum of the
   * sizes of already allocated objects.
   */
  size_t mallocSize =
      sizeof(T) + messageSize + filenameSize + ExtraMallocSize(report);
  uint8_t* cursor = cx->pod_calloc<uint8_t>(mallocSize);
  if (!cursor) {
    return nullptr;
  }

  UniquePtr<T> copy(new (cursor) T());
  cursor += sizeof(T);

  if (report->message()) {
    copy->initBorrowedMessage((const char*)cursor);
    js_memcpy(cursor, report->message().c_str(), messageSize);
    cursor += messageSize;
  }

  if (report->filename) {
    copy->filename = (const char*)cursor;
    js_memcpy(cursor, report->filename, filenameSize);
    cursor += filenameSize;
  }

  if (!CopyExtraData(cx, &cursor, copy.get(), report)) {
    return nullptr;
  }

  MOZ_ASSERT(cursor == (uint8_t*)copy.get() + mallocSize);

  /* Copy non-pointer members. */
  copy->sourceId = report->sourceId;
  copy->lineno = report->lineno;
  copy->column = report->column;
  copy->errorNumber = report->errorNumber;

  return copy;
}

UniquePtr<JSErrorNotes::Note> js::CopyErrorNote(JSContext* cx,
                                                JSErrorNotes::Note* note) {
  return CopyErrorHelper(cx, note);
}

UniquePtr<JSErrorReport> js::CopyErrorReport(JSContext* cx,
                                             JSErrorReport* report) {
  return CopyErrorHelper(cx, report);
}

struct SuppressErrorsGuard {
  JSContext* cx;
  JS::WarningReporter prevReporter;
  JS::AutoSaveExceptionState prevState;

  explicit SuppressErrorsGuard(JSContext* cx)
      : cx(cx),
        prevReporter(JS::SetWarningReporter(cx, nullptr)),
        prevState(cx) {}

  ~SuppressErrorsGuard() { JS::SetWarningReporter(cx, prevReporter); }
};

// Cut off the stack if it gets too deep (most commonly for infinite recursion
// errors).
static const size_t MAX_REPORTED_STACK_DEPTH = 1u << 7;

static bool CaptureStack(JSContext* cx, MutableHandleObject stack) {
  return CaptureCurrentStack(
      cx, stack, JS::StackCapture(JS::MaxFrames(MAX_REPORTED_STACK_DEPTH)));
}

JSString* js::ComputeStackString(JSContext* cx) {
  SuppressErrorsGuard seg(cx);

  RootedObject stack(cx);
  if (!CaptureStack(cx, &stack)) {
    return nullptr;
  }

  RootedString str(cx);
  if (!BuildStackString(cx, cx->realm()->principals(), stack, &str)) {
    return nullptr;
  }

  return str.get();
}

static void exn_finalize(FreeOp* fop, JSObject* obj) {
  MOZ_ASSERT(fop->maybeOnHelperThread());
  if (JSErrorReport* report = obj->as<ErrorObject>().getErrorReport()) {
    fop->delete_(report);
  }
}

JSErrorReport* js::ErrorFromException(JSContext* cx, HandleObject objArg) {
  // It's ok to UncheckedUnwrap here, since all we do is get the
  // JSErrorReport, and consumers are careful with the information they get
  // from that anyway.  Anyone doing things that would expose anything in the
  // JSErrorReport to page script either does a security check on the
  // JSErrorReport's principal or also tries to do toString on our object and
  // will fail if they can't unwrap it.
  RootedObject obj(cx, UncheckedUnwrap(objArg));
  if (!obj->is<ErrorObject>()) {
    return nullptr;
  }

  JSErrorReport* report = obj->as<ErrorObject>().getOrCreateErrorReport(cx);
  if (!report) {
    MOZ_ASSERT(cx->isThrowingOutOfMemory());
    cx->recoverFromOutOfMemory();
  }

  return report;
}

JS_PUBLIC_API JSObject* JS::ExceptionStackOrNull(HandleObject objArg) {
  ErrorObject* obj = objArg->maybeUnwrapIf<ErrorObject>();
  if (!obj) {
    return nullptr;
  }

  return obj->stack();
}

JS_PUBLIC_API uint64_t JS::ExceptionTimeWarpTarget(JS::HandleValue value) {
  if (!value.isObject()) {
    return 0;
  }

  ErrorObject* obj = value.toObject().maybeUnwrapIf<ErrorObject>();
  if (!obj) {
    return 0;
  }

  return obj->timeWarpTarget();
}

bool Error(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // ECMA ed. 3, 15.11.1 requires Error, etc., to construct even when
  // called as functions, without operator new.  But as we do not give
  // each constructor a distinct JSClass, we must get the exception type
  // ourselves.
  JSExnType exnType =
      JSExnType(args.callee().as<JSFunction>().getExtendedSlot(0).toInt32());

  JSProtoKey protoKey =
      JSCLASS_CACHED_PROTO_KEY(&ErrorObject::classes[exnType]);

  // ES6 19.5.1.1 mandates the .prototype lookup happens before the toString
  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, protoKey, &proto)) {
    return false;
  }

  // Compute the error message, if any.
  RootedString message(cx, nullptr);
  if (args.hasDefined(0)) {
    message = ToString<CanGC>(cx, args[0]);
    if (!message) {
      return false;
    }
  }

  // Find the scripted caller, but only ones we're allowed to know about.
  NonBuiltinFrameIter iter(cx, cx->realm()->principals());

  RootedString fileName(cx);
  uint32_t sourceId = 0;
  if (args.length() > 1) {
    fileName = ToString<CanGC>(cx, args[1]);
  } else {
    fileName = cx->runtime()->emptyString;
    if (!iter.done()) {
      if (const char* cfilename = iter.filename()) {
        fileName = JS_NewStringCopyZ(cx, cfilename);
      }
      if (iter.hasScript()) {
        sourceId = iter.script()->scriptSource()->id();
      }
    }
  }
  if (!fileName) {
    return false;
  }

  uint32_t lineNumber, columnNumber = 0;
  if (args.length() > 2) {
    if (!ToUint32(cx, args[2], &lineNumber)) {
      return false;
    }
  } else {
    lineNumber = iter.done() ? 0 : iter.computeLine(&columnNumber);
    columnNumber = FixupColumnForDisplay(columnNumber);
  }

  RootedObject stack(cx);
  if (!CaptureStack(cx, &stack)) {
    return false;
  }

  RootedObject obj(cx,
                   ErrorObject::create(cx, exnType, stack, fileName, sourceId,
                                       lineNumber, columnNumber, nullptr,
                                       message, proto));
  if (!obj) {
    return false;
  }

  args.rval().setObject(*obj);
  return true;
}

/*
 * Return a string that may eval to something similar to the original object.
 */
static bool exn_toSource(JSContext* cx, unsigned argc, Value* vp) {
  if (!CheckRecursionLimit(cx)) {
    return false;
  }
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedObject obj(cx, ToObject(cx, args.thisv()));
  if (!obj) {
    return false;
  }

  RootedValue nameVal(cx);
  RootedString name(cx);
  if (!GetProperty(cx, obj, obj, cx->names().name, &nameVal) ||
      !(name = ToString<CanGC>(cx, nameVal))) {
    return false;
  }

  RootedValue messageVal(cx);
  RootedString message(cx);
  if (!GetProperty(cx, obj, obj, cx->names().message, &messageVal) ||
      !(message = ValueToSource(cx, messageVal))) {
    return false;
  }

  RootedValue filenameVal(cx);
  RootedString filename(cx);
  if (!GetProperty(cx, obj, obj, cx->names().fileName, &filenameVal) ||
      !(filename = ValueToSource(cx, filenameVal))) {
    return false;
  }

  RootedValue linenoVal(cx);
  uint32_t lineno;
  if (!GetProperty(cx, obj, obj, cx->names().lineNumber, &linenoVal) ||
      !ToUint32(cx, linenoVal, &lineno)) {
    return false;
  }

  StringBuffer sb(cx);
  if (!sb.append("(new ") || !sb.append(name) || !sb.append("(")) {
    return false;
  }

  if (!sb.append(message)) {
    return false;
  }

  if (!filename->empty()) {
    if (!sb.append(", ") || !sb.append(filename)) {
      return false;
    }
  }
  if (lineno != 0) {
    /* We have a line, but no filename, add empty string */
    if (filename->empty() && !sb.append(", \"\"")) {
      return false;
    }

    JSString* linenumber = ToString<CanGC>(cx, linenoVal);
    if (!linenumber) {
      return false;
    }
    if (!sb.append(", ") || !sb.append(linenumber)) {
      return false;
    }
  }

  if (!sb.append("))")) {
    return false;
  }

  JSString* str = sb.finishString();
  if (!str) {
    return false;
  }
  args.rval().setString(str);
  return true;
}

/* static */
JSObject* ErrorObject::createProto(JSContext* cx, JSProtoKey key) {
  JSExnType type = ExnTypeFromProtoKey(key);

  if (type == JSEXN_ERR) {
    return GlobalObject::createBlankPrototype(
        cx, cx->global(), &ErrorObject::protoClasses[JSEXN_ERR]);
  }

  RootedObject protoProto(
      cx, GlobalObject::getOrCreateErrorPrototype(cx, cx->global()));
  if (!protoProto) {
    return nullptr;
  }

  return GlobalObject::createBlankPrototypeInheriting(
      cx, &ErrorObject::protoClasses[type], protoProto);
}

/* static */
JSObject* ErrorObject::createConstructor(JSContext* cx, JSProtoKey key) {
  JSExnType type = ExnTypeFromProtoKey(key);
  RootedObject ctor(cx);

  if (type == JSEXN_ERR) {
    ctor = GenericCreateConstructor<Error, 1, gc::AllocKind::FUNCTION_EXTENDED>(
        cx, key);
  } else {
    RootedFunction proto(
        cx, GlobalObject::getOrCreateErrorConstructor(cx, cx->global()));
    if (!proto) {
      return nullptr;
    }

    ctor = NewFunctionWithProto(
        cx, Error, 1, JSFunction::NATIVE_CTOR, nullptr, ClassName(key, cx),
        proto, gc::AllocKind::FUNCTION_EXTENDED, SingletonObject);
  }

  if (!ctor) {
    return nullptr;
  }

  ctor->as<JSFunction>().setExtendedSlot(0, Int32Value(type));
  return ctor;
}

JS_FRIEND_API JSFlatString* js::GetErrorTypeName(JSContext* cx,
                                                 int16_t exnType) {
  /*
   * JSEXN_INTERNALERR returns null to prevent that "InternalError: "
   * is prepended before "uncaught exception: "
   */
  if (exnType < 0 || exnType >= JSEXN_LIMIT || exnType == JSEXN_INTERNALERR ||
      exnType == JSEXN_WARN || exnType == JSEXN_NOTE) {
    return nullptr;
  }
  JSProtoKey key = GetExceptionProtoKey(JSExnType(exnType));
  return ClassName(key, cx);
}

void js::ErrorToException(JSContext* cx, JSErrorReport* reportp,
                          JSErrorCallback callback, void* userRef) {
  MOZ_ASSERT(reportp);
  MOZ_ASSERT(!JSREPORT_IS_WARNING(reportp->flags));

  // We cannot throw a proper object inside the self-hosting realm, as we
  // cannot construct the Error constructor without self-hosted code. Just
  // print the error to stderr to help debugging.
  if (cx->realm()->isSelfHostingRealm()) {
    PrintError(cx, stderr, JS::ConstUTF8CharsZ(), reportp, true);
    return;
  }

  // Find the exception index associated with this error.
  JSErrNum errorNumber = static_cast<JSErrNum>(reportp->errorNumber);
  if (!callback) {
    callback = GetErrorMessage;
  }
  const JSErrorFormatString* errorString = callback(userRef, errorNumber);
  JSExnType exnType =
      errorString ? static_cast<JSExnType>(errorString->exnType) : JSEXN_ERR;
  MOZ_ASSERT(exnType < JSEXN_LIMIT);
  MOZ_ASSERT(exnType != JSEXN_NOTE);

  if (exnType == JSEXN_WARN) {
    // werror must be enabled, so we use JSEXN_ERR.
    MOZ_ASSERT(cx->options().werror());
    exnType = JSEXN_ERR;
  }

  // Prevent infinite recursion.
  if (cx->generatingError) {
    return;
  }

  cx->generatingError = true;
  auto restore = mozilla::MakeScopeExit([cx] { cx->generatingError = false; });

  // Create an exception object.
  RootedString messageStr(cx, reportp->newMessageString(cx));
  if (!messageStr) {
    return;
  }

  RootedString fileName(cx, JS_NewStringCopyZ(cx, reportp->filename));
  if (!fileName) {
    return;
  }

  uint32_t sourceId = reportp->sourceId;
  uint32_t lineNumber = reportp->lineno;
  uint32_t columnNumber = reportp->column;

  RootedObject stack(cx);
  if (!CaptureStack(cx, &stack)) {
    return;
  }

  UniquePtr<JSErrorReport> report = CopyErrorReport(cx, reportp);
  if (!report) {
    return;
  }

  ErrorObject* errObject =
    ErrorObject::create(cx, exnType, stack, fileName, sourceId, lineNumber,
                          columnNumber, std::move(report), messageStr);
  if (!errObject) {
    return;
  }

  // Throw it.
  RootedValue errValue(cx, ObjectValue(*errObject));
  cx->setPendingException(errValue);

  // Flag the error report passed in to indicate an exception was raised.
  reportp->flags |= JSREPORT_EXCEPTION;
}

static bool IsDuckTypedErrorObject(JSContext* cx, HandleObject exnObject,
                                   const char** filename_strp) {
  /*
   * This function is called from ErrorReport::init and so should not generate
   * any new exceptions.
   */
  AutoClearPendingException acpe(cx);

  bool found;
  if (!JS_HasProperty(cx, exnObject, js_message_str, &found) || !found) {
    return false;
  }

  // First try "filename".
  const char* filename_str = *filename_strp;
  if (!JS_HasProperty(cx, exnObject, filename_str, &found)) {
    return false;
  }
  if (!found) {
    // If that doesn't work, try "fileName".
    filename_str = js_fileName_str;
    if (!JS_HasProperty(cx, exnObject, filename_str, &found) || !found) {
      return false;
    }
  }

  if (!JS_HasProperty(cx, exnObject, js_lineNumber_str, &found) || !found) {
    return false;
  }

  *filename_strp = filename_str;
  return true;
}

static JSString* ErrorReportToString(JSContext* cx, JSErrorReport* reportp) {
  /*
   * We do NOT want to use GetErrorTypeName() here because it will not do the
   * "right thing" for JSEXN_INTERNALERR.  That is, the caller of this API
   * expects that "InternalError: " will be prepended but GetErrorTypeName
   * goes out of its way to avoid this.
   */
  JSExnType type = static_cast<JSExnType>(reportp->exnType);
  RootedString str(cx);
  if (type != JSEXN_WARN && type != JSEXN_NOTE) {
    str = ClassName(GetExceptionProtoKey(type), cx);
  }

  /*
   * If "str" is null at this point, that means we just want to use
   * message without prefixing it with anything.
   */
  if (str) {
    RootedString separator(cx, JS_NewUCStringCopyN(cx, u": ", 2));
    if (!separator) {
      return nullptr;
    }
    str = ConcatStrings<CanGC>(cx, str, separator);
    if (!str) {
      return nullptr;
    }
  }

  RootedString message(cx, reportp->newMessageString(cx));
  if (!message) {
    return nullptr;
  }

  if (!str) {
    return message;
  }

  return ConcatStrings<CanGC>(cx, str, message);
}

ErrorReport::ErrorReport(JSContext* cx)
    : reportp(nullptr), str(cx), strChars(cx), exnObject(cx) {}

ErrorReport::~ErrorReport() {}

bool ErrorReport::init(JSContext* cx, HandleValue exn,
                       SniffingBehavior sniffingBehavior) {
  MOZ_ASSERT(!cx->isExceptionPending());
  MOZ_ASSERT(!reportp);

  if (exn.isObject()) {
    // Because ToString below could error and an exception object could become
    // unrooted, we must root our exception object, if any.
    exnObject = &exn.toObject();
    reportp = ErrorFromException(cx, exnObject);

    if (!reportp && sniffingBehavior == NoSideEffects) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_ERR_DURING_THROW);
      return false;
    }
  }

  // Be careful not to invoke ToString if we've already successfully extracted
  // an error report, since the exception might be wrapped in a security
  // wrapper, and ToString-ing it might throw.
  if (reportp) {
    str = ErrorReportToString(cx, reportp);
  } else if (exn.isSymbol()) {
    RootedValue strVal(cx);
    if (js::SymbolDescriptiveString(cx, exn.toSymbol(), &strVal)) {
      str = strVal.toString();
    } else {
      str = nullptr;
    }
  } else {
    str = ToString<CanGC>(cx, exn);
  }

  if (!str) {
    cx->clearPendingException();
  }

  // If ErrorFromException didn't get us a JSErrorReport, then the object
  // was not an ErrorObject, security-wrapped or otherwise. However, it might
  // still quack like one. Give duck-typing a chance.  We start by looking for
  // "filename" (all lowercase), since that's where DOMExceptions store their
  // filename.  Then we check "fileName", which is where Errors store it.  We
  // have to do it in that order, because DOMExceptions have Error.prototype
  // on their proto chain, and hence also have a "fileName" property, but its
  // value is "".
  const char* filename_str = "filename";
  if (!reportp && exnObject &&
      IsDuckTypedErrorObject(cx, exnObject, &filename_str)) {
    // Temporary value for pulling properties off of duck-typed objects.
    RootedValue val(cx);

    RootedString name(cx);
    if (JS_GetProperty(cx, exnObject, js_name_str, &val) && val.isString()) {
      name = val.toString();
    } else {
      cx->clearPendingException();
    }

    RootedString msg(cx);
    if (JS_GetProperty(cx, exnObject, js_message_str, &val) && val.isString()) {
      msg = val.toString();
    } else {
      cx->clearPendingException();
    }

    // If we have the right fields, override the ToString we performed on
    // the exception object above with something built out of its quacks
    // (i.e. as much of |NameQuack: MessageQuack| as we can make).
    //
    // It would be nice to use ErrorReportToString here, but we can't quite
    // do it - mostly because we'd need to figure out what JSExnType |name|
    // corresponds to, which may not be any JSExnType at all.
    if (name && msg) {
      RootedString colon(cx, JS_NewStringCopyZ(cx, ": "));
      if (!colon) {
        return false;
      }
      RootedString nameColon(cx, ConcatStrings<CanGC>(cx, name, colon));
      if (!nameColon) {
        return false;
      }
      str = ConcatStrings<CanGC>(cx, nameColon, msg);
      if (!str) {
        return false;
      }
    } else if (name) {
      str = name;
    } else if (msg) {
      str = msg;
    }

    if (JS_GetProperty(cx, exnObject, filename_str, &val)) {
      RootedString tmp(cx, ToString<CanGC>(cx, val));
      if (tmp) {
        filename = JS_EncodeStringToUTF8(cx, tmp);
      } else {
        cx->clearPendingException();
      }
    } else {
      cx->clearPendingException();
    }

    uint32_t lineno;
    if (!JS_GetProperty(cx, exnObject, js_lineNumber_str, &val) ||
        !ToUint32(cx, val, &lineno)) {
      cx->clearPendingException();
      lineno = 0;
    }

    uint32_t column;
    if (!JS_GetProperty(cx, exnObject, js_columnNumber_str, &val) ||
        !ToUint32(cx, val, &column)) {
      cx->clearPendingException();
      column = 0;
    }

    reportp = &ownedReport;
    new (reportp) JSErrorReport();
    ownedReport.filename = filename.get();
    ownedReport.lineno = lineno;
    ownedReport.exnType = JSEXN_INTERNALERR;
    ownedReport.column = column;

    if (str) {
      // Note that using |str| for |message_| here is kind of wrong,
      // because |str| is supposed to be of the format
      // |ErrorName: ErrorMessage|, and |message_| is supposed to
      // correspond to |ErrorMessage|. But this is what we've
      // historically done for duck-typed error objects.
      //
      // If only this stuff could get specced one day...
      char* utf8;
      if (str->ensureFlat(cx) && strChars.initTwoByte(cx, str) &&
          (utf8 =
               JS::CharsToNewUTF8CharsZ(cx, strChars.twoByteRange()).c_str())) {
        ownedReport.initOwnedMessage(utf8);
      } else {
        cx->clearPendingException();
        str = nullptr;
      }
    }
  }

  const char* utf8Message = nullptr;
  if (str) {
    toStringResultBytesStorage = JS_EncodeStringToUTF8(cx, str);
    utf8Message = toStringResultBytesStorage.get();
  }
  if (!utf8Message) {
    utf8Message = "unknown (can't convert to string)";
  }

  if (!reportp) {
    // This is basically an inlined version of
    //
    //   JS_ReportErrorNumberUTF8(cx, GetErrorMessage, nullptr,
    //                            JSMSG_UNCAUGHT_EXCEPTION, utf8Message);
    //
    // but without the reporting bits.  Instead it just puts all
    // the stuff we care about in our ownedReport and message_.
    if (!populateUncaughtExceptionReportUTF8(cx, utf8Message)) {
      // Just give up.  We're out of memory or something; not much we can
      // do here.
      return false;
    }
  } else {
    toStringResult_ = JS::ConstUTF8CharsZ(utf8Message, strlen(utf8Message));
    /* Flag the error as an exception. */
    reportp->flags |= JSREPORT_EXCEPTION;
  }

  return true;
}

bool ErrorReport::populateUncaughtExceptionReportUTF8(JSContext* cx, ...) {
  va_list ap;
  va_start(ap, cx);
  bool ok = populateUncaughtExceptionReportUTF8VA(cx, ap);
  va_end(ap);
  return ok;
}

bool ErrorReport::populateUncaughtExceptionReportUTF8VA(JSContext* cx,
                                                        va_list ap) {
  new (&ownedReport) JSErrorReport();
  ownedReport.flags = JSREPORT_ERROR;
  ownedReport.errorNumber = JSMSG_UNCAUGHT_EXCEPTION;
  // XXXbz this assumes the stack we have right now is still
  // related to our exception object.  It would be better if we
  // could accept a passed-in stack of some sort instead.
  NonBuiltinFrameIter iter(cx, cx->realm()->principals());
  if (!iter.done()) {
    ownedReport.filename = iter.filename();
    uint32_t column;
    ownedReport.sourceId =
      iter.hasScript() ? iter.script()->scriptSource()->id() : 0;
    ownedReport.lineno = iter.computeLine(&column);
    ownedReport.column = FixupColumnForDisplay(column);
    ownedReport.isMuted = iter.mutedErrors();
  }

  if (!ExpandErrorArgumentsVA(cx, GetErrorMessage, nullptr,
                              JSMSG_UNCAUGHT_EXCEPTION, nullptr,
                              ArgumentsAreUTF8, &ownedReport, ap)) {
    return false;
  }

  toStringResult_ = ownedReport.message();
  reportp = &ownedReport;
  return true;
}

JSObject* js::CopyErrorObject(JSContext* cx, Handle<ErrorObject*> err) {
  UniquePtr<JSErrorReport> copyReport;
  if (JSErrorReport* errorReport = err->getErrorReport()) {
    copyReport = CopyErrorReport(cx, errorReport);
    if (!copyReport) {
      return nullptr;
    }
  }

  RootedString message(cx, err->getMessage());
  if (message && !cx->compartment()->wrap(cx, &message)) {
    return nullptr;
  }
  RootedString fileName(cx, err->fileName(cx));
  if (!cx->compartment()->wrap(cx, &fileName)) {
    return nullptr;
  }
  RootedObject stack(cx, err->stack());
  if (!cx->compartment()->wrap(cx, &stack)) {
    return nullptr;
  }
  uint32_t sourceId = err->sourceId();
  uint32_t lineNumber = err->lineNumber();
  uint32_t columnNumber = err->columnNumber();
  JSExnType errorType = err->type();

  // Create the Error object.
  return ErrorObject::create(cx, errorType, stack, fileName, sourceId,
                             lineNumber, columnNumber,
                             std::move(copyReport), message);
}

JS_PUBLIC_API bool JS::CreateError(JSContext* cx, JSExnType type,
                                   HandleObject stack, HandleString fileName,
                                   uint32_t lineNumber, uint32_t columnNumber,
                                   JSErrorReport* report, HandleString message,
                                   MutableHandleValue rval) {
  cx->check(stack, fileName, message);
  AssertObjectIsSavedFrameOrWrapper(cx, stack);

  js::UniquePtr<JSErrorReport> rep;
  if (report) {
    rep = CopyErrorReport(cx, report);
    if (!rep) {
      return false;
    }
  }

  JSObject* obj =
      js::ErrorObject::create(cx, type, stack, fileName, 0, lineNumber,
                              columnNumber, std::move(rep), message);
  if (!obj) {
    return false;
  }

  rval.setObject(*obj);
  return true;
}

const char* js::ValueToSourceForError(JSContext* cx, HandleValue val,
                                      UniqueChars& bytes) {
  if (val.isUndefined()) {
    return "undefined";
  }

  if (val.isNull()) {
    return "null";
  }

  AutoClearPendingException acpe(cx);

  RootedString str(cx, JS_ValueToSource(cx, val));
  if (!str) {
    return "<<error converting value to string>>";
  }

  StringBuffer sb(cx);
  if (val.isObject()) {
    RootedObject valObj(cx, val.toObjectOrNull());
    ESClass cls;
    if (!GetBuiltinClass(cx, valObj, &cls)) {
      return "<<error determining class of value>>";
    }
    const char* s;
    if (cls == ESClass::Array) {
      s = "the array ";
    } else if (cls == ESClass::ArrayBuffer) {
      s = "the array buffer ";
    } else if (JS_IsArrayBufferViewObject(valObj)) {
      s = "the typed array ";
    } else {
      s = "the object ";
    }
    if (!sb.append(s, strlen(s))) {
      return "<<error converting value to string>>";
    }
  } else if (val.isNumber()) {
    if (!sb.append("the number ")) {
      return "<<error converting value to string>>";
    }
  } else if (val.isString()) {
    if (!sb.append("the string ")) {
      return "<<error converting value to string>>";
    }
  } else if (val.isBigInt()) {
    if (!sb.append("the BigInt ")) {
      return "<<error converting value to string>>";
    }
  } else {
    MOZ_ASSERT(val.isBoolean() || val.isSymbol());
    bytes = StringToNewUTF8CharsZ(cx, *str);
    return bytes.get();
  }
  if (!sb.append(str)) {
    return "<<error converting value to string>>";
  }
  str = sb.finishString();
  if (!str) {
    return "<<error converting value to string>>";
  }
  bytes = StringToNewUTF8CharsZ(cx, *str);
  return bytes.get();
}

bool js::GetInternalError(JSContext* cx, unsigned errorNumber,
                          MutableHandleValue error) {
  FixedInvokeArgs<1> args(cx);
  args[0].set(Int32Value(errorNumber));
  return CallSelfHostedFunction(cx, cx->names().GetInternalError,
                                NullHandleValue, args, error);
}

bool js::GetTypeError(JSContext* cx, unsigned errorNumber,
                      MutableHandleValue error) {
  FixedInvokeArgs<1> args(cx);
  args[0].set(Int32Value(errorNumber));
  return CallSelfHostedFunction(cx, cx->names().GetTypeError, NullHandleValue,
                                args, error);
}
