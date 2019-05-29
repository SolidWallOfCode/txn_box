/** @file Utility helpers for TS C API.

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

#include <array>

#include "txn_box/common.h"

#include <ts/ts.h>

/** Convert a TS hook ID to the local TxB enum.
 *
 * @param ts_id TS C API hook ID.
 * @return The corresponding TxB hook enum value, or @c Hook::INVALID if not a supported hook.
 */
Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev);

/// Convert TxB hook value to TS hook value.
/// TxB values are compact so an array works fine.
extern std::array<TSHttpHookID, std::tuple_size<Hook>::value> TS_Hook;
