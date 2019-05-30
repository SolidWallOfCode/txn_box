/*
   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.

*/

#pragma once

#include <memory>

#include <swoc/MemArena.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"
#include "txn_box/Directive.h"
#include "txn_box/ts_util.h"

class Config;

/** Per transaction context.
 *
 * This holds data associated with a specific transaction, along with pointers / references to
 * global structures, such that a transaction based hook can retrieve all necessary information
 * from a point to an instance of this class.
 */
class Context {
  using self_type = Context;
public:

  Context(Config & cfg);

  swoc::Errata when_do(Hook hook_idx, Directive * drtv);

  swoc::Errata invoke_for_hook(Hook hook);

  Hook _cur_hook = Hook::INVALID;
  TSCont _cont = nullptr;
  ts::HttpTxn _txn;

  /// Directives for a particular hook.
  struct HookDirectives {
    unsigned _count = 0; ///< Number of directives.
    unsigned _idx = 0; ///< Index of next directive to invoke.
    Directive** _drtv = nullptr; ///< Array of directive pointers.
    bool _hook_set = false; ///< If a hook has already been set.
  };

  /// State of each hook for this transaction / context.
  std::array<HookDirectives, std::tuple_size<Hook>::value> _directives;

  ts::HttpHeader creq_hdr();

protected:

  /// Cleanup functor for an inverted arena.
  /// @internal Because the arena is inverted, calling the destructor will clean up everything.
  /// For this reason @c delete must @b not be called (that will double free).
  struct ArenaDestructor {
    void operator()(swoc::MemArena* arena) { arena->swoc::MemArena::~MemArena(); }
  };

  /// Transaction local storage.
  /// This is a pointer so that the arena can be inverted to minimize allocations.
  std::unique_ptr<swoc::MemArena, ArenaDestructor> _arena;

  // HTTP header objects for the transaction.
  ts::HttpHeader _creq; ///< Client request header.
  ts::HttpHeader _preq; ///< Proxy request header.
  ts::HttpHeader _ursp; ///< Upstream response header.
  ts::HttpHeader _prsp; ///< Proxy response header.

};
