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

namespace ts {

/** Generic base class for objects in the TS Header heaps.
 * All of these are represented by a buffer and a location.
 */
class HeapObject {
  template < size_t IDX > friend typename std::tuple_element<IDX, ts::HeapObject>::type std::get(ts::HeapObject);
public:
  HeapObject() = default;
  HeapObject(TSMBuffer buff, TSMLoc loc);

  /// Check if there is a valid object.
  bool is_valid() const;

protected:
  TSMBuffer _buff = nullptr;
  TSMLoc _loc = nullptr;
};

class HttpHeader;

class URL : public HeapObject {
  friend class HttpHeader;
  using self_type = URL; ///< Self reference type.
  using super_type = HeapObject; ///< Parent type.
public:
  URL() = default;
  URL(TSMBuffer buff, TSMLoc loc);;

  swoc::TextView host();
};

class HttpField : public HeapObject {
  friend class HttpHeader;
  using self_type = HttpField; ///< Self reference type.
  using super_type = HeapObject; ///< Parent type.
public:
  HttpField() = default;

  swoc::TextView value();
protected:
  HttpField(TSMBuffer buff, TSMLoc hdr_loc, TSMLoc field_loc);

  TSMLoc _hdr = nullptr;
};

class HttpHeader : public HeapObject {
  friend class HttpTxn;
  using self_type = HttpHeader; ///< Self reference type.
  using super_type = HeapObject; ///< Parent type.
public:
  URL url();
  HttpField field(swoc::TextView name);

public:
  HttpHeader() = default;
  HttpHeader(TSMBuffer buff, TSMLoc loc);
};

/** Wrapper for a TS C API transaction.
 * This provides various utility methods, rather than having free functions that all take a
 * transaction instance.
 */
class HttpTxn {
  using self_type = HttpTxn; ///< Self reference type.
public:
  HttpTxn() = default;
  HttpTxn(TSHttpTxn txn);
  operator TSHttpTxn() const;

  HttpHeader creq_hdr();
  HttpHeader preq_hdr();
protected:
  TSHttpTxn _txn = nullptr;
};

inline HeapObject::HeapObject(TSMBuffer buff, TSMLoc loc) : _buff(buff), _loc(loc) {}

inline bool HeapObject::is_valid() const { return _buff != nullptr && _loc != nullptr; }

inline URL::URL(TSMBuffer buff, TSMLoc loc) : super_type(buff, loc) {}

inline HttpField::HttpField(TSMBuffer buff, TSMLoc hdr_loc, TSMLoc field_loc) : super_type(buff, field_loc), _hdr(hdr_loc) {}

inline HttpHeader::HttpHeader(TSMBuffer buff, TSMLoc loc) : super_type(buff, loc) {}

inline HttpTxn::HttpTxn(TSHttpTxn txn) : _txn(txn) {}

inline HttpTxn::operator TSHttpTxn() const { return _txn; }

}; // namespace ts

namespace std {

// Structured binding support for @c HeapObject.
template<> class tuple_size<ts::HeapObject> : public std::integral_constant<size_t, 2> {};
template<> class tuple_element<0, ts::HeapObject> { public: using type = TSMBuffer; };
template<> class tuple_element<1, ts::HeapObject> { public: using type = TSMLoc; };
template < size_t IDX > typename tuple_element<IDX, ts::HeapObject>::type get(ts::HeapObject);
template <> inline TSMBuffer get<0>(ts::HeapObject obj) { return obj._buff; }
template <> inline TSMLoc get<1>(ts::HeapObject obj) { return obj._loc; }
} // namespace std
