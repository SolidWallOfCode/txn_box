/** @file
   IP Space support.

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
#include <swoc/Lexicon.h>

#include "txn_box/Directive.h"
#include "txn_box/Modifier.h"
#include "txn_box/Config.h"
#include "txn_box/Context.h"

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

/* ------------------------------------------------------------------------------------ */
/** Simple class to emulate @c std::bitset over a variable amount of memory.
 * @c std::bitset doesn't work because the bit set size is a compile time constant.
 * @c std::vector<bool> doesn't work either because it does separate memory allocation.
 * In contrast to these, this class allows mapping an arbitrary previously allocated chunk of
 * memory as a bit set. This enables fitting it in to a @c Row in the @c IPSpace payload where
 * the @c Row data is allocated in a single chunk.
 */
class BitSpan {
  using self_type = BitSpan; ///< Self reference type.
  static constexpr unsigned BITS = BITSPERBYTE; ///< # of bits per unit.

  /// Reference class to make the index operator work.
  /// An instance of this fronts for a particular bit in the bit set.
  struct bit_ref {
    /** Assign a @c bool to the bit.
     *
     * @param b Value to set.
     * @return @a this
     *
     * The bit is set if @a b is @c true, and reset if @a b is @c false.
     */
    bit_ref & operator = (bool b) { if (b) bits.set(idx); else bits.reset(idx); return *this; }

    /** Assign an @c int to the bit.
     *
     * @param v The integer to assign.
     * @return @a this
     *
     * The bit is set to zero if @a v is zero, otherwise it is set to 1.
     */
    bit_ref & operator = (int v) { return (*this) = (v != 0);}

    /** Allow bit to be used as a boolean.
     *
     * @return @c true if the bit is set, @c false if not.
     */
    explicit operator bool () { return (bits._span[idx/BITS] & (1 << (idx % BITS))) != 0;}

    BitSpan& bits; ///< Containing span.
    unsigned idx; ///< Bit index.
  };
public:
  /// Construct from chunk of memory.
  BitSpan(MemSpan<void> const& span) : _span(span.rebind<uint8_t>()) {}
  /// Construct from chunk of bytes.
  BitSpan(MemSpan<uint8_t> const& span) : _span(span) {}

  /** Set a bit
   *
   * @param idx Bit index.
   * @return @a this.
   */
  self_type & set(unsigned idx);

  /** Reset a bit.
   *
   * @param idx Bit index.
   * @return @a this.
   */
  self_type & reset(unsigned idx);

  /** Reset all bits.
   *
   * @return @a this.
   */
  self_type & reset();

  /** Access a single bit.
   *
   * @param idx Index of bit.
   * @return @c true if the bit is set, @c false if not.
   */
  bit_ref operator [](unsigned idx) { return { *this, idx }; }

  unsigned count() const {
    unsigned zret = 0;
    for ( unsigned idx = 0, N = _span.count() ; idx < N ; ++idx ) {
      zret += std::bitset<BITS>(_span[idx]).count();
    }
    return zret;
  }
protected:
  MemSpan<uint8_t> _span;
};

inline auto BitSpan::set(unsigned int idx) -> self_type & {
  _span[idx/BITS] |= (1 << (idx % BITS));
  return *this;
}

inline auto BitSpan::reset(unsigned int idx) -> self_type & {
  _span[idx/BITS] &= ~(1 << (idx % BITS));
  return *this;
}

inline auto BitSpan::reset() -> self_type & {
  memset(_span, 0);
  return *this;
}
/* ------------------------------------------------------------------------------------ */
/// Define an IP Space
class Do_ip_space_define : public Directive {
  using self_type = Do_ip_space_define; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Functor to do file content updating as needed.
  struct Updater {
    std::weak_ptr<Config> _cfg; ///< Configuration.
    Do_ip_space_define * _block; ///< Space instance.

    void operator()(); ///< Do the update check.
  };

  ~Do_ip_space_define(); ///< Destructor.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node key_value);

  /** Per config initialization.
   *
   * @param cfg Config.
   * @return Errors, if any.
   */
  static Errata cfg_init(Config& cfg);

protected:
  /// Configuration level map of defined spaces.
  using Map = std::unordered_map<TextView, self_type*, std::hash<std::string_view>>;

  /// Per configuration data.
  /// An instance of this is stored in the configuration arena.
  struct CfgStoreInfo {
    Map _map; ///< Map of defined spaces.
    self_type * _active = nullptr; ///< Active space.
  };

  /// A row in the space.
  using Row = MemSpan<std::byte>;
  /// IPSpace to store the rows.
  using Space = IPSpace<Row>;
  /// Space information that must be reloaded on file change.
  struct SpaceInfo {
    Space space; ///< IPSpace.
    swoc::MemArena arena; ///< Row storage.
  };
  using SpaceHandle = std::shared_ptr<SpaceInfo>;

  struct Column {
    /// Column data type.
    enum DataType {
      INVALID, ///< Invalid marker.
      RANGE, ///< Special marker for range column (column 0)
      STRING, ///< text.
      INTEGER, ///< integral value.
      ENUM, ///< enumeration.
      FLAGS ///< Set of flags.
    };
    TextView _name; ///< Name.
    unsigned _idx; ///< Index.
    DataType _type; ///< Column data type.
    swoc::Lexicon<int> _tags; ///< Tags for enumerations or flags.
    size_t _row_offset; ///< Offset in to @c Row for column data.
    size_t _row_size; ///< # of bytes of row storage.

    Column() = default; ///< Default constructor.
    Column(Column && that) = default; ///< Move constructor.

    MemSpan<std::byte> data_in_row(Row* row) {
      return { row->data() + _row_offset, _row_size};
    }

    /// Mapping between strings and @c DataType enumeration values.
    static const swoc::Lexicon<DataType> TypeNames;
  };

  TextView _name; ///< Block name.
  swoc::file::path _path; ///< Path to file (optional)
  SpaceHandle _space; ///< The IP Space
  std::shared_mutex _space_mutex; ///< Reader / writer for @a _space.

  std::vector<Column> _cols; ///< Defined columns.
  swoc::Lexicon<unsigned> _col_names; ///< Mapping of names <-> indices.
  static constexpr int INVALID_TAG = -1; ///< Lexicon default value for invalid tag.
  static constexpr int AUTO_TAG = -2; ///< Lexicon default value for auto adding tags (enum).
  size_t _row_size = 0; ///< Current row size.

  feature_type_for<DURATION> _duration; ///< Time between update checks.
  std::atomic<std::chrono::system_clock::duration> _last_check = std::chrono::system_clock::now().time_since_epoch(); ///< Absolute time of the last alert.
  std::chrono::system_clock::time_point _last_modified; ///< Last modified time of the file.
  ts::TaskHandle _task; ///< Handle for periodic checking task.

  int _line_no = 0; ///< For debugging name conflicts.

  static const std::string NAME_TAG;
  static const std::string PATH_TAG;
  static const std::string COLUMNS_TAG;
  static const std::string DURATION_TAG;
  static const std::string TYPE_TAG;
  static const std::string VALUES_TAG;

  SpaceHandle acquire_space() {
    std::shared_lock lock(_space_mutex);
    return _space;
  }

  /// Find the space for the directive instance with @a rtti
  static Map* map(Directive::CfgInfo const * rtti);
  /// Get the map of IP Space directives from the @a cfg.
  static Map* map(Config& cfg);

  static CfgStoreInfo * cfg_store_info(Config & cfg);

  /// Context information for the active IP Space.
  struct CtxAxctiveInfo {
    SpaceHandle _space; ///< Active space.
    IPAddr _addr; ///< Search address.
    Row * _row; ///< Active row.
  };
  /// Per context storage for scoped Space handle.
  static CtxAxctiveInfo * ctx_active_info(Context& ctx);

  Do_ip_space_define() = default; ///< Default constructor.

  /** Define a column in the space.
   *
   * @param cfg Config object.
   * @param node Roto node of the column definition.
   * @return Errors, if any.
   */
  Errata define_column(Config & cfg, YAML::Node node);

  Rv<SpaceHandle> parse_space(Config & cfg, TextView content);

  /// Check if it is time to do a modified check on the file content.
  bool should_check();

  friend class Mod_ip_space;
  friend class Ex_ip_col;
  friend Updater;
};

const std::string Do_ip_space_define::KEY{"ip-space-define" };
const std::string Do_ip_space_define::NAME_TAG{"name" };
const std::string Do_ip_space_define::PATH_TAG{"path" };
const std::string Do_ip_space_define::COLUMNS_TAG{ "columns" };
const std::string Do_ip_space_define::DURATION_TAG{"duration" };
const std::string Do_ip_space_define::TYPE_TAG{"type"};
const std::string Do_ip_space_define::VALUES_TAG{"values"};

const HookMask Do_ip_space_define::HOOKS{MaskFor(Hook::POST_LOAD)};

const swoc::Lexicon<Do_ip_space_define::Column::DataType> Do_ip_space_define::Column::TypeNames {
  {{ STRING, "string" }
        ,{ ENUM , "enum" }
        ,{ INTEGER, "integer"}
        ,{ FLAGS, "flags" }
        }
        , INVALID };

Do_ip_space_define::~Do_ip_space_define() noexcept {
  _task.cancel();
}

auto Do_ip_space_define::cfg_store_info(Config& cfg) -> CfgStoreInfo * {
  return cfg.drtv_info(KEY)->_cfg_store.rebind<CfgStoreInfo>().data();
}
auto Do_ip_space_define::map(Directive::CfgInfo const * rtti) -> Map* { return &rtti->_cfg_store.rebind<CfgStoreInfo>()[0]._map; }
auto Do_ip_space_define::map(Config& cfg) -> Map* {
  return &cfg.drtv_info(KEY)->_cfg_store.rebind<CfgStoreInfo>()[0]._map;
}

auto Do_ip_space_define::ctx_active_info(Context &ctx) -> CtxAxctiveInfo * {
  return ctx.storage_for(ctx.cfg().drtv_info(Do_ip_space_define::NAME_TAG)).rebind<CtxAxctiveInfo>().data();
}

bool Do_ip_space_define::should_check() {
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

Errata Do_ip_space_define::invoke(Context &ctx) {
  // Start update checking.
  if (_duration.count()) {
    _task = ts::PerformAsTaskEvery(Updater{ctx.acquire_cfg(), this}
                                   , std::chrono::duration_cast<std::chrono::milliseconds>(_duration)
    );
  }
  return {};
}

auto Do_ip_space_define::parse_space(Config& cfg, TextView content) -> Rv<SpaceHandle> {
  TextView line;
  unsigned line_no = 0;
  auto space = std::make_shared<SpaceInfo>();
  while (! (line = content.take_prefix_at('\n')).empty()) {
    ++line_no;
    line.trim_if(&isspace);
    if (line.empty() || '#' == line.front()) {
      continue;
    }
    auto token = line.take_prefix_at(',');
    IPRange range{token};
    if (range.empty()) {
      return Error(R"(Invalid range "{}" at line {}.)", token, line_no);
    }

    Row row = space->arena.alloc(_row_size).rebind<std::byte>();
    TextView parsed;
    // Iterate over the columns. If the input data runs out, then @a token becomes the empty
    // view, which the various cases deal with (in most an empty token isn't a problem).
    // This guarantees that every column in every row is initialized.
    for ( unsigned idx = 1 ; idx < _cols.size() ; ++idx ) {
      Column& c = _cols[idx];
      MemSpan<void> data{row.data() + c._row_offset, c._row_size};
      token = line.take_prefix_at(',').ltrim_if(&isspace);
      switch (c._type) {
        case Column::STRING:
          data.rebind<TextView>()[0] = cfg.localize(token);
          break;
        case Column::INTEGER: {
          if (token) {
            auto n = swoc::svtoi(token, &parsed);
            if (parsed.size() == token.size()) {
              data.rebind<feature_type_for<INTEGER>>()[0] = n;
            }
          } else {
            data.rebind<feature_type_for<INTEGER>>()[0] = 0;
          }
        }
          break;
        case Column::ENUM:
          if (auto tidx = c._tags[token] ; INVALID_TAG == tidx) {
            return Error(R"("{}" is not a valid tag for column {}{} at line {}.)", token, c._idx, bwf::Optional(R"( "{}")", c._name), line_no);
          } else {
            if (AUTO_TAG == tidx) {
              idx = c._tags.count();
              c._tags.define(idx, token);
            }
            data.rebind<feature_type_for<INTEGER>>()[0] = idx;
          }
          break;
        case Column::FLAGS: {
          TextView key;
          BitSpan bits { data };
          bits.reset(); // start with no bits set.
          while (! (key = token.take_prefix_if([](char c)->bool{return !('-' == c || '_' ==c || isalnum(c));})).empty()) {
            if (auto idx = c._tags[key] ; idx >= 0) {
              bits[idx] = true;
            } else {
              return Error(R"("{}" is not a valid tag for column {}{} at line {}.)", token, c._idx, bwf::Optional(R"( "{}")", c._name), line_no);
            }
          }
        }
      }
    }
    space->space.mark(range, row);
  }
  return space;
}

Errata Do_ip_space_define::define_column(Config & cfg, YAML::Node node) {
  Column col;
  auto name_node = node[NAME_TAG];
  if (name_node) {
    auto &&[name_expr, name_errata]{cfg.parse_expr(name_node)};
    if (!name_errata.is_ok()) {
      name_errata.info("While parsing {} key at {} in {} at {}.", NAME_TAG, node.Mark(), COLUMNS_TAG, node.Mark());
      return std::move(name_errata);
    }
    if (!name_expr.is_literal() || !name_expr.result_type().can_satisfy(STRING)) {
      return Error("{} value at {} for {} define at {} must be a literal string.", NAME_TAG, name_node.Mark(), COLUMNS_TAG, node.Mark());
    }
    col._name = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._expr));
  }

  auto type_node = node[TYPE_TAG];
  if (! type_node){
    return Error("{} at {} must have a {} key.", COLUMNS_TAG, node.Mark(), TYPE_TAG);
  }
  auto &&[type_expr, type_errata]{cfg.parse_expr(type_node)};
  if (!type_errata.is_ok()) {
    type_errata.info("While parsing {} key at {} in {} at {}.", TYPE_TAG, node.Mark(), COLUMNS_TAG, node.Mark());
    return std::move(type_errata);
  }
  if (!type_expr.is_literal() || !type_expr.result_type().can_satisfy(STRING)) {
    return Error("{} value at {} for {} define at {} must be a literal string.", NAME_TAG, name_node.Mark(), COLUMNS_TAG, node.Mark());
  }
  auto text = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(type_expr._expr));
  col._type= Column::TypeNames[text];
  if (col._type == Column::INVALID) {
    return Error(R"(Type "{}" at {] is not valid - must be one of {:s}.)", text, type_node.Mark(), Column::TypeNames);
  }

  // Need names if it's FLAGS. Names for ENUM are optional.
  if (Column::ENUM == col._type || Column::FLAGS == col._type) {
    auto tags_node = node[VALUES_TAG];
    if (! tags_node){
      if (Column::FLAGS == col._type) {
        return Error("{} at {} must have a {} key because it is of type {}.", COLUMNS_TAG, node.Mark(), VALUES_TAG, Column::TypeNames[Column::FLAGS]);
      }
      col._tags.set_default(AUTO_TAG);
    } else {
      // key value must be a string or a tuple of strings.
      auto &&[tags_expr, tags_errata]{cfg.parse_expr(tags_node)};
      if (!type_errata.is_ok()) {
        type_errata.info("While parsing {} key at {} in {} at {}.", VALUES_TAG, node.Mark(), COLUMNS_TAG, node.Mark());
        return std::move(type_errata);
      }
      if (! type_expr.is_literal()) {
        return Error("{} value at {} for {} define at {} must be a literal string or list of strings.", NAME_TAG, name_node.Mark(), COLUMNS_TAG, node.Mark());
      }
      col._tags.set_default(INVALID_TAG);
      Feature lit = std::get<Expr::LITERAL>(type_expr._expr);
      if (ValueTypeOf(lit) == TUPLE) {
        for ( auto f : std::get<IndexFor(TUPLE)>(lit)) {
          if (ValueTypeOf(f) != STRING) {
            return Error("{} value at {} for {} define at {} must be a literal string or list of strings.", NAME_TAG, name_node.Mark(), COLUMNS_TAG, node.Mark());
          }
          col._tags.define(col._tags.count(), std::get<IndexFor(STRING)>(f));
        }
      } else if (ValueTypeOf(lit) == STRING ) {
        col._tags.define(col._tags.count(), std::get<IndexFor(STRING)>(lit));
      } else {
        return Error("{} value at {} for {} define at {} must be a literal string or list of strings.", NAME_TAG, name_node.Mark(), COLUMNS_TAG, node.Mark());
      }
    }
  }
  col._idx = _cols.size();
  col._row_offset = _row_size;
  switch (col._type) {
    case Column::ENUM:
    case Column::FLAGS:
    case Column::INTEGER: col._row_size = sizeof(feature_type_for<INTEGER>); break;
    case Column::STRING: col._row_size = sizeof(TextView); break;
  }
  _row_size += col._row_size;
  _cols.emplace_back(std::move(col));
  _col_names.define(col._idx, col._name);
  return {};
}

Rv<Directive::Handle> Do_ip_space_define::load(Config& cfg, YAML::Node drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node key_value) {
  auto self = new self_type();
  Handle handle(self);
  self->_line_no = drtv_node.Mark().line;

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
  if (! path_node) {
    return Error("{} directive at {} must have a {} key.", KEY, drtv_node.Mark(), PATH_TAG);
  }

  auto &&[path_expr, path_errata]{cfg.parse_expr(path_node)};
  if (!path_errata.is_ok()) {
    path_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
    return std::move(path_errata);
  }
  if (!path_expr.is_literal()) {
    return Error("{} value at {} for {} directive at {} must be a literal string.", PATH_TAG, path_node.Mark(), KEY, drtv_node.Mark());
  }
  drtv_node.remove(path_node);
  self->_path = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(path_expr._expr));
  ts::make_absolute(self->_path);

  auto dur_node = key_value[DURATION_TAG];
  if (dur_node) {
    auto &&[dur_expr, dur_errata] = cfg.parse_expr(dur_node);
    if (! dur_errata.is_ok()) {
      dur_errata.info("While parsing {} directive at {}.", KEY, drtv_node.Mark());
      return std::move(dur_errata);
    }
    if (! dur_expr.is_literal() || ! dur_expr.result_type().can_satisfy(DURATION)) {
      return Error("{} value at {} for {} directive at {} must be a literal duration.", DURATION_TAG, dur_node.Mark(), KEY, drtv_node.Mark());
    }
    drtv_node.remove(dur_node);
    self->_duration = std::get<IndexFor(DURATION)>(std::get<Expr::LITERAL>(dur_expr._expr));
  }

  auto cols_node = key_value[COLUMNS_TAG];
  // To simplify indexing, put in a "range" column as index 0, so config indices and internal
  // indices match up.
  self->_cols.emplace_back(Column{});
  self->_cols[0]._name = "range";
  self->_cols[0]._idx = 0;
  self->_cols[0]._type = Column::RANGE;
  self->_col_names.define(self->_cols[0]._idx, self->_cols[0]._name);

  if (cols_node) {
    if (cols_node.IsMap()) {
      auto errata = self->define_column(cfg, cols_node);
      if (!errata.is_ok()) {
        errata.info(R"(While parsing "{}" key at {}.)", COLUMNS_TAG, cols_node.Mark());
        return std::move(errata);
      }
    } else if (cols_node.IsSequence()) {
      for ( auto child : cols_node ) {
        auto errata = self->define_column(cfg, child);
        if (!errata.is_ok()) {
          errata.info(R"(While parsing "{}" key at {}.)", COLUMNS_TAG, cols_node.Mark());
          return std::move(errata);
        }
      }
    } else {
      return Error(R"("{}" at {} must be an object or a list of objects.)", COLUMNS_TAG, cols_node.Mark());
    }
  }

  std::error_code ec;
  auto content = swoc::file::load(self->_path, ec);
  self->_last_modified = swoc::file::modify_time(swoc::file::status(self->_path, ec));
  auto && [ space_info, space_errata ] = self->parse_space(cfg, content);
  if (! space_errata.is_ok()) {
    space_errata.info(R"(While parsing IPSpace file "{}" in space "{}".)", self->_path, self->_name);
  }
  self->_space = space_info;

  // Put the directive in the map.
  Map* map = self_type::map(cfg.drtv_info());
  if (auto spot = map->find(self->_name) ; spot != map->end()) {
    return Error(R"("{}" directive at {} has the same name "{}" as another instance at line {}.)"
    , KEY, drtv_node.Mark(), self->_name, spot->second->_line_no);
  }
  (*map)[self->_name] = self;

  return std::move(handle);
}

Errata Do_ip_space_define::cfg_init(Config &cfg) {
  // Note - @a h gets a pointer to the Handle.
  auto h = cfg.allocate_cfg_storage(sizeof(CfgStoreInfo)).rebind<CfgStoreInfo>().data();
  new (h) CfgStoreInfo();
  cfg.mark_for_cleanup(h); // takes a pointer to the object to clean up.
  // Scoped access to defined space in a @c Context.
  // Only one space can be active at a time therefore this can be shared among the instances in
  // a single @c Context.
  cfg.reserve_ctx_storage(sizeof(CtxAxctiveInfo));
  return {};
}

void Do_ip_space_define::Updater::operator()() {
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
    std::string content = swoc::file::load(_block->_path, ec);
    if (!ec) { // swap in updated content.
      auto && [ space, errata ] { _block->parse_space(*cfg, content) };
      if (errata.is_ok()) {
        std::unique_lock lock(_block->_space_mutex);
        _block->_space = space;
      }
      _block->_last_modified = mtime;
      return;
    }
  }
}

/* ------------------------------------------------------------------------------------ */
/// IPSpace modifier
class Mod_ip_space : public Modifier {
  using self_type = Mod_ip_space; ///< Self reference type.
  using super_type = Modifier; ///< Parent type.

  using CtxActiveInfo = Do_ip_space_define::CtxAxctiveInfo;
public:
  Mod_ip_space(Expr && expr, TextView const& view, Do_ip_space_define *drtv);

  static const std::string KEY;

  /** Modify the feature.
   *
   * @param ctx Run time context.
   * @param feature Feature to modify [in,out]
   * @return Errors, if any.
   */
  Rv<Feature> operator()(Context& ctx, feature_type_for<IP_ADDR> feature) override;

  /** Check if @a ftype is a valid type to be modified.
   *
   * @param ex_type Type of feature to modify.
   * @return @c true if this modifier can modity that feature type, @c false if not.
   */
  bool is_valid_for(ActiveType const& ex_type) const override;

  /// Resulting type of feature after modifying.
  ActiveType result_type(ActiveType const&) const override;

  /** Create an instance from YAML config.
   *
   * @param cfg Configuration state object.
   * @param mod_node Node with modifier.
   * @param key_node Node in @a mod_node that identifies the modifier.
   * @return A constructed instance or errors.
   */
  static Rv<Handle> load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value);

protected:
  Expr _expr; ///< Value expression.
  TextView _name; ///< Argument - IPSpace name.
  Do_ip_space_define* _drtv = nullptr; ///< The IPSpace define for @a _name
};

const std::string Mod_ip_space::KEY{"ip-space"};

Mod_ip_space::Mod_ip_space(Expr && expr, TextView const& name, Do_ip_space_define *drtv)
  : _expr(std::move(expr)), _name(name), _drtv(drtv) {}

bool Mod_ip_space::is_valid_for(ActiveType const& ex_type) const {
  return ex_type.can_satisfy(IP_ADDR);
}

ActiveType Mod_ip_space::result_type(const ActiveType &) const {
  return { NIL, STRING, INTEGER, ActiveType::TuplesOf(STRING) };
}

Rv<Modifier::Handle> Mod_ip_space::load(Config &cfg, YAML::Node node, TextView key, TextView arg, YAML::Node key_value) {
  auto csi = Do_ip_space_define::cfg_store_info(cfg);
  auto & map = csi->_map;
  auto spot = map.find(arg);
  if (spot == map.end()) {
    return Error(R"("{}" at {} is not the name of a defined IP space.)", arg, node.Mark());
  }
  let scope(csi->_active, spot->second);
  // Now parse the expression with the active IPSpace.
  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" modifier at {}.)", KEY, key_value.Mark());
    return std::move(errata);
  }
  return Handle(new self_type{std::move(expr), arg, static_cast<Do_ip_space_define*>(spot->second)});
};

Rv<Feature> Mod_ip_space::operator()(Context& ctx, feature_type_for<IP_ADDR> addr) {
  // Need to do better at some point?
  // All of this sets up data in the context for the extractors to pull from the correct row
  // in the correct space.
  std::array<std::byte, sizeof(CtxActiveInfo)> save;
  auto span = ctx.storage_for(_drtv);
  memcpy(save.data(), span.data(), sizeof(save)); // save old state.
  new (span.data()) CtxActiveInfo; // construct current state to know default.
  auto & ctx_ai = span.rebind<CtxActiveInfo>()[0];
  ctx_ai._space = _drtv->acquire_space();
  auto && [ range, payload ] = *ctx_ai._space->space.find(addr);
  ctx_ai._row = range.empty() ? nullptr : &payload;
  ctx_ai._addr = addr;

  // Environment is ready, do the extraction.
  auto value = ctx.extract(_expr);

  // shut it down and restore.
  std::destroy_at(&ctx_ai);
  memcpy(span.data(), save.data(), sizeof(save)); // restore previous state.

  return value;
}

/* ------------------------------------------------------------------------------------ */
/// IP Space extractor.
class Ex_ip_col : public Extractor {
  using self_type = Ex_ip_col; ///< Self reference type.
  using super_type = Extractor; ///< Parent type.

  using CtxActiveInfo = Do_ip_space_define::CtxAxctiveInfo;
public:
  static constexpr TextView NAME{"ip-col"};

  Rv<ActiveType> validate(Config& cfg, Spec& spec, TextView const& arg) override;

  Feature extract(Context & ctx, Spec const& spec) override;
  BufferWriter& format(BufferWriter& w, Spec const& spec, Context& ctx) override;
protected:
  struct Info {
    Do_ip_space_define * _drtv; ///< Source of IPSpace data.
    unsigned _idx; ///< Column index.
  };
};

Rv<ActiveType> Ex_ip_col::validate(Config &cfg, Spec &spec, const TextView &arg) {
  TextView parsed;
  if (arg.empty()) {
    return Error(R"("{}" extractor requires an argument to specify the column.)", NAME);
  }
  auto csi = Do_ip_space_define::cfg_store_info(cfg);
  if (! csi->_active) {
    return Error(R"("{}" extractor can only be used with an active IP Space.)", NAME);
  }

  auto span = cfg.allocate_cfg_storage(sizeof(Info)).rebind<Info>();
  spec._data = span;
  Info & info = span[0];
  info._drtv = csi->_active;
  auto & cols = csi->_active->_cols;
  if ( auto n = svtoi(arg, &parsed) ; arg.size() == parsed.size()) {
    if (n >= cols.size()) {
      return Error(R"(Invalid column index, {} of {} in space {}.)", n, cols.size(), csi->_active->_name);
    }
    info._idx = n;
  } else if ( auto spot = std::find_if(cols.begin(), cols.end(), [&](Do_ip_space_define::Column& c){ return 0 == strcasecmp(c._name, arg); }) ; spot != cols.end()) {
    info._idx = spot - cols.begin();
  } else {
    return Error(R"(Invalid column argument, "{}" in space {} is not recognized as an index or name.)", arg, csi->_active->_name);
  }
  ValueType result_type = NIL;
  switch (cols[info._idx]._type) {
    case Do_ip_space_define::Column::STRING: result_type = STRING;
    case Do_ip_space_define::Column::INTEGER: result_type = INTEGER;
    case Do_ip_space_define::Column::ENUM: result_type = STRING;
    case Do_ip_space_define::Column::FLAGS: result_type = TUPLE;
  }
  return { result_type };
}

Feature Ex_ip_col::extract(Context &ctx, const Spec &spec) {
  // Get all the pieces needed.
  auto info = spec._data.rebind<Info>().data();
  auto & col = info->_drtv->_cols[info->_idx];
  auto & ctx_ai = ctx.storage_for(info->_drtv).rebind<CtxActiveInfo>()[0];
  auto data = col.data_in_row(ctx_ai._row);
  switch (col._type) {
    case Do_ip_space_define::Column::STRING:
      return FeatureView::Literal(data.rebind<TextView>()[0]);
    case Do_ip_space_define::Column::INTEGER:
      return { data.rebind<feature_type_for<INTEGER>>()[0] };
    case Do_ip_space_define::Column::ENUM:
      return FeatureView::Literal(col._tags[data.rebind<unsigned>()[0]]);
    case Do_ip_space_define::Column::FLAGS: {
      auto t = ctx.span<feature_type_for<TUPLE>>(1)[0];
      auto bits = BitSpan(data);
      auto n_bits = bits.count();
      t = ctx.span<Feature>(n_bits);
      for ( unsigned idx = 0, t_idx = 0 ; idx < col._tags.count() ; ++idx ) {
        if (bits[idx]) {
          t[t_idx++] = FeatureView::Literal(col._tags[idx]);
        }
      }
      return { t };
    }
  }
  return NIL_FEATURE;
}

BufferWriter& Ex_ip_col::format(BufferWriter &w, Spec const &spec, Context &ctx) {
  return bwformat(w, spec, this->extract(ctx, spec));
}

/* ------------------------------------------------------------------------------------ */

namespace {
Ex_ip_col ex_ip_col;
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Config::define<Do_ip_space_define>();
  Modifier::define(Mod_ip_space::KEY, Mod_ip_space::load);
  Extractor::define(ex_ip_col.NAME, &ex_ip_col);
  return true;
} ();
} // namespace
