/** @file
   Text Block directives and extractors.

 * Copyright 2020 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
*/

#include <shared_mutex>

#include "txn_box/common.h"

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/ArenaWriter.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>
#include <swoc/bwf_ex.h>
#include <swoc/bwf_std.h>

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Comparison.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
namespace bwf = swoc::bwf;
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
/// Define a static text block.
class Do_text_block_define : public Directive {
  using self_type = Do_text_block_define; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
protected:
  struct CfgInfo;
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  static constexpr Options OPTIONS { sizeof(CfgInfo*) };

  /// Functor to do file content updating as needed.
  struct Updater {
    std::weak_ptr<Config> _cfg; ///< Configuration.
    Do_text_block_define * _block; ///< Text block holder.

    void operator()(); ///< Do the update check.
  };

  ~Do_text_block_define() noexcept;

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param rtti Configuration level static data for this directive.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, CfgStaticData const* rtti, YAML::Node drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node key_value);

  static Errata cfg_init(Config& cfg, CfgStaticData const* rtti);

protected:
  using Map = std::unordered_map<TextView, self_type *, std::hash<std::string_view>>;
  using MapHandle = std::unique_ptr<Map>;

  struct CfgInfo {
    MapHandle _map;
  };

  TextView _name; ///< Block name.
  swoc::file::path _path; ///< Path to file (optional)
  std::optional<TextView> _text; ///< Default literal text (optional)
  feature_type_for<DURATION> _duration; ///< Time between update checks.
  std::atomic<std::chrono::system_clock::duration> _last_check = std::chrono::system_clock::now().time_since_epoch(); ///< Absolute time of the last alert.
  std::chrono::system_clock::time_point _last_modified; ///< Last modified time of the file.
  std::shared_ptr<std::string> _content; ///< Content of the file.
  int _line_no = 0; ///< For debugging name conflicts.
  std::shared_mutex _content_mutex; ///< Lock for access @a content.
  ts::TaskHandle _task; ///< Handle for periodic checking task.

  static const std::string NAME_TAG;
  static const std::string PATH_TAG;
  static const std::string TEXT_TAG;
  static const std::string DURATION_TAG;

  /// Map of names to text blocks.
  static Map* map(Directive::CfgStaticData const * rtti);

  /// Check if it is time to do a modified check on the file content.
  bool should_check();

  Do_text_block_define() = default;

  friend class Ex_text_block;
  friend Updater;
};

const std::string Do_text_block_define::KEY{"text-block-define" };
const std::string Do_text_block_define::NAME_TAG{"name" };
const std::string Do_text_block_define::PATH_TAG{"path" };
const std::string Do_text_block_define::TEXT_TAG{"text" };
const std::string Do_text_block_define::DURATION_TAG{"duration" };
const HookMask Do_text_block_define::HOOKS{MaskFor(Hook::POST_LOAD)};

Do_text_block_define::~Do_text_block_define() noexcept {
  _task.cancel();
}

auto Do_text_block_define::map(Directive::CfgStaticData const * rtti) -> Map* {
  return rtti->_cfg_store.rebind<CfgInfo*>()[0]->_map.get();
}

bool Do_text_block_define::should_check() {
  using Clock = std::chrono::system_clock;
  bool zret = false;

  if (_duration.count() > 0) {
    Clock::duration last = _last_check; // required because CAS needs lvalue reference.
    auto now = Clock::now();                   // Current time_point.
    if (Clock::time_point(last) + _duration <= now) {
      // it's been long enough, swap out our time for the last time. The winner of this swap
      // does the actual alert, leaving its current time as the last alert time.
      zret = _last_check.compare_exchange_strong(last, now.time_since_epoch());
    }
  }
  return zret;
}

Errata Do_text_block_define::invoke(Context &ctx) {
  // Set up the update checking.
  if (_duration.count()) {
    _task = ts::PerformAsTaskEvery(Updater{ctx.acquire_cfg(), this}
                                   , std::chrono::duration_cast<std::chrono::milliseconds>(_duration)
    );
  }
  return {};
}

Rv<Directive::Handle> Do_text_block_define::load(Config& cfg, CfgStaticData const* rtti, YAML::Node drtv_node, swoc::TextView const&, swoc::TextView const&, YAML::Node key_value) {
  auto self = new self_type();
  Handle handle(self);
  self->_line_no = drtv_node.Mark().line;

  // Must have a NAME, and either TEXT or PATH. DURATION is optional, but must be a duration if present.

  auto name_node = key_value[NAME_TAG];
  if (!name_node) {
    return Error("{} directive at {} must have a {} key.", KEY, drtv_node.Mark(), NAME_TAG);
  }
  auto &&[name_expr, name_errata] { cfg.parse_expr(name_node) };
  if (! name_errata.is_ok()) {
    name_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
    return std::move(name_errata);
  }
  if (! name_expr.is_literal() || ! name_expr.result_type().can_satisfy(STRING)) {
    return Error("{} value at {} for {} directive at {} must be a literal string.", NAME_TAG, name_node.Mark(), KEY, drtv_node.Mark());
  }
  drtv_node.remove(name_node);
  self->_name = cfg.localize(TextView{std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._expr))});

  auto path_node = key_value[PATH_TAG];
  if (path_node) {
    auto &&[path_expr, path_errata]{cfg.parse_expr(path_node)};
    if (!path_errata.is_ok()) {
      path_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(path_errata);
    }
    if (! path_expr.is_literal() || ! path_expr.result_type().can_satisfy(STRING)) {
      return Error("{} value at {} for {} directive at {} must be a literal string.", PATH_TAG, path_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(path_node);
    self->_path = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(path_expr._expr));
    ts::make_absolute(self->_path);
  }

  auto text_node = key_value[TEXT_TAG];
  if (text_node) {
    auto &&[text_expr, text_errata]{cfg.parse_expr(text_node)};
    if (!text_errata.is_ok()) {
      text_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(text_errata);
    }
    if (! text_expr.is_literal() || ! text_expr.result_type().can_satisfy(STRING)) {
      return Error("{} value at {} for {} directive at {} must be a literal string.", TEXT_TAG, text_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(text_node); // ugly, need to fix the overall API.
    self->_text = cfg.localize(TextView{
        std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(text_expr._expr))});
  }

  if (! self->_text.has_value() && self->_path.empty()) {
    return Error("{} directive at {} must have a {} or a {} key.", KEY, drtv_node.Mark(), PATH_TAG, TEXT_TAG);
  }

  auto dur_node = key_value[DURATION_TAG];
  if (dur_node) {
    auto &&[dur_expr, dur_errata] = cfg.parse_expr(dur_node);
    if (! dur_errata.is_ok()) {
      dur_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(dur_errata);
    }
    if (! dur_expr.is_literal()) {
      return Error("{} value at {} for {} directive at {} must be a literal duration."
                   , DURATION_TAG, dur_node.Mark(), KEY, drtv_node.Mark());
    }
    auto && [ dur_value, dur_value_errata ] { std::get<Expr::LITERAL>(dur_expr._expr).as_duration()};
    if (! dur_value_errata.is_ok()) {
      return Error("{} value at {} for {} directive at {} is not a valid duration."
                   , DURATION_TAG, dur_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(dur_node);
    self->_duration = dur_value;
  }

  if (! self->_path.empty()) {
    std::error_code ec;
    auto content = swoc::file::load(self->_path, ec);
    if (!ec) {
      self->_content = std::make_shared<std::string>(std::move(content));
    } else if (self->_text.has_value()) {
      self->_content = nullptr;
    } else {
      return Error(R"("{}" directive at {} - value "{}" for key "{}" is not readable [{}] and no alternate "{}" key was present.)"
                   , KEY, drtv_node.Mark(), self->_path, PATH_TAG, ec, TEXT_TAG);
    }
    self->_last_modified = swoc::file::modify_time(swoc::file::status(self->_path, ec));
  }

  // Put the directive in the map.
  Map* map = self->map(rtti);
  if (auto spot = map->find(self->_name) ; spot != map->end()) {
    return Error(R"("{}" directive at {} has the same name "{}" as another instance at line {}.)"
    , KEY, drtv_node.Mark(), self->_name, spot->second->_line_no);
  }
  (*map)[self->_name] = self;

  return handle;
}

Errata Do_text_block_define::cfg_init(Config &cfg, CfgStaticData const* rtti) {
  // Get space for instance.
  auto cfg_info = cfg.allocate_cfg_storage(sizeof(CfgInfo), 8).rebind<CfgInfo>().data();
  // Initialize it.
  new (cfg_info) CfgInfo;
  // Remember where it is.
  rtti->_cfg_store.rebind<CfgInfo*>()[0] = cfg_info;
  // Create the map.
  cfg_info->_map.reset(new Map);
  // Clean it up when the config is destroyed.
  cfg.mark_for_cleanup(cfg_info);
  return {};
}

void Do_text_block_define::Updater::operator()() {
  auto cfg = _cfg.lock(); // Make sure the config is still around while work is done.
  if (!cfg) {
    return;
  }

  if (! _block->should_check()) {
    return; // not time yet.
  }

  std::error_code ec;
  auto fs = swoc::file::status(_block->_path, ec);
  if (!ec) {
    auto mtime = swoc::file::modify_time(fs);
    if (mtime <= _block->_last_modified) {
      return; // same as it ever was...
    }
    auto content = std::make_shared<std::string>();
    *content = swoc::file::load(_block->_path, ec);
    if (!ec) { // swap in updated content.
      std::unique_lock lock(_block->_content_mutex);
      _block->_content = content;
      _block->_last_modified = mtime;
      return;
    }
  }
  // If control flow gets here, the file is no longer accessible and the content
  // should be cleared. If the file shows up again, it should have a modified time
  // later than the previously existing file, so that can be left unchanged.
  std::unique_lock lock(_block->_content_mutex);
  _block->_content.reset();
}

/* ------------------------------------------------------------------------------------ */
/// Text block extractor.
class Ex_text_block : public Extractor {
  using self_type = Ex_text_block; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
public:
  static constexpr TextView NAME{"text-block"};

  Rv<ActiveType> validate(Config& cfg, Spec& spec, TextView const& arg) override;

  Feature extract(Context & ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
};

Rv<ActiveType> Ex_text_block::validate(Config &cfg, Spec &spec, const TextView &arg) {
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to specify the defined text block.)", NAME);
  }
  auto view = cfg.alloc_span<TextView>(1);
  view[0] = cfg.localize(TextView{arg});
  spec._data = view.rebind<void>();
  return { STRING };
}

Feature Ex_text_block::extract(Context &ctx, const Spec &spec) {
  auto arg = spec._data.rebind<TextView>()[0];
  if ( auto rtti = ctx.cfg().drtv_info(Do_text_block_define::KEY) ; nullptr != rtti ) {
    // If there's file content, get a shared pointer to it to preserve the full until
    // the end of the transaction.
    auto map = Do_text_block_define::map(rtti);
    if (auto spot = map->find(arg) ; spot != map->end()) {
      auto block = spot->second;
      // This needs to persist until the end of the invoking directive. There's no direct
      // support for that so the best that can be done is to persist until the end of the
      // transaction by putting it in context storage.
      std::shared_ptr<std::string>& content = *(ctx.make<std::shared_ptr<std::string>>());

      { // grab a copy of the shared pointer to file content.
        std::shared_lock lock(block->_content_mutex);
        content = block->_content;
      }

      if (content) {
        ctx.mark_for_cleanup(&content); // only need to cleanup if non-nullptr.
        return FeatureView{*content};
      }
      // No file content, see if there's alternate text.
      if (block->_text.has_value()) {
        return FeatureView{block->_text.value()};
      }
    }
  }
  // Couldn't find the directive, or the block, or any content
  return NIL_FEATURE;
}

BufferWriter& Ex_text_block::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */

namespace {
Ex_text_block text_block;

[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Config::define<Do_text_block_define>();
  Extractor::define(Ex_text_block::NAME, &text_block);
  return true;
} ();
} // namespace
