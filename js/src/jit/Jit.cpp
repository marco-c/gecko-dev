/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/Jit.h"

#include "jit/BaselineJIT.h"
#include "jit/Ion.h"
#include "jit/JitCommon.h"
#include "jit/JitRealm.h"
#include "vm/Interpreter.h"

#include "vm/Stack-inl.h"

using namespace js;
using namespace js::jit;

static EnterJitStatus JS_HAZ_JSNATIVE_CALLER EnterJit(JSContext* cx,
                                                      RunState& state,
                                                      uint8_t* code) {
  // We don't want to call the interpreter stub here (because
  // C++ -> interpreterStub -> C++ is slower than staying in C++).
  MOZ_ASSERT(code);
  MOZ_ASSERT(code != cx->runtime()->jitRuntime()->interpreterStub().value);

  MOZ_ASSERT(IsBaselineInterpreterOrJitEnabled());

  if (!CheckRecursionLimit(cx)) {
    return EnterJitStatus::Error;
  }

#ifdef DEBUG
  // Assert we don't GC before entering JIT code. A GC could discard JIT code
  // or move the function stored in the CalleeToken (it won't be traced at
  // this point). We use Maybe<> here so we can call reset() to call the
  // AutoAssertNoGC destructor before we enter JIT code.
  mozilla::Maybe<JS::AutoAssertNoGC> nogc;
  nogc.emplace(cx);
#endif

  JSScript* script = state.script();
  size_t numActualArgs;
  bool constructing;
  size_t maxArgc;
  Value* maxArgv;
  JSObject* envChain;
  CalleeToken calleeToken;

  if (state.isInvoke()) {
    const CallArgs& args = state.asInvoke()->args();
    numActualArgs = args.length();

    if (TooManyActualArguments(numActualArgs)) {
      // Too many arguments for Ion. Baseline supports more actual
      // arguments, so in that case force Baseline code.
      if (numActualArgs > BASELINE_MAX_ARGS_LENGTH) {
        return EnterJitStatus::NotEntered;
      }
      if (script->hasBaselineScript()) {
        code = script->baselineScript()->method()->raw();
      } else {
        code = cx->runtime()->jitRuntime()->baselineInterpreter().codeRaw();
      }
    }

    constructing = state.asInvoke()->constructing();
    maxArgc = args.length() + 1;
    maxArgv = args.array() - 1;  // -1 to include |this|
    envChain = nullptr;
    calleeToken = CalleeToToken(&args.callee().as<JSFunction>(), constructing);

    unsigned numFormals = script->functionNonDelazifying()->nargs();
    if (numFormals > numActualArgs) {
      code = cx->runtime()->jitRuntime()->getArgumentsRectifier().value;
    }
  } else {
    numActualArgs = 0;
    constructing = false;
    if (script->isDirectEvalInFunction()) {
      if (state.asExecute()->newTarget().isNull()) {
        ScriptFrameIter iter(cx);
        state.asExecute()->setNewTarget(iter.newTarget());
      }
      maxArgc = 1;
      maxArgv = state.asExecute()->addressOfNewTarget();
    } else {
      maxArgc = 0;
      maxArgv = nullptr;
    }
    envChain = state.asExecute()->environmentChain();
    calleeToken = CalleeToToken(state.script());
  }

  // Caller must construct |this| before invoking the function.
  MOZ_ASSERT_IF(constructing, maxArgv[0].isObject() ||
                                  maxArgv[0].isMagic(JS_UNINITIALIZED_LEXICAL));

  RootedValue result(cx, Int32Value(numActualArgs));
  {
    AssertRealmUnchanged aru(cx);
    ActivationEntryMonitor entryMonitor(cx, calleeToken);
    JitActivation activation(cx);
    EnterJitCode enter = cx->runtime()->jitRuntime()->enterJit();

#ifdef DEBUG
    nogc.reset();
#endif
    CALL_GENERATED_CODE(enter, code, maxArgc, maxArgv, /* osrFrame = */ nullptr,
                        calleeToken, envChain, /* osrNumStackValues = */ 0,
                        result.address());
  }

  MOZ_ASSERT(!cx->hasIonReturnOverride());

  // Release temporary buffer used for OSR into Ion.
  cx->freeOsrTempData();

  if (result.isMagic()) {
    MOZ_ASSERT(result.isMagic(JS_ION_ERROR));
    return EnterJitStatus::Error;
  }

  // Jit callers wrap primitive constructor return, except for derived
  // class constructors, which are forced to do it themselves.
  if (constructing && result.isPrimitive()) {
    MOZ_ASSERT(maxArgv[0].isObject());
    result = maxArgv[0];
  }

  state.setReturnValue(result);
  return EnterJitStatus::Ok;
}

EnterJitStatus js::jit::MaybeEnterJit(JSContext* cx, RunState& state) {
  JSScript* script = state.script();

  uint8_t* code = script->jitCodeRaw();
  do {
    // Make sure we can enter Baseline Interpreter or JIT code. Note that
    // the prologue has warm-up checks to tier up if needed.
    if (IsBaselineInterpreterEnabled()) {
      if (script->jitScript()) {
        break;
      }
    } else {
      if (script->hasBaselineScript()) {
        break;
      }
    }

    script->incWarmUpCounter();

    // Try to Ion-compile.
    if (jit::IsIonEnabled()) {
      jit::MethodStatus status = jit::CanEnterIon(cx, state);
      if (status == jit::Method_Error) {
        return EnterJitStatus::Error;
      }
      if (status == jit::Method_Compiled) {
        code = script->jitCodeRaw();
        break;
      }
    }

    // Try to Baseline-compile.
    if (jit::IsBaselineJitEnabled()) {
      jit::MethodStatus status =
          jit::CanEnterBaselineMethod<BaselineTier::Compiler>(cx, state);
      if (status == jit::Method_Error) {
        return EnterJitStatus::Error;
      }
      if (status == jit::Method_Compiled) {
        code = script->jitCodeRaw();
        break;
      }
    }

    // Try to enter the Baseline Interpreter.
    if (IsBaselineInterpreterEnabled()) {
      jit::MethodStatus status =
          jit::CanEnterBaselineMethod<BaselineTier::Interpreter>(cx, state);
      if (status == jit::Method_Error) {
        return EnterJitStatus::Error;
      }
      if (status == jit::Method_Compiled) {
        code = script->jitCodeRaw();
        break;
      }
    }

    return EnterJitStatus::NotEntered;
  } while (false);

  return EnterJit(cx, state, code);
}
