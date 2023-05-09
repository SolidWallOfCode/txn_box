/** @file
   IP Space support.

 * Copyright 2020 Verizon Media
 * SPDX-License-Identifier: Apache-2.0
*/

#include <swoc/bwf_base.h>
#include <swoc/bwf_ex.h>

#include "txn_box/Config.h"
#include "txn_box/Expr.h"
#include "txn_box/table_util.h"

using namespace swoc::literals;
namespace bwf = swoc::bwf;
using swoc::Errata;
using swoc::TextView;

/* ------------------------------------------------------------------------------------ */
namespace txb {

unsigned
BitSpan::count() const
{
  unsigned zret = 0;
  for (unsigned idx = 0, N = _span.size(); idx < N; ++idx) {
    zret += std::bitset<BITS>(_span[idx]).count();
  }
  return zret;
}

// --
class ExternalFile::Ex_Name : public Extractor {
  using self_type = Ex_Name;
public:
  static const inline TextView KEY = "name";
  swoc::Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const &) override;
  Feature extract(Context &ctx, Spec const &) override;
} ex_table_name;

swoc::Rv<ActiveType>
ExternalFile::Ex_Name::validate(Config &, Extractor::Spec &, const TextView &)
{ return { { NIL, STRING } }; }

Feature
ExternalFile::Ex_Name::extract(Context &ctx, const Extractor::Spec &)
{
  if ( auto data = InvocationData::instance(ctx) ; data ) {
    return { data->_file_obj->_name };
  }
  return NIL_FEATURE;
}

// --

class ExternalFile::Ex_Path : public Extractor {
  using self_type = Ex_Path;
public:
  static const inline TextView KEY = "path";
  virtual swoc::Rv<ActiveType> validate(Config &, Spec &, swoc::TextView const &);
  virtual Feature extract(Context &ctx, Spec const &);
} ex_table_path;

swoc::Rv<ActiveType>
ExternalFile::Ex_Path::validate(Config &, Extractor::Spec &, const TextView &)
{ return { { NIL, STRING } }; }

Feature
ExternalFile::Ex_Path::extract(Context &ctx, const Extractor::Spec &)
{
  if (auto data = InvocationData::instance(ctx); data) {
    return {data->_file_obj->_path.view()};
  }
  return NIL_FEATURE;
}

// --

Extractor::Table ExternalFile::_local_binding = {
  {ExternalFile::Ex_Name::KEY, &ex_table_name}
, {ExternalFile::Ex_Path::KEY, &ex_table_path}
};

auto
ExternalFile::InvocationData::instance(Context & ctx) -> self_type * {
  return *ctx.named_object<self_type*>(KEY);
};

bool
ExternalFile::is_time_to_check()
{
  using Clock = std::chrono::system_clock;
  bool zret   = false;

  auto last = _last_check.load(); // required because CAS needs lvalue reference.
  auto now  = Clock::now();       // Current time_point.
  if (Clock::time_point(last) + _duration <= now) {
    // it's been long enough, swap out our time for the last time. The winner of this swap
    // does the actual alert, leaving its current time as the last alert time.
    zret = _last_check.compare_exchange_strong(last, now.time_since_epoch());
  }
  return zret;
}

void
ExternalFile::Updater::operator()()
{
  Errata errata;
  auto cfg = _cfg.lock(); // Make sure the config is still around while work is done.

  if (!cfg) {
    return;
  }

  if (!_file->is_time_to_check()) {
    return; // not time yet.
  }

  // Used to invoke directives.
  auto invoker = [&](Directive::Handle const& drtv) {
    if (drtv) {
      char buffer[8192];
      Context ctx(cfg, swoc::MemSpan<void>(buffer, sizeof(buffer)));
      auto hook_scope = ctx.push_current_hook(Hook::TASK);
      auto ex_scope = cfg->push_extractor_binding(&_local_binding);
      InvocationData ex_data(ctx, _file, errata);
      ctx.obtain_named_object<InvocationData *>(InvocationData::KEY, &ex_data);

      drtv->invoke(ctx);
    }
  };

  std::error_code ec;
  auto fs = swoc::file::status(_file->_path, ec);
  if (!ec) {
    auto mtime = swoc::file::last_write_time(fs);
    if (mtime <= _file->_last_modified) {
      return; // same as it ever was...
    }
    std::string content = swoc::file::load(_file->_path, ec);
    if (!ec) { // swap in updated content.
      _file->_last_modified = mtime;
      if (_file->_update_cb) {
        errata = _file->_update_cb(*cfg, content);
      }
      if (errata.is_ok()) {
        invoker(_file->_on_update);
        return;
      }
    }
  }
  if (_file->_error_cb) {
    errata = _file->_error_cb(*cfg, ec);
  }
  invoker(_file->_on_error);
}

Errata
ExternalFile::load(Config &cfg, YAML::Node base)
{
  if (auto path_node = base[PATH_TAG]; path_node) {
    auto &&[path_expr, path_errata]{cfg.parse_expr(path_node)};
    if (!path_errata.is_ok()) {
      return std::move(path_errata.note("In {} key at {}.", PATH_TAG, base.Mark()));
    }
    if (!path_expr.is_literal()) {
      return Errata(S_ERROR, "{} value at {} .", PATH_TAG, path_node.Mark());
    }
    base.remove(path_node);
    _path = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(path_expr._raw));
    ts::make_absolute(_path);
  } else {
    return Errata(S_ERROR, "Missing required {} key.", PATH_TAG);
  }

  if (auto n = base[DURATION_TAG]; n) {
    auto &&[expr, errata] = cfg.parse_expr(n);
    if (!errata.is_ok()) {
      return std::move(errata);
    }
    if (!expr.is_literal()) {
      return Errata(S_ERROR, "{} value at {} isn't a literal duration as required.", DURATION_TAG, n.Mark());
    }
    auto &&[value, value_errata]{std::get<Expr::LITERAL>(expr._raw).as_duration()};
    if (!value_errata.is_ok()) {
      value_errata.note("{} value at {} is not a valid duration.", DURATION_TAG, n.Mark());
      return std::move(value_errata);
    }
    _duration = value;
    base.remove(n);
  }

  if (auto n = base[ON_UPDATE_TAG]) {
    if (auto do_n = n[Directive::DO_KEY]; do_n) {
      let ex_scope = cfg.push_extractor_binding(&_local_binding);
      if (auto && [ handle, errata ] = cfg.parse_directive(n, Hook::TASK); !errata.is_ok()) {
        errata.note(S_ERROR, R"(While parsing "{}" tag for "{}" key at {})", Directive::DO_KEY, ON_UPDATE_TAG, n.Mark());
        return std::move(errata);
      } else {
        _on_update = std::move(handle);
      }
    }
    base.remove(ON_UPDATE_TAG);
  }

  if (auto n = base[ON_ERROR_TAG]) {
    if (auto do_n = n[Directive::DO_KEY]; do_n) {
      let ex_scope = cfg.push_extractor_binding(&_local_binding);
      if (auto && [ handle, errata ] = cfg.parse_directive(n, Hook::TASK); !errata.is_ok()) {
        errata.note(S_ERROR, R"(While parsing "{}" tag for "{}" key at {})", Directive::DO_KEY, ON_ERROR_TAG, n.Mark());
        return std::move(errata);
      } else {
        _on_error = std::move(handle);
      }
    }
    base.remove(ON_ERROR_TAG);
  }

  return {};
}

swoc::Errata
ExternalFile::run(Context & ctx)
{
  auto cfg = ctx.acquire_cfg();
  Updater{cfg, this}(); // Always load the first time.
  if (_duration.count()) { // don't check if there's no check duration.
    _task =
      ts::PerformAsTaskEvery(Updater{cfg, this}, std::chrono::duration_cast<std::chrono::milliseconds>(_duration));
  }

  return {};
}

namespace table {

const swoc::Lexicon<ColumnType> txb::table::Column::TypeNames{
    {{ColumnType::STRING, "string"}, {ColumnType::ENUM, "enum"}, {ColumnType::INTEGER, "integer"}, {ColumnType::FLAGS, "flags"}},
    ColumnType::INVALID};

Errata Base::parse_name(Config & cfg, YAML::Node node)
{
  if (auto n = node[NAME_TAG]; n) {
    auto &&[name_expr, name_errata]{cfg.parse_expr(n)};
    if (!name_errata.is_ok()) {
      return std::move(name_errata);
    }

    if (!name_expr.is_literal() || !name_expr.result_type().can_satisfy(STRING)) {
      return {S_ERROR, "{} value at {} is not a literal string as required.", NAME_TAG, n.Mark()};
    }

    node.remove(NAME_TAG);
    _name = cfg.localize(swoc::TextView{std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._raw))});
  } else {
    return {S_ERROR, "Missing required {} key.", NAME_TAG};
  }
  return {};
}

Errata
Base::parse_column_definitions(Config &cfg, YAML::Node node)
{
  Column col;
  auto name_node = node[NAME_TAG];
  if (name_node) {
    auto &&[name_expr, name_errata]{cfg.parse_expr(name_node)};
    if (!name_errata.is_ok()) {
      name_errata.note("While parsing {} key at {} in {} at {}.", NAME_TAG, node.Mark(), COLUMNS_TAG, node.Mark());
      return std::move(name_errata);
    }
    if (!name_expr.is_literal() || !name_expr.result_type().can_satisfy(STRING)) {
      return {S_ERROR,    "{} value at {} for {} define at {} must be a literal string.", NAME_TAG, name_node.Mark(), COLUMNS_TAG,
              node.Mark()};
    }
    col._name = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(name_expr._raw));
  }

  auto type_node = node[TYPE_TAG];
  if (!type_node) {
    return {S_ERROR, "{} at {} must have a {} key.", COLUMNS_TAG, node.Mark(), TYPE_TAG};
  }
  auto &&[type_expr, type_errata]{cfg.parse_expr(type_node)};
  if (!type_errata.is_ok()) {
    type_errata.note("While parsing {} key at {} in {} at {}.", TYPE_TAG, node.Mark(), COLUMNS_TAG, node.Mark());
    return std::move(type_errata);
  }
  if (!type_expr.is_literal() || !type_expr.result_type().can_satisfy(STRING)) {
    return {S_ERROR,    "{} value at {} for {} define at {} must be a literal string.", NAME_TAG, name_node.Mark(), COLUMNS_TAG,
            node.Mark()};
  }
  auto text = std::get<IndexFor(STRING)>(std::get<Expr::LITERAL>(type_expr._raw));
  col._type = Column::TypeNames[text];
  if (col._type == ColumnType::INVALID) {
    return {S_ERROR, R"(Type "{}" at {] is not valid - must be one of {:s}.)", text, type_node.Mark(), Column::TypeNames};
  }

  // Need names if it's FLAGS. Names for ENUM are optional.
  if (ColumnType::ENUM == col._type || ColumnType::FLAGS == col._type) {
    auto tags_node = node[VALUES_TAG];
    if (!tags_node) {
      if (ColumnType::FLAGS == col._type) {
        return {S_ERROR,     "{} at {} must have a {} key because it is of type {}.",
                COLUMNS_TAG, node.Mark(),
                VALUES_TAG,  Column::TypeNames[ColumnType::FLAGS]};
      }
      col._tags.set_default(AUTO_TAG);
    } else {
      // key value must be a string or a tuple of strings.
      auto &&[tags_expr, tags_errata]{cfg.parse_expr(tags_node)};
      if (!tags_errata.is_ok()) {
        tags_errata.note("While parsing {} key at {} in {} at {}.", VALUES_TAG, tags_node.Mark(), COLUMNS_TAG, node.Mark());
        return std::move(type_errata);
      }
      if (!tags_expr.is_literal()) {
        return {S_ERROR,     "{} value at {} for {} define at {} must be a literal string or list of strings.",
                NAME_TAG,    tags_node.Mark(),
                COLUMNS_TAG, node.Mark()};
      }
      col._tags.set_default(INVALID_TAG);
      Feature lit = std::get<Expr::LITERAL>(tags_expr._raw);
      if (lit.value_type() == TUPLE) {
        for (auto f : std::get<IndexFor(TUPLE)>(lit)) {
          if (f.value_type() != STRING) {
            return {S_ERROR,     "{} value at {} for {} define at {} must be a literal string or list of strings.",
                    NAME_TAG,    name_node.Mark(),
                    COLUMNS_TAG, node.Mark()};
          }
          col._tags.define(col._tags.count(), std::get<IndexFor(STRING)>(f));
        }
      } else if (lit.value_type() == STRING) {
        col._tags.define(col._tags.count(), std::get<IndexFor(STRING)>(lit));
      } else {
        return {S_ERROR,     "{} value at {} for {} define at {} must be a literal string or list of strings.",
                NAME_TAG,    name_node.Mark(),
                COLUMNS_TAG, node.Mark()};
      }
    }
  }
  col._idx        = _cols.size();
  col._row_offset = _row_size;
  switch (col._type) {
  default:
    break; // shouldn't happen.
  case ColumnType::ENUM:
  case ColumnType::FLAGS:
  case ColumnType::INTEGER:
    col._row_size = sizeof(feature_type_for<INTEGER>);
    break;
  case ColumnType::STRING:
    col._row_size = sizeof(TextView);
    break;
  }
  _row_size += col._row_size;
  _cols.emplace_back(std::move(col));
  return {};
}

Errata
Base::parse_row(Config &cfg, Row row, TextView line, int line_no)
{
  TextView parsed;
  // Iterate over the columns. If the input data runs out, then @a token becomes the empty
  // view, which the various cases deal with (in most an empty token isn't a problem).
  // This guarantees that every column in every row is initialized.
  for (unsigned col_idx = 1; col_idx < _cols.size(); ++col_idx) {
    Column &c = _cols[col_idx];
    MemSpan<void> data{row.data() + c._row_offset, c._row_size};
    auto token = line.take_prefix_at(',').ltrim_if(&isspace);
    switch (c._type) {
    default:
      break; // Shouldn't ever happen.
    case ColumnType::STRING:
      data.rebind<TextView>()[0] = cfg.localize(token);
      break;
    case ColumnType::INTEGER: {
      if (token) {
        auto n = swoc::svtoi(token, &parsed);
        if (parsed.size() == token.size()) {
          data.rebind<feature_type_for<INTEGER>>()[0] = n;
        }
      } else {
        data.rebind<feature_type_for<INTEGER>>()[0] = 0;
      }
    } break;
    case ColumnType::ENUM:
      if (auto idx = c._tags[token]; INVALID_TAG == idx) {
        return {
          S_ERROR, R"("{}" is not a valid tag for column {}{} at line {}.)", token, c._idx, bwf::Optional(R"( "{}")", c._name),
          line_no};
      } else {
        if (AUTO_TAG == idx) {
          idx = c._tags.count();
          c._tags.define(idx, token);
        }
        data.rebind<feature_type_for<INTEGER>>()[0] = idx;
      }
      break;
    case ColumnType::FLAGS: {
      TextView key;
      txb::BitSpan bits{data};
      bits.reset(); // start with no bits set.
      while (!(key = token.take_prefix_if([](char c) -> bool { return !('-' == c || '_' == c || isalnum(c)); })).empty()) {
        if (auto idx = c._tags[key]; idx >= 0) {
          bits[idx] = true;
        } else {
          return {
            S_ERROR, R"("{}" is not a valid tag for column {}{} at line {}".)", key, c._idx, bwf::Optional(R"( "{}")", c._name),
            line_no};
        }
      }
    }
    }
  }
  return {};
}

unsigned
Base::col_idx(swoc::TextView name)
{
  if (auto spot =
        std::find_if(_cols.begin(), _cols.end(), [&](Column &c) { return 0 == strcasecmp(c._name, name); });
      spot != _cols.end()) {
    return spot - _cols.begin();
  }

  return INVALID_IDX;
}

auto
CfgInfo::make(Config &cfg, swoc::TextView key, size_t reserve) -> self_type *
{
  auto cfg_info = cfg.obtain_named_object<self_type>(key);
  if (reserve) {
    cfg_info->_reserved_span = cfg.reserve_ctx_storage(reserve);
  }
  cfg.mark_for_cleanup(cfg_info);
  return cfg_info;
}

swoc::Errata CfgInfo::register_drtv(Base *instance)
{
  if (auto spot = _map.find(instance->_name) ; spot == _map.end()) {
    _map[instance->_name] = instance;
  } else {
    return swoc::Errata(S_ERROR, "Table name \"{}\" already in use on line {}", instance->_name, spot->second->_line_no);
  }
  return {};
}

Errata
Base::ExInfo::init(Config &cfg, Base *base, swoc::TextView arg)
{
  swoc::TextView parsed;
  if (auto n = svtou(arg, &parsed); arg.size() == parsed.size()) {
    if (base && n >= base->_cols.size()) {
      return Errata(S_ERROR, R"(Invalid column index, {} of {} in table {}.)", n, base->_cols.size(), base->_name);
    }
    _idx = n;
  } else if (base) { // otherwise if it's not remap, verify the column name and convert to index.
    auto idx = base->col_idx(arg);
    if (idx == INVALID_IDX) {
      return Errata(S_ERROR, R"(Invalid column argument, "{}" in table {} is not recognized as an index or name.)", arg, base->_name);
    }
    _idx = idx;
  } else {
    _arg = cfg.localize(arg);
    _idx = INVALID_IDX;
  }
  return {};
}

Feature
Base::ExInfo::extract(Context &ctx, Base::CtxScopeInfo *info, Row row, const bwf::Spec &)
{
  auto drtv = info->drtv();
  auto idx  = (_idx != INVALID_IDX ? _idx : drtv->col_idx(_arg));
  if (idx != INVALID_IDX) { // Column is valid.
    auto &col = drtv->_cols[idx];
    if (row) {
      auto data = col.data_in_row(row);
      switch (col._type) {
      default:
        break; // Shouldn't happen.
      case ColumnType::KEY:
        return info->key_as_feature(ctx);
      case ColumnType::STRING:
        return FeatureView::Literal(data.rebind<TextView>()[0]);
      case ColumnType::INTEGER:
        return {data.rebind<feature_type_for<INTEGER>>()[0]};
      case ColumnType::ENUM:
        return FeatureView::Literal(col._tags[data.rebind<unsigned>()[0]]);
      case ColumnType::FLAGS: {
        auto bits   = txb::BitSpan(data);
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
  return NIL_FEATURE;
}
}}// namespace txb::table
