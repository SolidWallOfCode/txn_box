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

#include <tuple>
#include <swoc/Lexicon.h>

/// Supported hooks.
enum class Hook {
  READ_REQ, ///< Read Request from user agent.
  SEND_RSP, ///< Send Response to user agent.
  BEGIN = READ_REQ, ///< Iteration support.
  END = SEND_RSP + 1 ///< Iteration support.
};

/// Make @c tuple_size work for the @c Hook enum.
namespace std {
template<> struct tuple_size<Hook> : public std::integral_constant<size_t,
    static_cast<size_t>(Hook::END) - 1> {
};
} // namespace std

/// Name lookup for hook values.
extern swoc::Lexicon<Hook> HookName;
