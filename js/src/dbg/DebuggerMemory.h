/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef dbg_DebuggerMemory_h
#define dbg_DebuggerMemory_h

#include "jsapi.h"

#include "js/Class.h"
#include "js/Value.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"

namespace js {

class DebuggerMemory : public NativeObject {
  friend class Debugger;

  static DebuggerMemory* checkThis(JSContext* cx, CallArgs& args,
                                   const char* fnName);

  Debugger* getDebugger();

 public:
  static DebuggerMemory* create(JSContext* cx, Debugger* dbg);

  enum { JSSLOT_DEBUGGER, JSSLOT_COUNT };

  static bool construct(JSContext* cx, unsigned argc, Value* vp);
  static const Class class_;
  static const JSPropertySpec properties[];
  static const JSFunctionSpec methods[];

  // Accessor properties of Debugger.Memory.prototype.

  static bool setTrackingAllocationSites(JSContext* cx, unsigned argc,
                                         Value* vp);
  static bool getTrackingAllocationSites(JSContext* cx, unsigned argc,
                                         Value* vp);
  static bool setMaxAllocationsLogLength(JSContext* cx, unsigned argc,
                                         Value* vp);
  static bool getMaxAllocationsLogLength(JSContext* cx, unsigned argc,
                                         Value* vp);
  static bool setAllocationSamplingProbability(JSContext* cx, unsigned argc,
                                               Value* vp);
  static bool getAllocationSamplingProbability(JSContext* cx, unsigned argc,
                                               Value* vp);
  static bool getAllocationsLogOverflowed(JSContext* cx, unsigned argc,
                                          Value* vp);

  static bool getOnGarbageCollection(JSContext* cx, unsigned argc, Value* vp);
  static bool setOnGarbageCollection(JSContext* cx, unsigned argc, Value* vp);

  // Function properties of Debugger.Memory.prototype.

  static bool takeCensus(JSContext* cx, unsigned argc, Value* vp);
  static bool drainAllocationsLog(JSContext* cx, unsigned argc, Value* vp);
};

} /* namespace js */

#endif /* dbg_DebuggerMemory_h */
