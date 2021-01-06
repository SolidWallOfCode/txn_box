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
#include <swoc/swoc_file.h>
#include <swoc/MemArena.h>

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

template < typename ... Args >
void DebugMsg(swoc::TextView fmt, Args && ... args) {
  swoc::LocalBufferWriter<1024> w;
  auto arg_pack = std::forward_as_tuple(args...);
  w.print_v(fmt, arg_pack);
  if (! w.error()) {
    TSDebug("txn_box", "%.*s", int(w.size()), w.data());
  } else {
    // Do it the hard way.
    std::vector<char> buff;
    buff.resize(w.extent());
    swoc::FixedBufferWriter fw(buff.data(), buff.size());
    fw.print_v(fmt, arg_pack);
    TSDebug("txn_box", "%.*s", int(fw.size()), fw.data());
  }
}

/** Hold a string allocated from TS core.
 * This provides both a full of the string and clean up when destructed.
 *
 * @internal - why isn't this a @c std::unique_ptr with a specialized destructor? Because we
 * need the length?
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

using ConfVarData = std::variant<std::monostate, intmax_t, double, swoc::TextView>;

inline ts::String::String(char *s, int64_t size) : _view{ s, static_cast<size_t>(size) } {}

inline String::~String() { if (_view.data()) { TSfree(const_cast<char*>(_view.data())); } }

inline String::operator swoc::TextView() const { return _view; }

inline swoc::file::path && make_absolute(swoc::file::path && path) {
  if (path.is_relative()) {
    path = swoc::file::path(TSConfigDirGet()) / path;
  }
  return std::move(path);
}

inline swoc::file::path & make_absolute(swoc::file::path & path) {
  if (path.is_relative()) {
    path = swoc::file::path(TSConfigDirGet()) / path;
  }
  return path;
}

/// Clean up an TS @c TSIOBuffer
struct IOBufferDeleter {
  void operator()(TSIOBuffer buff) { if (buff) { TSIOBufferDestroy(buff); } }
};

using IOBuffer = std::unique_ptr<std::remove_pointer<TSIOBuffer>::type, IOBufferDeleter>;

/** Generic base class for objects in the TS Header heaps.
 * All of these are represented by a buffer and a location.
 */
class HeapObject {
public:
  HeapObject() = default;
  HeapObject(TSMBuffer buff, TSMLoc loc);

  /// Check if there is a valid object.
  bool is_valid() const;

  TSMBuffer mbuff() const;
  TSMLoc mloc() const;

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

  /** Write the full URL to @a w.
   *
   * @param w Destination buffer.
   * @return @a w
   */
  swoc::BufferWriter& write_full(swoc::BufferWriter& w) const;

  /** Write the network location of the URL to @a w.
   *
   * @param w Output buffer.
   * @return @a w
   */
  swoc::BufferWriter& write_loc(swoc::BufferWriter& w) const;

  /** Get the network location
   *
   * @return A tuple of [ host, port ].
   */
  std::tuple<swoc::TextView, in_port_t> loc() const;

  swoc::TextView host() const; ///< View of the URL host.
  /** Get the port in the URL.
   *
   * @return The port.
   *
   * @note If the port is not explicitly set, it is computed based on the scheme.
   */
  in_port_t port() const; ///< Port.
  swoc::TextView scheme() const; ///< View of the URL scheme.
  swoc::TextView path() const; ///< View of the URL path.
  swoc::TextView query() const; ///< View of the query.

  /** Set the scheme for the URL.
   *
   * @param scheme Scheme as text.
   * @return @a this
   */
  self_type & scheme_set(swoc::TextView const& scheme);

  /** Set the location for the URL.
   *
   * @param loc Network location (host[:port])
   * @return @a this
   */
  self_type & loc_set(swoc::TextView const& loc);

  /** Set the host in the URL.
   *
   * @param host Host.
   * @return @a this.
   */
  self_type & host_set(swoc::TextView const& host);

  static bool is_port_canonical(swoc::TextView const& scheme, in_port_t port);
  bool is_port_canonical() const { return this->is_port_canonical(this->scheme(), this->port()); }

  /** Set the @a port in the URL.
   *
   * @param port Port value.
   * @return @a this
   *
   * @note If the port matches the computed port of the scheme, it is not explicitly set.
   */
  self_type & port_set(in_port_t port);

  self_type & path_set(swoc::TextView path) {
    TSUrlPathSet(_buff, _loc, path.data(), path.size());
    return *this;
  }

  self_type & query_set(swoc::TextView text);
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

  /// @return The name of the field.
  swoc::TextView name() const;

  /// @return  The current value for the field.
  swoc::TextView value() const;

  /** Set the @a value for @a this field.
   *
   * @param value Value to set.
   * @return @c true if the value was successful updated, @c false if not.
   */
  bool assign(swoc::TextView value);

  /// Destroy the field (remove from the HTTP header).
  bool destroy();

  /** Get the next duplicate field.
   *
   * @return The duplicate after @a this.
   *
   * Duplicates are fields with the same name.
   */
  self_type next_dup() const {
    return this->is_valid()
    ? self_type{_buff, _hdr, TSMimeHdrFieldNextDup(_buff, _hdr, _loc )}
    : self_type{};
  }

  /** Get the number of duplicates for this field.
   *
   * @return The number of instances of this field.
   */
  unsigned dup_count() const;

  bool operator == (self_type const& that) { return _loc == that._loc; }
  bool operator != (self_type const& that) { return _loc != that._loc; }

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
};

class HttpRequest : public HttpHeader {
  using self_type = HttpRequest;
  using super_type = HttpHeader;
public:
  /// Make super type constructors available.
  using super_type::super_type;

  /** Retrieve the URL object from the header.
   *
   * @return A URL object wrapper.
   */
  URL url() const;

  /** Effective URL for the request.
   *
   * @param arena Temporary memory to write the URL.
   * @return A view of the URL in @a arena.
   */
  swoc::BufferWriter& effective_url(swoc::BufferWriter&w);

  swoc::TextView method() const;

  /** Write the request network location to @a w.
   *
   * @param w Output buffer
   * @return @a w
   *
   * The network location is pulled from the URL if present, otherwise from the @c Host field.
   */
  swoc::BufferWriter& write_loc(swoc::BufferWriter& w) const;

  swoc::TextView host() const;
  in_port_t port() const;

  /** Get the network location
   *
   * @return A tuple of [ host, port ].
   */
  std::tuple<swoc::TextView, in_port_t> loc() const;

  /** Assign @a text as the URL.
   *
   * @param text Fully formed URL.
   * @return @c true if the URL was set, @c false if not.
   *
   * @a text must be a valid URL and all elements of this object are replaced by that URL.
   */
  bool url_set(swoc::TextView text);

  /** Set the @a host for the request.
   *
   * @param host Host for request.
   * @return @c true on success, @c false on failure.
   *
   * This will update the request as little as possible. If the URL does not contain a host
   * then it is unmodified and the @c Host field is set to @a host. If the URL has a host and
   * there is no @c Host field, only the URL is updated. The port is not updated.
   */
  bool host_set(swoc::TextView const& host);

  /** Set the port.
   *
   * @param port Port in host order.
   * @return If the port was updated.
   *
   * This updates the URL and @c Host field as needed, making as few changes as possible.
   */
  bool port_set(in_port_t port);
};

class HttpResponse : public HttpHeader {
  using self_type = HttpResponse;
  using super_type = HttpHeader;
public:
  /// Make super type constructors available.
  using super_type::super_type;

  TSHttpStatus status() const;
  bool status_set(TSHttpStatus status) const;

  swoc::TextView reason() const;
  /** Set the reason field in the header.
   *
   * @param reason Reason string.
   * @return @c true if success, @c false if not.
   */
  bool reason_set(swoc::TextView reason);
};

/** Wrapper for a TS C API session.
 *
 */
class HttpSsn {
  friend class HttpTxn;
public:
  /// Default constructor - null session.
  HttpSsn() = default;

  /// Transaction count.
  unsigned txn_count() const;

  /// Return the inbound SNI name, if any.
  swoc::TextView inbound_sni() const;

  swoc::TextView proto_contains(swoc::TextView const& tag) const;

  /** Retrieve the protocol stack for @a this session in to @a tags.
   *
   * @param tags [out] Protocol tags.
   * @return The actual number of protocol tags, or -1 on error.
   *
   * The number of tags retrieved will be the minimum of the actual number of tags and the
   * size of @a tags. The return value will be the number of actual tags. It is the caller's
   * responsibility to handle the case where this is larger than @a tags.
   */
  int protocol_stack(swoc::MemSpan<char const*> tags) const;

  swoc::IPEndpoint remote_addr() const;
protected:
  TSHttpSsn _ssn = nullptr; ///< Session handle.

  HttpSsn(TSHttpSsn ssn) : _ssn(ssn) {}
};

class TxnConfigVar {
  using self_type = TxnConfigVar; ///< Self reference type.
public:
  TxnConfigVar(swoc::TextView const& name, TSOverridableConfigKey key, TSRecordDataType type);
  swoc::TextView name() const { return _name; }
  TSOverridableConfigKey key() const { return _key; }
  TSRecordDataType type() const { return _ts_type; }

  bool is_valid(intmax_t) const { return _ts_type == TS_RECORDDATATYPE_INT; }
  bool is_valid(swoc::TextView const&) const { return _ts_type == TS_RECORDDATATYPE_STRING; }
  bool is_valid(double) const { return _ts_type == TS_RECORDDATATYPE_FLOAT; }
  template < typename T > bool is_valid(typename std::decay<T>::type) { return false; }

protected:
  std::string _name; ///< Name.
  TSOverridableConfigKey _key; ///< override index value.
  TSRecordDataType _ts_type { TS_RECORDDATATYPE_NULL };
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

  HttpRequest ua_req_hdr();
  HttpRequest preq_hdr();
  HttpResponse ursp_hdr();
  HttpResponse prsp_hdr();

  /** Is this an internal request?
   *
   * @return @c true if internal, @c false if not.
   */
  bool is_internal() const;

  /** The effective URL for the transaction.
   *
   * @return The effective URL of the user agent request.
   */
  String effective_url_get() const;

  /** The pristine user agent request URL.
   *
   * @return The pristine user agent request URL.
   *
   * @note Do not modify this URL. It will not end well.
   */
   URL pristine_url_get() const;

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

  /** Assign the cache @a key for the transaction.
   *
   * @param key Cache key for the retrieved object.
   * @return Errors, if any.
   */
  swoc::Errata cache_key_assign(swoc::TextView const& key);

  /// @return The session object for @a this transaction.
  HttpSsn ssn() const;

  /** Fix the upstream address.
   *
   * @param addr Address to use for the upstream.
   * @return Errors, if any.
   */
  bool set_upstream_addr(swoc::IPAddr const& addr) const;

  /** Assign @a n to the integer transaction overridable configuration @a var
   *
   * @param var Overridable variable.
   * @param n Value to assign.
   * @return Errors, if any.
   *
   * @note This does not check if @a var is in fact an integer, the caller must assure that.
   */
  swoc::Errata override_assign(TxnConfigVar const& var, intmax_t n);

  /** Assign @a n to the string transaction overridable configuration @a var
   *
   * @param var Overridable variable.
   * @param text Value to assign.
   * @return Errors, if any.
   *
   * @note This does not check if @a var is in fact an string, the caller must assure that.
   */
  swoc::Errata override_assign(TxnConfigVar const& var, swoc::TextView const& text);

  /** Assign @a n to the double transaction overridable configuration @a var
   *
   * @param var Overridable variable.
   * @param text Value to assign.
   * @return Errors, if any.
   *
   * @note This does not check if @a var is in fact an double, the caller must assure that.
   */
  swoc::Errata override_assign(TxnConfigVar const& var, double f);

  swoc::Rv<ConfVarData> override_fetch(TxnConfigVar const& var);

  /** Look up the transaction overridable configuration variable @a name.
   *
   * @param name Variable name.
   * @return The variable, or @a nullptr if @a name is not found to be a valid.
   */
  static TxnConfigVar * find_override(swoc::TextView const& name);

  /** Retrieve transaction arg @a idx
   *
   * @param idx Index of argument.
   * @return The value for the transaction argument at index @a idx.
   */
  void * arg(int idx);

  /** Assign the transaction are at @a idx.
   *
   * @param idx Index of argument.
   * @param value Value to assign.
   */
  void arg_assign(int idx, void * value);

  static swoc::Rv<int> reserve_arg(swoc::TextView const &name, swoc::TextView const &description);

  /** Perform DSO load time intialization.
   *
   * @param errata Current errors.
   * @return @a errata, updated with any additional errors.
   */
  static swoc::Errata & init(swoc::Errata & errata);

  /** Gets the number of transactions between the Traffic Server proxy and the
   *  origin server from a single session. Any value greater than zero indicates connection reuse.
   *
   * @return int The number of transaction
   */
  int server_ssn_txn_count() const;
protected:
  using TxnConfigVarTable = std::unordered_map<swoc::TextView, std::unique_ptr<TxnConfigVar>, std::hash<std::string_view>>;

  TSHttpTxn _txn = nullptr;
  static TxnConfigVarTable _var_table;
  static std::mutex _var_table_lock;
  static int _arg_idx;

  /** Duplicate a string into TS owned memory.
   *
   * @param text String to duplicate.
   * @return The duplicated string.
   */
  swoc::MemSpan<char> ts_dup(swoc::TextView const& text);

  static void config_bool_record(swoc::Errata & errata, swoc::TextView name);
  static void config_integer_record(swoc::Errata & errata, swoc::TextView name);
  static void config_integer_record(swoc::Errata & errata, swoc::TextView name, int min, int max);
  static void config_string_record(swoc::Errata & errata, swoc::TextView name);
};

/** The Traffic Server proxy.
 *
 */
class Proxy {
  using self_type = Proxy; ///< Self reference type.
public:
protected:
};

// ----

int plugin_stat_index(swoc::TextView const& name);

int plugin_stat_value(int idx);

void plugin_stat_update(int idx, intmax_t value);

swoc::Rv<int> plugin_stat_define(swoc::TextView const& name, int value, bool persistent_p);

// ----

struct TaskHandle {
  /// Wrapper for data needed when the event is dispatched.
  struct Data {
    std::function<void ()> _f; ///< Functor to dispatch.
    std::atomic<bool> _active = true; ///< Set @c false if the task has been canceled.

    /// Construct from functor @a f.
    Data(std::function<void ()> && f) : _f(std::move(f)) {}
  };

  TSAction _action = nullptr; ///< Internal handle returned from task scheduling.
  TSCont _cont = nullptr; ///< Continuation for @a _action.

  /// Cancel the task.
  void cancel();
};

TaskHandle PerformAsTask(std::function<void ()> && task);

TaskHandle PerformAsTaskEvery(std::function<void ()> && task, std::chrono::milliseconds period);

inline HeapObject::HeapObject(TSMBuffer buff, TSMLoc loc) : _buff(buff), _loc(loc) {}

inline bool HeapObject::is_valid() const { return _buff != nullptr && _loc != nullptr; }

inline TSMBuffer HeapObject::mbuff() const { return _buff; }

inline TSMLoc HeapObject::mloc() const { return _loc; }

inline URL::URL(TSMBuffer buff, TSMLoc loc) : super_type(buff, loc) {}

inline swoc::TextView URL::path() const { int length; auto text = TSUrlPathGet(_buff, _loc, &length); return { text, static_cast<size_t>(length) }; }

inline swoc::TextView URL::query() const { int length; auto text = TSUrlHttpQueryGet(_buff, _loc, &length); return { text, static_cast<size_t>(length) }; }

inline URL &URL::host_set(swoc::TextView const& host) {
  if (this->is_valid()) {
    TSUrlHostSet(_buff, _loc, host.data(), host.size());
  }
  return *this;
}

inline auto URL::port_set(in_port_t port) -> self_type & {
  TSUrlPortSet(_buff, _loc, port);
  return *this;
}

inline URL &URL::scheme_set(swoc::TextView const& scheme) {
  if (this->is_valid()) {
    TSUrlSchemeSet(_buff, _loc, scheme.data(), scheme.size());
  }
  return *this;
}

inline URL &URL::query_set(swoc::TextView text) {
  if (this->is_valid()) {
    TSUrlHttpQuerySet(_buff, _loc, text.data(), text.size());
  }
  return *this;
}

inline HttpField::HttpField(TSMBuffer buff, TSMLoc hdr_loc, TSMLoc field_loc) : super_type(buff, field_loc), _hdr(hdr_loc) {}

inline HttpHeader::HttpHeader(TSMBuffer buff, TSMLoc loc) : super_type(buff, loc) {}

inline TxnConfigVar::TxnConfigVar(swoc::TextView const &name, TSOverridableConfigKey key
                           , TSRecordDataType type) : _name(name), _key(key), _ts_type(type) {}

inline TSHttpStatus HttpResponse::status() const { return TSHttpHdrStatusGet(_buff, _loc); }

inline HttpTxn::HttpTxn(TSHttpTxn txn) : _txn(txn) {}

inline HttpTxn::operator TSHttpTxn() const { return _txn; }

inline HttpSsn HttpTxn::ssn() const { return _txn ? TSHttpTxnSsnGet(_txn) : nullptr; }

inline unsigned HttpSsn::txn_count() const { return TSHttpSsnTransactionCount(_ssn); };


const swoc::TextView HTTP_FIELD_HOST { TS_MIME_FIELD_HOST, static_cast<size_t>(TS_MIME_LEN_HOST) };
const swoc::TextView HTTP_FIELD_LOCATION { TS_MIME_FIELD_LOCATION, static_cast<size_t>(TS_MIME_LEN_LOCATION) };
const swoc::TextView HTTP_FIELD_CONTENT_LENGTH { TS_MIME_FIELD_CONTENT_LENGTH, static_cast<size_t>(TS_MIME_LEN_CONTENT_LENGTH) };
const swoc::TextView HTTP_FIELD_CONTENT_TYPE { TS_MIME_FIELD_CONTENT_TYPE, static_cast<size_t>(TS_MIME_LEN_CONTENT_TYPE) };

const swoc::TextView URL_SCHEME_HTTP { TS_URL_SCHEME_HTTP, static_cast<size_t>(TS_URL_LEN_HTTP) };
const swoc::TextView URL_SCHEME_HTTPS { TS_URL_SCHEME_HTTPS, static_cast<size_t>(TS_URL_LEN_HTTPS) };

extern const swoc::Lexicon<TSRecordDataType> TSRecordDataTypeNames;
}; // namespace ts

namespace swoc {
  BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, TSHttpStatus status);
  BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, TSRecordDataType);
  BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, ts::ConfVarData const&);
} // namespace swoc

namespace std {

// Structured binding support for @c HeapObject.
template<> class tuple_size<ts::HeapObject> : public std::integral_constant<size_t, 2> {};
template<> class tuple_element<0, ts::HeapObject> { public: using type = TSMBuffer; };
template<> class tuple_element<1, ts::HeapObject> { public: using type = TSMLoc; };
template < size_t IDX > typename tuple_element<IDX, ts::HeapObject>::type get(ts::HeapObject const&);
template <> inline TSMBuffer get<0>(ts::HeapObject const& obj) { return obj.mbuff(); }
template <> inline TSMLoc get<1>(ts::HeapObject const& obj) { return obj.mloc(); }
} // namespace std
