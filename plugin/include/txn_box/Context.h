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
    void operator()(swoc::MemArena* arena) { arena->swoc::MemArena::~MemArena(); }
  };
public:
  /// Transaction local storage.
  /// This is a pointer so that the arena can be inverted to minimize allocations.
  std::unique_ptr<swoc::MemArena, ArenaDestructor> _arena;

  /// Construct based a specific configuration.
  explicit Context(std::shared_ptr<Config> const& cfg);

  swoc::Errata on_hook_do(Hook hook, Directive *drtv);

  swoc::Errata invoke_for_hook(Hook hook);
  swoc::Errata invoke_for_remap(Config &rule_cfg, TSRemapRequestInfo *rri);

  /** Set up to handle the hooks in the @a txn.
   *
   * @param txn TS transaction object.
   * @return @a this
   */
  self_type & enable_hooks(TSHttpTxn txn);

  /** Extract a feature.
   *
   * @param fmt The extraction format.
   * @return The feature.
   *
   * This extracts the feature as described by @a fmt. This feature is transient and will potentially
   * be overwritten by the next feature extraction. If the feature should be preserved longer than
   * that, use @c commit.
   *
   * The purpose of this is to permit examining the feature after extraction and before committing
   * it to transaction local memory.
   *
   * @see commit
   */
  Feature extract(Extractor::Format const& fmt);

  /** Commit a feature.
   *
   * @param feature Feature to commit.
   * @return @a this
   *
   * This causes the feature data in @a feature to be committed such that it will no longer be
   * overwritten by future feature extractions. This @b must be called on the most recently
   * extracted feature.
   *
   * @see extract
   */
  self_type& commit(Feature const& feature);

  swoc::MemSpan<void> storage_for(Directive* drtv);

  Hook _cur_hook = Hook::INVALID;
  TSCont _cont = nullptr;
  ts::HttpTxn _txn = nullptr;
  /// Current extracted feature data.
  Feature _feature;

  void operator()(swoc::BufferWriter& w, Extractor::Spec const& spec);

  class ArgPack : public swoc::bwf::ArgPack {
  public:
    explicit ArgPack(Context& ctx) : _ctx(ctx) {}

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
    swoc::BufferWriter &print(unsigned idx, swoc::BufferWriter &w, swoc::bwf::Spec const &spec) const override;

    /// Number of arguments in the pack.
    unsigned count() const override;

    Context& _ctx;
  };

  struct Callback {
    using self_type = Callback;
  protected:
    Directive * _drtv = nullptr; ///< Directive to invoke for the callback.
    self_type * _next = nullptr; ///< Intrusive list link.
    self_type * _prev = nullptr; ///< Intrusive list link.
    /// Intrusive list descriptor.
  public:
    using Linkage = swoc::IntrusiveLinkage<self_type, &self_type::_next, &self_type::_prev>;
    Callback(Directive* drtv) : _drtv(drtv) {}
    swoc::Errata invoke(Context& ctx) { return _drtv->invoke(ctx); }
  };

  /// Directives for a particular hook.
  struct HookInfo {
    using List = swoc::IntrusiveDList<Callback::Linkage>;
    List cb_list; ///< List of directives to call back.
    bool hook_set_p = false; ///< If a hook has already been set.
  };

  /// State of each global config hook for this transaction / context.
  std::array<HookInfo, std::tuple_size<Hook>::value> _hooks;

  ts::HttpHeader creq_hdr();
  ts::HttpHeader preq_hdr();
  ts::HttpHeader ursp_hdr();
  ts::HttpHeader prsp_hdr();

  /// Context for working with PCRE - allocates from the transaction arena.
  pcre2_general_context* _rxp_ctx = nullptr;

  /// Active regex capture data.
  pcre2_match_data *_rxp_capture = nullptr;
  /// Active view to which the capture groups refer.
  FeatureView _rxp_src;

  /// Temporary / working capture group data.
  pcre2_match_data *_rxp_working = nullptr;

  /// Promote the working capture group data to active capture group data.
  pcre2_match_data* promote_capture_data() {
    std::swap(_rxp_capture, _rxp_working);
    return _rxp_capture;
  }

  TSRemapRequestInfo* _remap_info = nullptr;
  TSRemapStatus _remap_status = TSREMAP_NO_REMAP;

  /** Clear cached data.
   *
   */
  void clear_cache() {
    _creq = {};
    _preq = {};
    _ursp = {};
    _prsp = {};
  }

protected:
  // HTTP header objects for the transaction.
  ts::HttpHeader _creq; ///< Client request header.
  ts::HttpHeader _preq; ///< Proxy request header.
  ts::HttpHeader _ursp; ///< Upstream response header.
  ts::HttpHeader _prsp; ///< Proxy response header.

  /// Base / Global configuration object.
  std::shared_ptr<Config> _cfg;

  /// Directive shared storage.
  swoc::MemSpan<void> _ctx_store;

  swoc::Errata invoke_callbacks();

  static int ts_callback(TSCont cont, TSEvent evt, void * payload);
};
