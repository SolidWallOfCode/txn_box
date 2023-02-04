/** @file
   IP Space support.

 * Copyright 2020 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
*/

#include <shared_mutex>
#include <limits>

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>
#include <swoc/bwf_ip.h>
#include <swoc/bwf_std.h>
#include <swoc/Lexicon.h>

#include "txn_box/common.h"
#include "txn_box/Directive.h"
#include "txn_box/Modifier.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"
#include "txn_box/table_util.h"

#include "txn_box/yaml_util.h"
#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;
using swoc::BufferWriter;
using swoc::IPAddr;
using swoc::IPRange;
using swoc::IPSpace;
using swoc::MemSpan;

using namespace swoc::literals;
namespace bwf = swoc::bwf;
using namespace txb::table;

/* ------------------------------------------------------------------------------------ */
/// Define an IP Space
class Do_ip_space_define : public Directive, public txb::table::Base
{
  using self_type  = Do_ip_space_define; ///< Self reference type.
  using super_type = Directive;          ///< Parent type.
  using cfg_info = txb::table::CfgInfo;

public:
  using table_type = swoc::IPSpace<Row>;
  using table_data_type     = txb::table::Base::TableData<self_type>;
  using ctx_scope_data_type = txb::table::Base::CtxScopeBase<self_type, IPAddr>;

  static inline constexpr swoc::TextView KEY = "ip-table-define"; ///< Directive name.
  static const HookMask HOOKS;  ///< Valid hooks for directive.

  ~Do_ip_space_define() noexcept override = default; ///< Destructor.

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
  static swoc::Rv<Handle> load(Config &cfg, CfgStaticData const *rtti, YAML::Node drtv_node, swoc::TextView const &name,
                               swoc::TextView const &arg, YAML::Node key_value);

  /** Per config initialization.
   *
   * @param cfg Config.
   * @param rtti Defined directive data.
   * @return Errors, if any.
   */
  static Errata cfg_init(Config &cfg, CfgStaticData const *rtti);

protected:
  table_data_type::Handle _instance; ///< The IP Space
  std::shared_mutex _instance_mutex; ///< Reader / writer for @a _space.
  txb::ExternalFile _file; ///< External file handling.

  Do_ip_space_define() = default; ///< Default constructor.

  table_data_type::Handle table_data(); ///< Obtain stable reference to table data.

  /** Parse the input file.
   *
   * @param cfg Configuration instance.
   * @param content File content.
   * @return The parsed space, or errors.
   */
  Errata update_table(Config &cfg, TextView content);

  friend class Mod_ip_space;
  friend class Ex_ip_col;
};

const HookMask Do_ip_space_define::HOOKS{MaskFor(Hook::POST_LOAD)};

Errata
Do_ip_space_define::update_table(Config &cfg, TextView content)
{
  TextView line;
  unsigned line_no = 0;
  auto instance       = std::make_shared<table_data_type>();
  while (!(line = content.take_prefix_at('\n')).empty()) {
    ++line_no;
    line.trim_if(&isspace);
    if (line.empty() || '#' == line.front()) {
      continue;
    }
    auto token = line.take_prefix_at(',');
    IPRange range{token};
    if (range.empty()) {
      return Errata(S_ERROR, R"(Invalid range "{}" at line {}.)", token, line_no);
    }

    Row row = instance->_arena.alloc(_row_size).rebind<std::byte>();
    if ( auto errata = this->parse_row(cfg, row, line, line_no) ; ! errata.is_ok()) {
      return errata;
    }
    instance->_table.fill(range, row);
  }

  std::unique_lock lock(_instance_mutex);
  _instance = instance;

  return {};
}

Rv<Directive::Handle>
Do_ip_space_define::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &,
                         swoc::TextView const &, YAML::Node key_value)
{
  auto self = new self_type();
  Handle handle(self);
  self->_line_no = drtv_node.Mark().line;

  if (auto errata = self->parse_name(cfg, drtv_node) ; ! errata.is_ok()) {
    return std::move(errata.note("In directive {} at {}", KEY, drtv_node.Mark()));
  }

  // To simplify indexing, put in a "range" column as index 0, so config indices and internal
  // indices match up.
  self->_cols.emplace_back(Column{});
  self->_cols[0]._name = "range";
  self->_cols[0]._idx  = 0;
  self->_cols[0]._type = ColumnType::KEY;

  if (auto cols_node = key_value[COLUMNS_TAG] ; cols_node) {
    if (cols_node.IsMap()) {
      auto errata = self->parse_column_definitions(cfg, cols_node);
      if (!errata.is_ok()) {
        errata.note(R"(While parsing "{}" key at {}.)", COLUMNS_TAG, cols_node.Mark());
        return errata;
      }
    } else if (cols_node.IsSequence()) {
      for (auto child : cols_node) {
        auto errata = self->parse_column_definitions(cfg, child);
        if (!errata.is_ok()) {
          errata.note(R"(While parsing "{}" key at {}.)", COLUMNS_TAG, cols_node.Mark());
          return errata;
        }
      }
    } else {
      return Errata(S_ERROR, R"("{}" at {} must be an object or a list of objects.)", COLUMNS_TAG, cols_node.Mark());
    }
    drtv_node.remove(COLUMNS_TAG);
  }

  if (auto errata = self->_file.load(cfg, drtv_node) ; errata.is_ok()) {
    return std::move(errata.note("In directive {} at {}", KEY, drtv_node.Mark()));
  }

  self->_file.name(self->_name);
  self->_file.update_cb([=](Config& cfg, TextView content) -> Errata { return self->update_table(cfg, content);});

  /// Don't register until all loading has finished successfully.

  CfgInfo::instance(cfg, KEY)->register_drtv(self);

  return handle;
}

Errata
Do_ip_space_define::cfg_init(Config &cfg, CfgStaticData const *)
{
  auto cfg_info = cfg.obtain_named_object<CfgInfo>(KEY);
  // Scoped access to defined space in a @c Context.
  // Only one space can be active at a time therefore this can be shared among the instances in
  // a single @c Context. Use @c let to manage nested references.
  cfg_info->_reserved_span = cfg.reserve_ctx_storage(sizeof(ctx_scope_data_type));
  cfg.mark_for_cleanup(cfg_info);
  return {};
}

auto
Do_ip_space_define::table_data() -> table_data_type::Handle
{
  std::unique_lock lock(_instance_mutex);
  return _instance;
}

/* ------------------------------------------------------------------------------------ */
/// IPSpace modifier
/// Convert an IP address feature in to an IPSpace row.
class Mod_ip_space : public Modifier
{
  using self_type  = Mod_ip_space; ///< Self reference type.
  using super_type = Modifier;     ///< Parent type.
  using drtv_type = Do_ip_space_define; ///< Associated directive.

public:
  Mod_ip_space(Expr &&expr, TextView const &view, Do_ip_space_define *drtv);

  static inline constexpr swoc::TextView KEY{"ip-space"};

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context &ctx, feature_type_for<IP_ADDR> feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ex_type Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const &ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const &) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _expr;                          ///< Value expression.
  TextView _name;                      ///< Argument - IPSpace name.
  Do_ip_space_define *_drtv = nullptr; ///< The IPSpace define for @a _name
};

Mod_ip_space::Mod_ip_space(Expr &&expr, TextView const &name, Do_ip_space_define *drtv)
  : _expr(std::move(expr)), _name(name), _drtv(drtv)
{
}

bool
Mod_ip_space::is_valid_for(ActiveType const &ex_type) const
{
  return ex_type.can_satisfy(IP_ADDR);
}

ActiveType
Mod_ip_space::result_type(const ActiveType &) const
{
  return {NIL, STRING, INTEGER, ActiveType::TupleOf(STRING)};
}

Rv<Modifier::Handle>
Mod_ip_space::load(Config &cfg, YAML::Node node, TextView, TextView arg, YAML::Node key_value)
{
  auto *csi = CfgInfo::instance(cfg, drtv_type::KEY);
  drtv_type * drtv = nullptr;

  // Unfortunately supporting remap requires dynamic access.
  if (csi) { // global, resolve to the specific ipspace directive.
    drtv = static_cast<drtv_type*>(csi->drtv(arg));
    if (drtv == nullptr) {
      return Errata(S_ERROR, R"("{}" at {} is not the name of a defined IP space.)", arg, node.Mark());
    }
  } // else leave @a drtv null as a signal to find it dynamically.

  // Make info about active space available to expression parsing.
  Base * dummy = nullptr; // dummy for scope if no context.
  let<Base*> drtv_scope{csi ? csi->_active_drtv : dummy, drtv};
  auto &&[expr, errata]{cfg.parse_expr(key_value)};

  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(expr), cfg.localize(arg), drtv});
}

Rv<Feature>
Mod_ip_space::operator()(Context &ctx, feature_type_for<IP_ADDR> addr)
{
  // Set up local active state.
  drtv_type::ctx_scope_data_type active;
  auto drtv = _drtv; // may be locally updated, need a copy.
  if (drtv) {
    active._table = drtv->table_data();
  } else if ( nullptr != (drtv = CfgInfo::drtv<drtv_type>(ctx.cfg(), _name))) {
    active._table = drtv->table_data();
  }
  Feature value{FeatureView::Literal("")};
  if (active._table) {
    auto &&[range, payload] = *active._table->_table.find(addr);
    active._row             = range.empty() ? nullptr : payload;
    active._key             = addr;
    active._drtv            = drtv;

    // Current active data.
    auto * store = drtv_type::ctx_scope_data_type ::instance(ctx);
    // Temporarily update it to local conditions.
    let scope(*store, active);
    value = ctx.extract(_expr);
  }
  return value;
}

/* ------------------------------------------------------------------------------------ */
/// IP Space extractor.
class Ex_ip_col : public Extractor
{
  using self_type  = Ex_ip_col; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.
  using drtv_type = Do_ip_space_define;

public:
  static constexpr TextView NAME{"ip-col"};
  static constexpr auto INVALID_IDX = drtv_type::INVALID_IDX;

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  Feature extract(Context &ctx, Spec const &spec) override;
};

Rv<ActiveType>
Ex_ip_col::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  TextView parsed;
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to specify the column.)", NAME);
  }

  auto *mod_info = cfg.active_value<drtv_type::ctx_scope_data_type>(Mod_ip_space::KEY);
  if (!mod_info) {
    return Errata(S_ERROR, R"("{}" extractor can only be used with an active IP Space from the {} modifier.)", NAME, Mod_ip_space::KEY);
  }

  auto span       = cfg.allocate_cfg_storage(sizeof(drtv_type::ExInfo)).rebind<drtv_type::ExInfo>();
  spec._data.span = span;
  drtv_type::ExInfo &info      = span[0];
  if ( auto errata = info.init(cfg, mod_info->_drtv, arg) ; ! errata.is_ok() ) {
    return errata;
  }

  return {{ NIL, mod_info->_drtv->_cols[info._idx].active_type() }}; // any column can return @c NIL if the address isn't found.
}

Feature
Ex_ip_col::extract(Context &ctx, const Spec &spec)
{
  // Get all the pieces needed.
  if ( auto ctx_ai = drtv_type::ctx_scope_data_type::instance(ctx) ; ctx_ai ) {
    auto info = spec._data.span.rebind<Base::ExInfo>().data(); // Extractor local storage.
    return info->extract(ctx, ctx_ai, ctx_ai->_row, spec);
  }
  return NIL_FEATURE;
}

/* ------------------------------------------------------------------------------------ */

namespace
{
Ex_ip_col ex_ip_col;
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Config::define<Do_ip_space_define>();
  // Alias it as the old name.
  Config::define("do-ip-space-define", Do_ip_space_define::HOOKS, Do_ip_space_define::load, Do_ip_space_define::cfg_init);
  Modifier::define(Mod_ip_space::KEY, Mod_ip_space::load);
  Extractor::define(ex_ip_col.NAME, &ex_ip_col);
  return true;
}();
} // namespace
