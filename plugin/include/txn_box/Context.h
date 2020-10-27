/** @file
   Per transaction context types.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#pragma once

#include <memory>
#include <functional>

#include <swoc/MemArena.h>
#include <swoc/Errata.h>

#include "txn_box/common.h"
#include "txn_box/Rxp.h"
#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Expr.h"
#include "txn_box/ts_util.h"
#include <ts/remap.h>

struct _tm_remap_request_info;
using TSRemapRequestInfo = _tm_remap_request_info;

/** Per transaction context.
 *
 * This holds data associated with a specific transaction, along with pointers / references to
 * global structures, such that a transaction based hook can retrieve all necessary information
 * from a point to an instance of this class.
 */
class Context {
  using self_type = Context;
  /// Cleanup functor for an inverted arena.
  /// @internal Because the arena is inverted, calling the destructor will clean up everything.
  /// For this reason @c delete must @b not be called (that will double free).
  struct ArenaDestructor {
    void operator()(swoc::MemArena* arena);
  };
public:
  /// Transaction local storage.
  /// This is a pointer so that the arena can be inverted to minimize allocations.
  std::unique_ptr<swoc::MemArena, ArenaDestructor> _arena;

  /// Construct based a specific configuration.
  explicit Context(std::shared_ptr<Config> const& cfg);

  ~Context();

  /** Schedule a directive for a @a hook.
   *
   * @param hook Hook on which to invoke.
   * @param drtv Directive to invoke.
   * @return Errors, if any.
   */
  swoc::Errata on_hook_do(Hook hook, Directive *drtv);

  /** Invoke directives for @a hook.
   *
   * @param hook Hook index.
   * @return Errors, if any/
   */
  swoc::Errata invoke_for_hook(Hook hook);

  /** Invoke directives for remap.
   *
   * @param rule_cfg  Remap rule configuration.
   * @param rri Remap rule info from TS.
   * @return Errors, if any
   */
  swoc::Errata invoke_for_remap(Config &rule_cfg, TSRemapRequestInfo *rri);

  /** Set up to handle the hooks in the @a txn.
   *
   * @param txn TS transaction object.
   * @return @a this
   */
  self_type & enable_hooks(TSHttpTxn txn);

  /** Extract a feature.
   *
   * @param expr The feature expression.
   * @return The feature.
   *
   * This extracts the feature as described by @a expr. This feature is transient and will
   * potentially be overwritten by the next feature extraction. If the feature should be preserved
   * longer than that, use @c commit.
   *
   * The purpose of this is to permit examining the feature after extraction and before committing
   * it to transaction local memory.
   *
   * @see commit
   */
  Feature extract(Expr const& expr);

  enum ViewOption {
    EX_COMMIT, ///< Force transient to be committed
    EX_C_STR ///< Force C-string (null terminated)
  };

  FeatureView extract_view(Expr const& expr, std::initializer_list<ViewOption> opts = {});

  /** Commit a feature.
   *
   * @param feature Feature to commit.
   * @return @a feature (after possible modification)
   *
   * This causes the feature data in @a feature to be committed such that it will no longer be
   * overwritten by future feature extractions. This @b must be called on the most recently
   * extracted feature. This may cause @a feature to be modified.
   *
   * @see extract
   */
  Feature& commit(Feature & feature);

  /** Allocate context (txn scoped) space for an array of @a T.
   *
   * @tparam T Element type.
   * @param count # of elements.
   * @return A span covering the allocated array.
   *
   * This allocates in the context storage. No destructors are called when the config is destructed.
   *
   * @see mark_for_cleanup
   */
  template < typename T > swoc::MemSpan<T> span(unsigned count);

  /** Access per context storage for directive @a drtv.
   *
   * @param drtv Directive pointer.
   * @return Storage allocated.
   *
   * This must have been allocated during configuration loading by calling @c Config::reserve_ctx_storage.
   *
   * @see Config::reserve_ctx_storage
   */
  swoc::MemSpan<void> storage_for(Directive const* drtv);

  swoc::MemSpan<void> storage_for(Directive::CfgInfo const* rtti);

  Hook _cur_hook = Hook::INVALID;
  TSCont _cont = nullptr;
  ts::HttpTxn _txn = nullptr;

  /// Current extracted feature.
  Feature _active;
  /// Extension for active feature when needed.
  Feature _active_ext;
  /// Feature remnant, after matching.
  FeatureView _remainder;
  /// Should the active feature be updated (e.g., is used later).
  bool _update_remainder_p = false;

  /// Context for working with PCRE - allocates from the transaction arena.
  pcre2_general_context* _rxp_ctx = nullptr;

  /** Set capture groups for a literal match.
   *
   * @param text The literal text.
   *
   * THis is used to set capture group 0 for literal matches.
   */
  void set_literal_capture(swoc::TextView text);

  /** BWF interface for name binding.
   *
   * @param w Output writer.
   * @param spec Specifier with name to bind.
   *
   * Generate output to @a w based on data in @a spec.
   * Conceptually
   */
  void operator()(swoc::BufferWriter& w, Extractor::Spec const& spec);

  /** Class for handling numbered arguments to formatting.
   *
   * The primary use is for mapping regular expression capture groups to indices.
   */
  class ArgPack : public swoc::bwf::ArgPack {
  public:
    explicit ArgPack(Context& ctx) : _ctx(ctx) {} ///< Default constructor.

    /** Get argument at index @a idx.
      *
      * @param idx Argument index.
      * @return The argument value.
      *
      * Should have BWF supply a default implementation that throws - this is used in so few
      * cases it shouldn't have to be implemented by default.
      */
    std::any capture(unsigned idx) const override;

    /** Generate formatted output for an argument.
     *
     * @param idx Argument index.
     * @param w Output.
     * @param spec Formatting specifier (should be a @c Extractor::Spec)
     * @return @a w
     *
     * For now this is unimplemented - it will get filled out when regex support is done.
     */
    swoc::BufferWriter &print(swoc::BufferWriter &w, swoc::bwf::Spec const &spec, unsigned idx) const override;

    /// Number of arguments in the pack.
    unsigned count() const override;

    /// Transaction context.
    Context& _ctx;
  };

  /// Wrapper for top level directives.
  /// This is used to handle both configuration level directives and directives scheduled by @c when.
  struct Callback {
    using self_type = Callback; ///< Self reference type.
  protected:
    Directive * _drtv = nullptr; ///< Directive to invoke for the callback.
    self_type * _next = nullptr; ///< Intrusive list link.
    self_type * _prev = nullptr; ///< Intrusive list link.
  public:
    /// Intrusive list descriptor.
    using Linkage = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;
    /// Constructor.
    /// @param drtv Directive for the callback.
    Callback(Directive* drtv) : _drtv(drtv) {}
    /// Call the directive in @a this.
    /// @param ctx Transaction context for the callback.
    /// @return Errors, if any.
    swoc::Errata invoke(Context& ctx);
  };

  /// Directives for a particular hook.
  struct HookInfo {
    /// @c IntrusiveDList support.
    using List = swoc::IntrusiveDList<Callback::Linkage>;
    List cb_list; ///< List of directives to call back.
    bool hook_set_p = false; ///< If a TS level callback for this hook has already been set.
  };

  /// State of each global config hook for this transaction / context.
  std::array<HookInfo, std::tuple_size<Hook>::value> _hooks;

  ts::HttpRequest ua_req_hdr(); ///< @return user agent (client) request.
  ts::HttpRequest proxy_req_hdr(); ///< @return proxy request.
  ts::HttpResponse upstream_rsp_hdr(); ///< @return upstream request.
  ts::HttpResponse proxy_rsp_hdr(); ///< @return proxy response.

  /** Store a transaction variable.
   *
   * @param name Variable name.
   * @param value Variable value.
   * @return @a this
   */
  self_type & store_txn_var(swoc::TextView const& name, Feature && value);

  /** Store a transaction variable.
   *
   * @param name Variable name.
   * @param value Variable value.
   * @return @a this
   */
  self_type & store_txn_var(swoc::TextView const& name, Feature & value);

  /** Load a transaction variable.
   *
   * @param name Variable name.
   * @return Value of the variable.
   */
  Feature const& load_txn_var(swoc::TextView const& name);

  /// Storage for remap txn information, if a remap rule is active.
  TSRemapRequestInfo* _remap_info = nullptr;
  /// Value to return from a remap invocation.
  TSRemapStatus _remap_status = TSREMAP_NO_REMAP;

  /** Working match data for doing PCRE matching.
   *
   * @param n Number of capture groups required.
   * @return Cpature data sufficient to match @a n groups.
   */
  self_type & rxp_match_require(unsigned n);

  pcre2_match_data * rxp_working_match_data() { return _rxp_working; }

  /// Commit the working match data as the active match data.
  pcre2_match_data * rxp_commit_match(swoc::TextView const& src);

  /** Make a transaction local copy of @a text that is a C string if needed.
   *
   * @param text Current string.
   * @return @a test if it is a C-string, a null terminated copy if not.
   *
   * The copy is stored in transaction scoped memory.
   */
  swoc::TextView localize_as_c_str(swoc::TextView text);

  /// Clear transaction headers - not reliable across hooks.
  void clear_cache();

  /** Mark @a ptr for cleanup when @a this is destroyed.
   *
   * @tparam T Type of @a ptr
   * @param ptr Object to clean up.
   * @return @a this
   *
   * @a ptr is cleaned up by calling
   */
  template <typename T> self_type & mark_for_cleanup(T* ptr);

  /** Get a reference to the configuration for @a this.
   *
   * @return The configuration.
   *
   * Use when local access to the configuration is needed.
   */
  Config& cfg();

  /** Get a reference to the configuration.
   *
   * @return A shared reference to the configuration to @a this.
   *
   * Use when the configuration needs to persist.
   */
  std::shared_ptr<Config> acquire_cfg();

  bool is_terminal() const { return _terminal_p; }
  self_type & mark_terminal(bool flag) { _terminal_p= flag; return *this; }

protected:
  // HTTP header objects for the transaction.
  ts::HttpRequest _ua_req; ///< Client request header.
  ts::HttpRequest _proxy_req; ///< Proxy request header.
  ts::HttpResponse _upstream_rsp; ///< Upstream response header.
  ts::HttpResponse _proxy_rsp; ///< Proxy response header.

  /// Base / Global configuration object.
  std::shared_ptr<Config> _cfg;

  /// Directive shared storage.
  swoc::MemSpan<void> _ctx_store;

  /// Invoke the callbacks for the current hook.
  swoc::Errata invoke_callbacks();

  /// Active regex capture data.
  pcre2_match_data * _rxp_active = nullptr;

  /// Temporary / working capture group data.
  /// If successful, this becomes active via @c rxp_commit_match
  /// @see rxp_commit_match
  pcre2_match_data * _rxp_working = nullptr;

  /// Number of capture groups supported by current match data allocations.
  unsigned _rxp_n = 0;

  /// Active full to which the capture groups refer.
  FeatureView _rxp_src;

  /// Additional clean up needed when @a this is destroyed.
  swoc::IntrusiveDList<Finalizer::Linkage> _finalizers;

  /// A transaction scope variable.
  struct TxnVar {
    using self_type = TxnVar; ///< Self reference type.
    static constexpr std::hash<std::string_view> Hash_Func{};

    swoc::TextView _name; ///< Name of variable.
    Feature _value; ///< Value of variable.
    self_type * _next = nullptr; ///< Intrusive link.
    self_type * _prev = nullptr; ///< Intrusive link.

    /// Linkage for @c IntrusiveHashMap.
    struct Linkage : public swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev> {
      static swoc::TextView key_of(self_type* self) { return self->_name; }
      static auto hash_of(swoc::TextView const& text) -> decltype(Hash_Func(text)) { return Hash_Func(text); }
      static bool equal(swoc::TextView const& lhs, swoc::TextView const& rhs) { return lhs == rhs; }
    };

    TxnVar(swoc::TextView const& name, Feature const& value) : _name(name), _value(value) {}
  };

  using TxnVariables = swoc::IntrusiveHashMap<TxnVar::Linkage>;
  TxnVariables _txn_vars; ///< Variables for the transaction.

  /// Flag for continuing invoking directives.
  bool _terminal_p = false;

  /** Entry point from TS via plugin API.
   *
   * @param cont TS Continuation.
   * @param evt Event type.
   * @param payload Ignored.
   * @return 0
   *
   * The @c Context instance is carried as the Continuation data.
   */
  static int ts_callback(TSCont cont, TSEvent evt, void * payload);
};

// --- Implementation ---

inline auto Context::store_txn_var(swoc::TextView const&name, Feature&&value) -> self_type & {
  return this->store_txn_var(name, value);
}

template < typename T > Context& Context::mark_for_cleanup(T *ptr) {
  auto f = _arena->make<Finalizer>(ptr, [](void* ptr) { std::destroy_at(static_cast<T*>(ptr)); });
  _finalizers.append(f);
  return *this;
}

template<typename T>
swoc::MemSpan<T> Context::span(unsigned int count) {
  return _arena->alloc(sizeof(T) * count).rebind<T>();
}

inline Config& Context::cfg() { return *_cfg; }

inline std::shared_ptr<Config> Context::acquire_cfg() { return _cfg; }

inline swoc::Errata Context::Callback::invoke(Context& ctx) { return _drtv->invoke(ctx); }

inline void Context::ArenaDestructor::operator()(swoc::MemArena *arena) { arena->swoc::MemArena::~MemArena(); }

inline void Context::clear_cache() {
  _ua_req = {};
  _proxy_req = {};
  _upstream_rsp = {};
  _proxy_rsp = {};
}
