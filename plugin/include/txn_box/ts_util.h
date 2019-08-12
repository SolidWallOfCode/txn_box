/** @file
 *  Utility helpers for TS C API.
 *
 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <array>
#include <type_traits>

#include "txn_box/common.h"

#include <ts/ts.h>

/** Convert a TS hook ID to the local TxB enum.
 *
 * @param ev TS C API event value.
 * @return The corresponding TxB hook enum value, or @c Hook::INVALID if not a supported hook.
 */
Hook Convert_TS_Event_To_TxB_Hook(TSEvent ev);

/// Convert TxB hook value to TS hook value.
/// TxB values are compact so an array works fine.
extern std::array<TSHttpHookID, std::tuple_size<Hook>::value> TS_Hook;

namespace ts {

/** Hold a string allocated from TS core.
 * This provides both a view of the string and clean up when destructed.
 */
class String {
public:
  String() = default; ///< Construct an empty instance.
  ~String(); ///< Clean up string.

  /** Construct from a TS allocated string.
   *
   * @param s Pointer returned from C API call.
   * @param size Size of the string.
   */
  String(char *s, int64_t size);

  operator swoc::TextView () const;

protected:
  swoc::TextView _view;
};

inline ts::String::String(char *s, int64_t size) : _view{ s, static_cast<size_t>(size) } {}

inline String::~String() { if (_view.data()) { TSfree(const_cast<char*>(_view.data())); } }

inline String::operator swoc::TextView() const { return _view; }

/// Clean up an TS @c TSIOBuffer
struct IOBufferDeleter {
  void operator()(TSIOBuffer buff) { if (buff) { TSIOBufferDestroy(buff); } }
};

using IOBuffer = std::unique_ptr<std::remove_pointer<TSIOBuffer>::type, IOBufferDeleter>;

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

/// A URL object.
class URL : public HeapObject {
  friend class HttpHeader;
  using self_type = URL; ///< Self reference type.
  using super_type = HeapObject; ///< Parent type.
public:
  URL() = default;
  /// Construct from TS data.
  URL(TSMBuffer buff, TSMLoc loc);

  swoc::TextView view() const; ///< View of entire URL.
  swoc::TextView host() const; ///< View of the URL host.
  swoc::TextView scheme() const; ///< View of the URL scheme.
  swoc::TextView path() const; ///< View of the URL path.

  /** Set the host in the URL.
   *
   * @param host Host.
   * @return @a this.
   */
  self_type & set_host(swoc::TextView host);
protected:
  mutable IOBuffer _iobuff; ///< IO buffer with the URL text.
  mutable swoc::TextView _view; ///< View of the URL in @a _iobuff.
};

class HttpField : public HeapObject {
  friend class HttpHeader;
  using self_type = HttpField; ///< Self reference type.
  using super_type = HeapObject; ///< Parent type.
public:
  HttpField() = default;
  HttpField(self_type const&) = delete;
  HttpField(self_type && that) : super_type(that), _hdr(that._hdr) {
    that._buff = nullptr;
    that._hdr = nullptr;
    that._loc = nullptr;
  }

  self_type & operator = (self_type const&) = delete;
  self_type &operator = (self_type && that) {
    this->~self_type();
    new (this) self_type(std::move(that));
    return *this;
  }

  ~HttpField();

  /// Return the current value for the field.
  swoc::TextView value();

  /** Set the @a value for @a this field.
   *
   * @param value Value to set.
   * @return @c true if the value was successful updated, @c false if not.
   */
  bool assign(swoc::TextView value);

  /** Guarantee a @a value for @a this field.
   *
   * @param value Field value.
   * @return @c true if the field has a value, @c false if successfully updated.
   *
   * If the field already has a value, this does nothing. Otherwise the value is set to @a value.
   */
  bool assign_if_not_set(swoc::TextView value);

  bool destroy();

protected:
  HttpField(TSMBuffer buff, TSMLoc hdr_loc, TSMLoc field_loc);

  TSMLoc _hdr = nullptr;
};

class HttpHeader : public HeapObject {
  friend class HttpTxn;
  using self_type = HttpHeader; ///< Self reference type.
  using super_type = HeapObject; ///< Parent type.
public:
  HttpHeader() = default;
  HttpHeader(TSMBuffer buff, TSMLoc loc);

  /** Retrieve the URL object from the header.
   *
   * @return A URL object wrapper.
   */
  URL url();

  /** Find the field with @a name.
   *
   * @param name Field name.
   * @return The field if found, an invalid field if not.
   */
  HttpField field(swoc::TextView name) const;

  /** Create a field with @a name and no value.
   *
   * @param name Name of field.
   * @return A valid on success, invalid on error.
   */
  HttpField field_create(swoc::TextView name);

  /** Find or create a field with @a name.
   *
   * @param name Name of the field.
   * @return A valid field on success, invalid on error.
   *
   * This is convenient for setting a field, as it will create the field if not found. Failure
   * to get a valid field indicates a bad HTTP header object.
   */
  HttpField field_obtain(swoc::TextView name);

  /** Remove a field.
   *
   * @param name Field name.
   * @return @a this.
   */
  self_type& field_remove(swoc::TextView name);

  TSHttpStatus status() const { return TSHttpHdrStatusGet(_buff, _loc); }
  swoc::TextView method() const { int length; auto text = TSHttpHdrMethodGet(_buff, _loc, &length); return { text, static_cast<size_t>(length) }; }

  bool status_set(TSHttpStatus status) const;

  /** Set the reason field in the header.
   *
   * @param reason Reason string.
   * @return @c true if success, @c false if not.
   */
  bool reason_set(swoc::TextView reason);
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
  HttpHeader ursp_hdr();
  HttpHeader prsp_hdr();

  /** Is this an internal request?
   *
   * @return @c true if internal, @c false if not.
   */
  bool is_internal() const;

  String effective_url_get() const;

  /** Set the transaction status.
   *
   * @param status HTTP status code.
   *
   * If this is called before the @c POST_REMAP hook it will prevent an upstream request and instead
   * return a response with this status. After that, it will modify the status of the upstream
   * response.
   *
   * @see error_body_set
   * @see HttpHeader::set_reason
   */
  void status_set(int status);

  /** Set the body on an error response.
   *
   * @param body Body content.
   * @param content_type Content type.
   */
  void error_body_set(swoc::TextView body, swoc::TextView content_type);

protected:
  TSHttpTxn _txn = nullptr;

  /** Duplicate a string into TS owned memory.
   *
   * @param text String to duplicate.
   * @return The duplicated string.
   */
  swoc::MemSpan<char> ts_dup(swoc::TextView const& text);
};

inline HeapObject::HeapObject(TSMBuffer buff, TSMLoc loc) : _buff(buff), _loc(loc) {}

inline bool HeapObject::is_valid() const { return _buff != nullptr && _loc != nullptr; }

inline URL::URL(TSMBuffer buff, TSMLoc loc) : super_type(buff, loc) {}

inline swoc::TextView URL::scheme() const { int length; auto text = TSUrlSchemeGet(_buff, _loc, &length); return { text, static_cast<size_t>(length) }; }

inline swoc::TextView URL::path() const { int length; auto text = TSUrlPathGet(_buff, _loc, &length); return { text, static_cast<size_t>(length) }; }

inline URL &URL::set_host(swoc::TextView host) {
  if (this->is_valid()) {
    TSUrlHostSet(_buff, _loc, host.data(), host.size());
  }
  return *this;
}

inline HttpField::HttpField(TSMBuffer buff, TSMLoc hdr_loc, TSMLoc field_loc) : super_type(buff, field_loc), _hdr(hdr_loc) {}

inline HttpHeader::HttpHeader(TSMBuffer buff, TSMLoc loc) : super_type(buff, loc) {}

inline HttpTxn::HttpTxn(TSHttpTxn txn) : _txn(txn) {}

inline HttpTxn::operator TSHttpTxn() const { return _txn; }

const swoc::TextView HTTP_FIELD_HOST { TS_MIME_FIELD_HOST, static_cast<size_t>(TS_MIME_LEN_HOST) };
const swoc::TextView HTTP_FIELD_LOCATION { TS_MIME_FIELD_LOCATION, static_cast<size_t>(TS_MIME_LEN_LOCATION) };
const swoc::TextView HTTP_FIELD_CONTENT_LENGTH { TS_MIME_FIELD_CONTENT_LENGTH, static_cast<size_t>(TS_MIME_LEN_CONTENT_LENGTH) };
const swoc::TextView HTTP_FIELD_CONTENT_TYPE { TS_MIME_FIELD_CONTENT_TYPE, static_cast<size_t>(TS_MIME_LEN_CONTENT_TYPE) };

}; // namespace ts

namespace swoc {
  BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, TSHttpStatus status);
} // namespace swoc

namespace std {

// Structured binding support for @c HeapObject.
template<> class tuple_size<ts::HeapObject> : public std::integral_constant<size_t, 2> {};
template<> class tuple_element<0, ts::HeapObject> { public: using type = TSMBuffer; };
template<> class tuple_element<1, ts::HeapObject> { public: using type = TSMLoc; };
template < size_t IDX > typename tuple_element<IDX, ts::HeapObject>::type get(ts::HeapObject);
template <> inline TSMBuffer get<0>(ts::HeapObject obj) { return obj._buff; }
template <> inline TSMLoc get<1>(ts::HeapObject obj) { return obj._loc; }
} // namespace std
