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
class Do_ip_space_define;

namespace
{
/// IPSpace to store the rows.
using IPTable = IPSpace<Row>;

/// Key for defining directive.
/// Also used as the key for config level storage.
static inline constexpr swoc::TextView DRTV_KEY = "ip-space-define";

/// Space information that must be reloaded on file change.
struct TableInfo {
  IPTable table;          ///< IP table data.
  swoc::MemArena arena; ///< Row storage.

  /// Reference during active use.
  using Handle = std::shared_ptr<TableInfo>;
};

/// A single for all IP tables stored in the configuration arena.
struct CfgInfo {
  using self_type = CfgInfo; ///< Self reference type.

  /// Configuration level map of defined spaces.
  using TableMap = std::unordered_map<TextView, Do_ip_space_define *, std::hash<std::string_view>>;

  ReservedSpan _ctx_reserved_span; ///< Per context reserved storage.
  TableMap _map; ///< Map of defined spaces.

  /** Access the config level information from the configuration.
   *
   * @param cfg Configuration instance.
   * @return The table information.
   */
  static self_type * instance(Config & cfg) {
    return cfg.named_object<CfgInfo>(DRTV_KEY);
  }
};

/// Context information for the active IP Space.
/// This is set up by the @c ip-space modifier and is only valid in the expression scope.
struct CtxActiveInfo {
  using self_type = CtxActiveInfo; ///< Self reference type.

  IPAddr _addr;                         ///< Search address.
  TableInfo::Handle _info;              ///< Active space.
  Do_ip_space_define * _drtv = nullptr; ///< Active directive.
  Row *_row = nullptr;                  ///< Active row.

  /** Access the context level information.
   *
   * @param ctx The context instance.
   * @return The active table instance.
   */
  self_type * instance_from(Context & ctx) {
    auto info = CfgInfo::instance(ctx.cfg());
    return info ? ctx.storage_for(info->_ctx_reserved_span).rebind<CtxActiveInfo>().data() : nullptr;
  }
};

} // namespace
/* ------------------------------------------------------------------------------------ */
/// Define an IP Space
class Do_ip_space_define : public Directive, public txb::table::Base
{
  using self_type  = Do_ip_space_define; ///< Self reference type.
  using super_type = Directive;          ///< Parent type.
protected:
  /// Configuration level map of defined spaces.
  using Map = CfgInfo::TableMap;

public:
  static inline constexpr swoc::TextView KEY = DRTV_KEY; ///< Directive name.
  static const HookMask HOOKS;  ///< Valid hooks for directive.

  /// Functor to do file content updating as needed.
  struct Updater {
    std::weak_ptr<Config> _cfg; ///< Configuration.
    Do_ip_space_define *_block; ///< Space instance.

    void operator()(); ///< Do the update check.
  };

  ~Do_ip_space_define() noexcept override; ///< Destructor.

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

  static constexpr unsigned INVALID_IDX = std::numeric_limits<unsigned>::max();

  unsigned col_idx(swoc::TextView const &name);

protected:
  TableInfo::Handle _space;             ///< The IP Space
  std::shared_mutex _space_mutex; ///< Reader / writer for @a _space.

  ts::TaskHandle _task;                                  ///< Handle for periodic checking task.

  int _line_no = 0; ///< For debugging name conflicts.

  Do_ip_space_define() = default; ///< Default constructor.

  TableInfo::Handle acquire_space();

  /** Parse the input file.
   *
   * @param cfg Configuration instance.
   * @param content File content.
   * @return The parsed space, or errors.
   */
  Rv<TableInfo::Handle> parse_space(Config &cfg, TextView content);

  friend class Mod_ip_space;
  friend class Ex_ip_col;
  friend Updater;
};

const HookMask Do_ip_space_define::HOOKS{MaskFor(Hook::POST_LOAD)};

Do_ip_space_define::~Do_ip_space_define() noexcept
{
  _task.cancel();
}

Errata
Do_ip_space_define::invoke(Context &ctx)
{
  // Start update checking.
  if (_duration.count()) {
    _task =
      ts::PerformAsTaskEvery(Updater{ctx.acquire_cfg(), this}, std::chrono::duration_cast<std::chrono::milliseconds>(_duration));
  }
  return {};
}

auto
Do_ip_space_define::parse_space(Config &cfg, TextView content) -> Rv<TableInfo::Handle>
{
  TextView line;
  unsigned line_no = 0;
  auto space       = std::make_shared<TableInfo>();
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

    Row row = space->arena.alloc(_row_size).rebind<std::byte>();
    if ( auto errata = this->parse_columns(cfg, row, line, line_no) ; ! errata.is_ok()) {
      return errata;
    }
    space->table.fill(range, row);
  }
  return space;
}

Rv<Directive::Handle>
Do_ip_space_define::load(Config &cfg, CfgStaticData const *, YAML::Node drtv_node, swoc::TextView const &,
                         swoc::TextView const &, YAML::Node key_value)
{
  auto self = new self_type();
  Handle handle(self);
  self->_line_no = drtv_node.Mark().line;

  if (auto errata = self->parse_name(cfg, drtv_node, key_value) ; ! errata.is_ok()) {
    return std::move(errata.note("For directive {} at {}", KEY, drtv_node.Mark()));
  }

  if (auto errata = self->parse_path(cfg, drtv_node, key_value) ; ! errata.is_ok()) {
    return std::move(errata.note("For directive {} at {}", KEY, drtv_node.Mark()));
  }

  if (auto errata = self->parse_duration(cfg, drtv_node, key_value) ; ! errata.is_ok()) {
    return std::move(errata.note("For directive {} at {}", KEY, drtv_node.Mark()));
  }

  // To simplify indexing, put in a "range" column as index 0, so config indices and internal
  // indices match up.
  self->_cols.emplace_back(Column{});
  self->_cols[0]._name = "range";
  self->_cols[0]._idx  = 0;
  self->_cols[0]._type = ColumnType::KEY;
  self->_col_names.define(self->_cols[0]._idx, self->_cols[0]._name);

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

  std::error_code ec;
  auto content = swoc::file::load(self->_path, ec);
  if (ec) {
    return Errata(S_ERROR, "Unable to read input file {} for space {} - {}", self->_path, self->_name, ec);
  }
  self->_last_modified              = swoc::file::modify_time(swoc::file::status(self->_path, ec));
  auto &&[space_info, space_errata] = self->parse_space(cfg, content);
  if (!space_errata.is_ok()) {
    space_errata.note(R"(While parsing IPSpace file "{}" in space "{}".)", self->_path, self->_name);
    return std::move(space_errata);
  }
  self->_space = space_info;

  // Put the directive in the map.
  Map & map = CfgInfo::instance(cfg)->_map;
  if (auto spot = map.find(self->_name); spot != map.end()) {
    return Errata(S_ERROR, R"("{}" directive at {} has the same name "{}" as another instance at line {}.)", KEY, drtv_node.Mark(),
                 self->_name, spot->second->_line_no);
  }
  map[self->_name] = self;

  return handle;
}

Errata
Do_ip_space_define::cfg_init(Config &cfg, CfgStaticData const *)
{
  auto cfg_info = cfg.obtain_named_object<CfgInfo>(KEY);
  // Scoped access to defined space in a @c Context.
  // Only one space can be active at a time therefore this can be shared among the instances in
  // a single @c Context.
  cfg_info->_ctx_reserved_span = cfg.reserve_ctx_storage(sizeof(CtxActiveInfo));
  cfg.mark_for_cleanup(cfg_info); // takes a pointer to the object to clean up.
  return {};
}

void
Do_ip_space_define::Updater::operator()()
{
  auto cfg = _cfg.lock(); // Make sure the config is still around while work is done.
  if (!cfg) {
    return;
  }

  if (!_block->should_check()) {
    return; // not time yet.
  }

  std::error_code ec;
  auto fs = swoc::file::status(_block->_path, ec);
  if (!ec) {
    auto mtime = swoc::file::modify_time(fs);
    if (mtime <= _block->_last_modified) {
      return; // same as it ever was...
    }
    std::string content = swoc::file::load(_block->_path, ec);
    if (!ec) { // swap in updated content.
      auto &&[space, errata]{_block->parse_space(*cfg, content)};
      if (errata.is_ok()) {
        std::unique_lock lock(_block->_space_mutex);
        _block->_space = space;
      }
      _block->_last_modified = mtime;
      return;
    }
  }
}

unsigned
Do_ip_space_define::col_idx(TextView const &name)
{
  if (auto spot =
        std::find_if(_cols.begin(), _cols.end(), [&](Do_ip_space_define::Column &c) { return 0 == strcasecmp(c._name, name); });
      spot != _cols.end()) {
    return spot - _cols.begin();
  }
  // Not found.
  return INVALID_IDX;
}

SpaceHandle
Do_ip_space_define::acquire_space()
{
  std::shared_lock lock(_space_mutex);
  return _space;
}
/* ------------------------------------------------------------------------------------ */
/// IPSpace modifier
/// Convert an IP address feature in to an IPSpace row.
class Mod_ip_space : public Modifier
{
  using self_type  = Mod_ip_space; ///< Self reference type.
  using super_type = Modifier;     ///< Parent type.

public:
  Mod_ip_space(Expr &&expr, TextView const &view, Do_ip_space_define *drtv);

  static inline constexpr swoc::TextView KEY{"ip-space"};

  struct CfgActiveInfo {
    Do_ip_space_define *_drtv = nullptr;
  };

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
  auto *csi = Txb_IP_Space::cfg_info(cfg);
  CfgActiveInfo info;
  // Unfortunately supporting remap requires dynamic access.
  if (csi) { // global, resolve to the specific ipspace directive.
    auto &map = csi->_map;
    auto spot = map.find(arg);
    if (spot == map.end()) {
      return Errata(S_ERROR, R"("{}" at {} is not the name of a defined IP space.)", arg, node.Mark());
    }
    info._drtv = spot->second;
  } // else leave @a _drtv null as a signal to find it dynamically.

  // Make info about active space available to expression parsing.
  auto scope{cfg.active_value_let(KEY, &info)};
  auto &&[expr, errata]{cfg.parse_expr(key_value)};

  if (!errata.is_ok()) {
    errata.note(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(expr), cfg.localize(arg), info._drtv});
}

Rv<Feature>
Mod_ip_space::operator()(Context &ctx, feature_type_for<IP_ADDR> addr)
{
  // Set up local active state.
  CtxActiveInfo active;
  auto drtv = _drtv; // may be locally updated, need a copy.
  if (drtv) {
    active._space = drtv->acquire_space();
  } else {
    if (auto *csi = Txb_IP_Space::cfg_info(ctx.cfg()); nullptr != csi) {
      auto &map = csi->_map;
      if (auto spot = map.find(_name); spot != map.end()) {
        drtv          = spot->second;
        active._space = drtv->acquire_space();
      }
    }
  }
  Feature value{FeatureView::Literal("")};
  if (active._space) {
    auto &&[range, payload] = *active._space->space.find(addr);
    active._row             = range.empty() ? nullptr : &payload;
    active._addr            = addr;
    active._drtv            = drtv;

    // Current active data.
    auto * store = Txb_IP_Space::ctx_active_info(ctx);
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

public:
  static constexpr TextView NAME{"ip-col"};

  Rv<ActiveType> validate(Config &cfg, Spec &spec, TextView const &arg) override;

  Feature extract(Context &ctx, Spec const &spec) override;

protected:
  static constexpr auto INVALID_IDX = Do_ip_space_define::INVALID_IDX;
  struct Info {
    unsigned _idx = INVALID_IDX; ///< Column index.
    TextView _arg;               ///< Argument name for use in remap / lazy lookup.
  };
};

Rv<ActiveType>
Ex_ip_col::validate(Config &cfg, Spec &spec, const TextView &arg)
{
  TextView parsed;
  if (arg.empty()) {
    return Errata(S_ERROR, R"("{}" extractor requires an argument to specify the column.)", NAME);
  }

  auto *mod_info = cfg.active_value<Mod_ip_space::CfgActiveInfo>(Mod_ip_space::KEY);
  if (!mod_info) {
    return Errata(S_ERROR, R"("{}" extractor can only be used with an active IP Space from the {} modifier.)", NAME, Mod_ip_space::KEY);
  }

  auto span       = cfg.allocate_cfg_storage(sizeof(Info)).rebind<Info>();
  spec._data.span = span;
  Info &info      = span[0];
  auto drtv       = mod_info->_drtv;
  // Always do the column conversion if it's an integer - that won't change at runtime.
  if (auto n = svtou(arg, &parsed); arg.size() == parsed.size()) {
    if (drtv && n >= drtv->_cols.size()) {
      return Errata(S_ERROR, R"(Invalid column index, {} of {} in space {}.)", n, drtv->_cols.size(), drtv->_name);
    }
    info._idx = n;
  } else if (drtv) { // otherwise if it's not remap, verify the column name and convert to index.
    auto idx = drtv->col_idx(arg);
    if (idx == INVALID_IDX) {
      return Errata(S_ERROR, R"(Invalid column argument, "{}" in space {} is not recognized as an index or name.)", arg, drtv->_name);
    }
    info._idx = idx;
  } else {
    info._arg = cfg.localize(arg);
    info._idx = INVALID_IDX;
    return {{NIL, STRING, INTEGER, IP_ADDR, TUPLE}};
  }

  ActiveType result_type = NIL;
  switch (drtv->_cols[info._idx]._type) {
  default:
    break; // shouldn't happen.
  case ColumnType::ADDRESS:
    result_type = IP_ADDR;
    break;
  case ColumnType::STRING:
    result_type = STRING;
    break;
  case ColumnType::INTEGER:
    result_type = INTEGER;
    break;
  case ColumnType::ENUM:
    result_type = STRING;
    break;
  case ColumnType::FLAGS:
    result_type = TUPLE;
    break;
  }
  return {{ NIL, result_type }}; // any column can return @c NIL if the address isn't found.
}

Feature
Ex_ip_col::extract(Context &ctx, const Spec &spec)
{
  // Get all the pieces needed.
  if ( auto ctx_ai = Txb_IP_Space::ctx_active_info(ctx) ; ctx_ai ) {
    auto info = spec._data.span.rebind<Info>().data(); // Extractor local storage.
    auto idx  = (info->_idx != INVALID_IDX ? info->_idx : ctx_ai->_drtv->col_idx(info->_arg));
    if (idx != INVALID_IDX) { // Column is valid.
      auto &col = ctx_ai->_drtv->_cols[idx];
      if (ctx_ai->_row) {
        auto data = col.data_in_row(ctx_ai->_row);
        switch (col._type) {
        default:
          break; // Shouldn't happen.
        case ColumnType::ADDRESS:
          return {ctx_ai->_addr};
        case ColumnType::STRING:
          return FeatureView::Literal(data.rebind<TextView>()[0]);
        case ColumnType::INTEGER:
          return {data.rebind<feature_type_for<INTEGER>>()[0]};
        case ColumnType::ENUM:
          return FeatureView::Literal(col._tags[data.rebind<unsigned>()[0]]);
        case ColumnType::FLAGS: {
          auto bits   = BitSpan(data);
          auto n_bits = bits.count();
          auto t      = ctx.alloc_span<Feature>(n_bits);
          for (unsigned k = 0, t_idx = 0; k < col._tags.count(); ++k) {
            if (bits[k]) {
              t[t_idx++] = FeatureView::Literal(col._tags[k]);
            }
          }
          return {t};
        }
        }
      }
    }
  }
  return NIL_FEATURE;
}

/* ------------------------------------------------------------------------------------ */

namespace
{
Ex_ip_col ex_ip_col;
[[maybe_unused]] bool INITIALIZED = []() -> bool {
  Config::define<Do_ip_space_define>();
  Modifier::define(Mod_ip_space::KEY, Mod_ip_space::load);
  Extractor::define(ex_ip_col.NAME, &ex_ip_col);
  return true;
}();
} // namespace
