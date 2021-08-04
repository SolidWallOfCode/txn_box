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
using Clock = std::chrono::system_clock;

/* ------------------------------------------------------------------------------------ */
/// Define a static text block.
class Do_text_block_define : public Directive
{
  using self_type  = Do_text_block_define; ///< Self reference type.
  using super_type = Directive;            ///< Parent type.
protected:
  struct CfgInfo;

public:
  static inline const std::string KEY{"text-block-define"}; ///< Directive name.
  static const HookMask HOOKS;                              ///< Valid hooks for directive.

  static constexpr Options OPTIONS{sizeof(CfgInfo *)};

  /// Functor to do file content updating as needed.
  struct Updater {
    std::weak_ptr<Config> _cfg;   ///< Configuration.
    Do_text_block_define *_block; ///< Text block holder.

    void operator()(); ///< Do the update check.
  };

  ~Do_text_block_define() noexcept;

  Errata invoke(Context &ctx) override; ///< Runtime activation.

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
  static Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                         swoc::TextView const &arg, YAML::Node key_value);

  /** Create config level shared data.
   *
   * @param cfg Configuration.
   * @param rtti Static configuration data
   * @return
   */
  static Errata cfg_init(Config &cfg, CfgStaticData const *rtti);

protected:
  using Map       = std::unordered_map<TextView, self_type *, std::hash<std::string_view>>;
  using MapHandle = std::unique_ptr<Map>;

  /// Config level data for all text blocks.
  struct CfgInfo {
    MapHandle _map; ///< Map of names to specific text block definitions.
  };

  TextView _name;                                                             ///< Block name.
  swoc::file::path _path;                                                     ///< Path to file (optional)
  std::optional<TextView> _text;                                              ///< Default literal text (optional)
  feature_type_for<DURATION> _duration;                                       ///< Time between update checks.
  std::atomic<Clock::duration> _last_check = Clock::now().time_since_epoch(); ///< Absolute time of the last alert.
  Clock::time_point _last_modified;                                           ///< Last modified time of the file.
  std::shared_ptr<std::string> _content;                                      ///< Content of the file.
  int _line_no = 0;                                                           ///< For debugging name conflicts.
  std::shared_mutex _content_mutex;                                           ///< Lock for access @a content.
  ts::TaskHandle _task;                                                       ///< Handle for periodic checking task.

  FeatureGroup _fg;
  using index_type                  = FeatureGroup::index_type;
  static auto constexpr INVALID_IDX = FeatureGroup::INVALID_IDX;
  index_type _notify_idx;

  static inline const std::string NAME_TAG{"name"};
  static inline const std::string PATH_TAG{"path"};
  static inline const std::string TEXT_TAG{"text"};
  static inline const std::string DURATION_TAG{"duration"};
  static inline const std::string NOTIFY_TAG{"notify"};

  /// Map of names to text blocks.
  static Map *map(Directive::CfgStaticData const *rtti);

  Do_text_block_define() = default;

  friend class Ex_text_block;
  friend Updater;
};

const HookMask Do_text_block_define::HOOKS{MaskFor(Hook::POST_LOAD)};

Do_text_block_define::~Do_text_block_define() noexcept
{
  _task.cancel();
}

auto
Do_text_block_define::map(Directive::CfgStaticData const *rtti) -> Map *
{
  return rtti->_cfg_store.rebind<CfgInfo *>()[0]->_map.get();
}

Errata
Do_text_block_define::invoke(Context &ctx)
{
  // Set up the update checking.
  if (_duration.count()) {
    _task =
      ts::PerformAsTaskEvery(Updater{ctx.acquire_cfg(), this}, std::chrono::duration_cast<std::chrono::milliseconds>(_duration));
  }
  return {};
}

Rv<Directive::Handle>
Do_text_block_define::load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &,
                           swoc::TextView const &, YAML::Node key_value)
{
  auto self = new self_type();
  Handle handle(self);
  auto &fg       = self->_fg;
  self->_line_no = drtv_node.Mark().line;

  auto errata =
    self->_fg.load(cfg, key_value, {{NAME_TAG, FeatureGroup::REQUIRED}, {PATH_TAG}, {TEXT_TAG}, {DURATION_TAG}, {NOTIFY_TAG}});

  if (!errata.is_ok()) {
    errata.info(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return errata;
  }
  auto idx = fg.index_of(NAME_TAG);

  // Must have a NAME, and either TEXT or PATH. DURATION is optional, but must be a duration if present.
  auto &name_expr{fg[idx]._expr};
  if (!name_expr.is_literal() || !name_expr.result_type().can_satisfy(STRING)) {
    return Error("{} value for {} directive at {} must be a literal string.", NAME_TAG, KEY, drtv_node.Mark());
  }
  self->_name = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._raw));

  if (auto path_idx = fg.index_of(PATH_TAG); path_idx != INVALID_IDX) {
    auto &path_expr = fg[path_idx]._expr;
    if (!path_expr.is_literal() || !path_expr.result_type().can_satisfy(STRING)) {
      return Error("{} value for {} directive at {} must be a literal string.", PATH_TAG, KEY, drtv_node.Mark());
    }
    self->_path = cfg.localize(ts::make_absolute(std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(path_expr._raw))).view().data(),
                               Config::LOCAL_CSTR);
  }

  if (auto text_idx = fg.index_of(TEXT_TAG); text_idx != INVALID_IDX) {
    auto &text_expr = fg[text_idx]._expr;
    if (!text_expr.is_literal() || !text_expr.result_type().can_satisfy(STRING)) {
      return Error("{} value for {} directive at {} must be a literal string.", TEXT_TAG, KEY, drtv_node.Mark());
    }
    self->_text = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(text_expr._raw));
  }

  if (!self->_text.has_value() && self->_path.empty()) {
    return Error("{} directive at {} must have a {} or a {} key.", KEY, drtv_node.Mark(), PATH_TAG, TEXT_TAG);
  }

  if (auto dur_idx = fg.index_of(DURATION_TAG); dur_idx != INVALID_IDX) {
    auto &dur_expr = fg[dur_idx]._expr;
    if (!dur_expr.is_literal()) {
      return Error("{} value for {} directive at {} must be a literal duration.", DURATION_TAG, KEY, drtv_node.Mark());
    }
    auto &&[dur_value, dur_value_errata]{std::get<Expr::LITERAL>(dur_expr._raw).as_duration()};
    if (!dur_value_errata.is_ok()) {
      return Error("{} value for {} directive at {} is not a valid duration.", DURATION_TAG, KEY, drtv_node.Mark());
    }
    self->_duration = dur_value;
  }

  self->_notify_idx = fg.index_of(NOTIFY_TAG);

  if (!self->_path.empty()) {
    std::error_code ec;
    auto content = swoc::file::load(self->_path, ec);
    if (!ec) {
      self->_content = std::make_shared<std::string>(std::move(content));
    } else if (self->_text.has_value()) {
      self->_content = nullptr;
    } else {
      return Error(R"("{}" directive at {} - value "{}" for key "{}" is not readable [{}] and no alternate "{}" key was present.)",
                   KEY, drtv_node.Mark(), self->_path, PATH_TAG, ec, TEXT_TAG);
    }
    self->_last_modified = swoc::file::modify_time(swoc::file::status(self->_path, ec));
  }

  // Put the directive in the map.
  Map *map = self->map(rtti);
  if (auto spot = map->find(self->_name); spot != map->end()) {
    return Error(R"("{}" directive at {} has the same name "{}" as another instance at line {}.)", KEY, drtv_node.Mark(),
                 self->_name, spot->second->_line_no);
  }
  (*map)[self->_name] = self;

  return handle;
}

Errata
Do_text_block_define::cfg_init(Config &cfg, CfgStaticData const *rtti)
{
  // Get space for instance.
  auto cfg_info = cfg.allocate_cfg_storage(sizeof(CfgInfo), 8).rebind<CfgInfo>().data();
  // Initialize it.
  new (cfg_info) CfgInfo;
  // Remember where it is.
  rtti->_cfg_store.rebind<CfgInfo *>()[0] = cfg_info;
  // Create the map.
  cfg_info->_map.reset(new Map);
  // Clean it up when the config is destroyed.
  cfg.mark_for_cleanup(cfg_info);
  return {};
}

void
Do_text_block_define::Updater::operator()()
{
  auto cfg = _cfg.lock(); // Make sure the config is still around while work is done.
  if (!cfg) {
    return; // presume the config destruction is ongoing and will clean this up.
  }

  // This should be scheduled at the appropriate intervals and so no need to check time.
  std::error_code ec;
  auto fs = swoc::file::status(_block->_path, ec);
  if (!ec) {
    auto mtime = swoc::file::modify_time(fs);
    if (mtime <= _block->_last_modified) {
      return; // same as it ever was...
    }
    auto content = std::make_shared<std::string>();
    *content     = swoc::file::load(_block->_path, ec);
    if (!ec) { // swap in updated content.
      {
        std::unique_lock lock(_block->_content_mutex);
        _block->_content       = content;
        _block->_last_modified = mtime;
      }
      if (_block->_notify_idx != FeatureGroup::INVALID_IDX) {
        Context ctx(cfg);
        auto text{_block->_fg.extract(ctx, _block->_notify_idx)};
        auto msg = ctx.render_transient([&](BufferWriter &w) { w.print("[{}] {}", Config::PLUGIN_TAG, text); });
        ts::Log_Note(msg);
      }
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
class Ex_text_block : public Extractor
{
  using self_type  = Ex_text_block; ///< Self reference type.
  using super_type = Extractor;     ///< Parent type.
public:
  static constexpr TextView NAME{"text-block"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  Feature extract(Context &ctx, Spec const &spec) override;
  BufferWriter &format(BufferWriter &w, Spec const &spec, Context &ctx) override;
};

Rv<ActiveType>
Ex_text_block::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to specify the defined text block.)", NAME);
  }
  auto view       = cfg.alloc_span<TextView>(1);
  view[0]         = cfg.localize(TextView{arg});
  spec._data.span = view.rebind<void>();
  return {STRING};
}

Feature
Ex_text_block::extract(Context &ctx, const Spec &spec)
{
  auto arg = spec._data.span.rebind<TextView>()[0];
  if (auto rtti = ctx.cfg().drtv_info(Do_text_block_define::KEY); nullptr != rtti) {
    // If there's file content, get a shared pointer to it to preserve the full until
    // the end of the transaction.
    auto map = Do_text_block_define::map(rtti);
    if (auto spot = map->find(arg); spot != map->end()) {
      auto block = spot->second;
      // This needs to persist until the end of the invoking directive. There's no direct
      // support for that so the best that can be done is to persist until the end of the
      // transaction by putting it in context storage.
      std::shared_ptr<std::string> &content = *(ctx.make<std::shared_ptr<std::string>>());

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

BufferWriter &
Ex_text_block::format(BufferWriter &w, Spec const &spec, Context &ctx)
{
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */

namespace
{
Ex_text_block text_block;

[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Config::define<Do_text_block_define>();
  Extractor::define(Ex_text_block::NAME, &text_block);
  return true;
}();
} // namespace
