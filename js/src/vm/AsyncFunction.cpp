/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/AsyncFunction.h"

#include "mozilla/Maybe.h"

#include "builtin/Promise.h"
#include "vm/GeneratorObject.h"
#include "vm/GlobalObject.h"
#include "vm/Interpreter.h"
#include "vm/Realm.h"
#include "vm/SelfHosting.h"

#include "vm/JSObject-inl.h"

using namespace js;

using mozilla::Maybe;

/* static */
bool GlobalObject::initAsyncFunction(JSContext* cx,
                                     Handle<GlobalObject*> global) {
  if (global->getReservedSlot(ASYNC_FUNCTION_PROTO).isObject()) {
    return true;
  }

  RootedObject asyncFunctionProto(
      cx, NewSingletonObjectWithFunctionPrototype(cx, global));
  if (!asyncFunctionProto) {
    return false;
  }

  if (!DefineToStringTag(cx, asyncFunctionProto, cx->names().AsyncFunction)) {
    return false;
  }

  RootedValue function(cx, global->getConstructor(JSProto_Function));
  if (!function.toObjectOrNull()) {
    return false;
  }
  RootedObject proto(cx, &function.toObject());
  RootedAtom name(cx, cx->names().AsyncFunction);
  RootedObject asyncFunction(
      cx, NewFunctionWithProto(cx, AsyncFunctionConstructor, 1,
                               JSFunction::NATIVE_CTOR, nullptr, name, proto));
  if (!asyncFunction) {
    return false;
  }
  if (!LinkConstructorAndPrototype(cx, asyncFunction, asyncFunctionProto,
                                   JSPROP_PERMANENT | JSPROP_READONLY,
                                   JSPROP_READONLY)) {
    return false;
  }

  global->setReservedSlot(ASYNC_FUNCTION, ObjectValue(*asyncFunction));
  global->setReservedSlot(ASYNC_FUNCTION_PROTO,
                          ObjectValue(*asyncFunctionProto));
  return true;
}

enum class ResumeKind { Normal, Throw };

// Async Functions proposal 2.2 steps 3.f, 3.g.
// Async Functions proposal 2.2 steps 3.d-e, 3.g.
// Implemented in js/src/builtin/Promise.cpp

// Async Functions proposal 2.2 steps 3-8, 2.4 steps 2-7, 2.5 steps 2-7.
static bool AsyncFunctionResume(JSContext* cx,
                                Handle<AsyncFunctionGeneratorObject*> generator,
                                ResumeKind kind, HandleValue valueOrReason) {
  Rooted<PromiseObject*> resultPromise(cx, generator->promise());

  RootedObject stack(cx);
  Maybe<JS::AutoSetAsyncStackForNewCalls> asyncStack;
  if (JSObject* allocationSite = resultPromise->allocationSite()) {
    // The promise is created within the activation of the async function, so
    // use the parent frame as the starting point for async stacks.
    stack = allocationSite->as<SavedFrame>().getParent();
    if (stack) {
      asyncStack.emplace(
          cx, stack, "async",
          JS::AutoSetAsyncStackForNewCalls::AsyncCallKind::EXPLICIT);
    }
  }

  MOZ_ASSERT(!generator->isClosed(),
             "closed generator when resuming async function");
  MOZ_ASSERT(generator->isSuspended(),
             "non-suspended generator when resuming async function");

  // Execution context switching is handled in generator.
  HandlePropertyName funName = kind == ResumeKind::Normal
                                   ? cx->names().AsyncFunctionNext
                                   : cx->names().AsyncFunctionThrow;
  FixedInvokeArgs<1> args(cx);
  args[0].set(valueOrReason);
  RootedValue generatorOrValue(cx, ObjectValue(*generator));
  if (!CallSelfHostedFunction(cx, funName, generatorOrValue, args,
                              &generatorOrValue)) {
    if (!generator->isClosed()) {
      generator->setClosed();
    }
    return false;
  }

  MOZ_ASSERT_IF(generator->isClosed(), generatorOrValue.isObject());
  MOZ_ASSERT_IF(generator->isClosed(),
                &generatorOrValue.toObject() == resultPromise);
  MOZ_ASSERT_IF(!generator->isClosed(), generator->isAfterAwait());

  return true;
}

// Async Functions proposal 2.3 steps 1-8.
// Implemented in js/src/builtin/Promise.cpp

// Async Functions proposal 2.4.
MOZ_MUST_USE bool js::AsyncFunctionAwaitedFulfilled(
    JSContext* cx, Handle<AsyncFunctionGeneratorObject*> generator,
    HandleValue value) {
  // Step 1 (implicit).

  // Steps 2-7.
  return AsyncFunctionResume(cx, generator, ResumeKind::Normal, value);
}

// Async Functions proposal 2.5.
MOZ_MUST_USE bool js::AsyncFunctionAwaitedRejected(
    JSContext* cx, Handle<AsyncFunctionGeneratorObject*> generator,
    HandleValue reason) {
  // Step 1 (implicit).

  // Step 2-7.
  return AsyncFunctionResume(cx, generator, ResumeKind::Throw, reason);
}

JSObject* js::AsyncFunctionResolve(
    JSContext* cx, Handle<AsyncFunctionGeneratorObject*> generator,
    HandleValue valueOrReason, AsyncFunctionResolveKind resolveKind) {
  Rooted<PromiseObject*> promise(cx, generator->promise());
  if (resolveKind == AsyncFunctionResolveKind::Fulfill) {
    if (!AsyncFunctionReturned(cx, promise, valueOrReason)) {
      return nullptr;
    }
  } else {
    if (!AsyncFunctionThrown(cx, promise, valueOrReason)) {
      return nullptr;
    }
  }
  return promise;
}

const Class AsyncFunctionGeneratorObject::class_ = {
    "AsyncFunctionGenerator",
    JSCLASS_HAS_RESERVED_SLOTS(AsyncFunctionGeneratorObject::RESERVED_SLOTS)};

AsyncFunctionGeneratorObject* AsyncFunctionGeneratorObject::create(
    JSContext* cx, HandleFunction fun) {
  MOZ_ASSERT(fun->isAsync() && !fun->isGenerator());

  Rooted<PromiseObject*> resultPromise(cx, CreatePromiseObjectForAsync(cx));
  if (!resultPromise) {
    return nullptr;
  }

  auto* obj = NewBuiltinClassInstance<AsyncFunctionGeneratorObject>(cx);
  if (!obj) {
    return nullptr;
  }
  obj->initFixedSlot(PROMISE_SLOT, ObjectValue(*resultPromise));

  // Starts in the running state.
  obj->setResumeIndex(AbstractGeneratorObject::RESUME_INDEX_RUNNING);

  return obj;
}
