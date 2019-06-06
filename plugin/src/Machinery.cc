/* 
   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
   
*/

#include <swoc/TextView.h>
#include <swoc/Errata.h>

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

const std::string Directive::DO_KEY { "do" };
/* ------------------------------------------------------------------------------------ */
Rv<Directive::Handle> Directive::load(Config & cfg, YAML::Node drtv_node) {
  YAML::Node key_node;
  for ( auto const&  [ key_name, key_value ] : drtv_node ) {
    TextView key { key_name.Scalar() };
    // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
    // if there is ever more than one.
    if (key == DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to directives. First key that is in the factory determines the directive type.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      return spot->second(cfg, drtv_node, key_value);
    }
  }
  return { {}, Errata().error(R"(Directive at {} has no recognized tag.)", drtv_node.Mark()) };
}

/* ------------------------------------------------------------------------------------ */
class Do_set_preq_url_host : public Directive {
  using super_type = Directive;
  using self_type = Do_set_preq_url_host;
public:
  static const std::string KEY;

  explicit Do_set_preq_url_host(TextView text) : _host(text) {}

  Errata invoke(Context &ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);
  std::string _host;
};

const std::string Do_set_preq_url_host::KEY { "set-preq-url-host" };

Errata Do_set_preq_url_host::invoke(Context &ctx) {
  Errata zret;
  return zret;
}

swoc::Rv<Directive::Handle> Do_set_preq_url_host::load(Config &, YAML::Node, YAML::Node key_node) {
  return { Handle{new self_type{key_node.Scalar()}}, {} };
}

/* ------------------------------------------------------------------------------------ */
class Do_set_preq_host : public Directive {
  using super_type = Directive;
  using self_type = Do_set_preq_host;
public:
  static const std::string KEY;

  explicit Do_set_preq_host(TextView text);

  Errata invoke(Context &ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);
  std::string _host;
};

const std::string Do_set_preq_host::KEY { "set-preq-host" };

Do_set_preq_host::Do_set_preq_host(TextView text) : _host(text) {}

Errata Do_set_preq_host::invoke(Context &ctx) {
  Errata zret;
  return zret;
}

swoc::Rv<Directive::Handle> Do_set_preq_host::load(Config &, YAML::Node, YAML::Node key_node) {
  return { Handle{new self_type{key_node.Scalar()}}, {} };
}

/* ------------------------------------------------------------------------------------ */
class Do_set_preq_field : public Directive {
  using self_type = Do_set_preq_field;
public:
  static const std::string KEY;

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Extractor::Format _name_fmt;
  Extractor::Format _value_fmt;

  Do_set_preq_field() = default;
};

const std::string Do_set_preq_field::KEY { "set-preq-field" };

Errata Do_set_preq_field::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.preq_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<VIEW>(ctx.extract(_name_fmt));
    if (auto field { hdr.field_obtain(name) } ; field.is_valid()) {
      TextView value = std::get<VIEW>(ctx.extract(_value_fmt));
      field.assign(value);
    }
    return Errata().error(R"(Failed to find or create field "{}")", name);
  }
  return Errata().error(R"(Failed to assign field value due to invalid HTTP header.)");
}

Rv<Directive::Handle> Do_set_preq_field::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  if (key_node.IsSequence()) {
    if (key_node.size() == 2) {
      auto name_node { key_node[0] };
      auto value_node { key_node[1] };
      if (name_node.IsScalar() && value_node.IsScalar()) {
        auto && [ name_fmt, name_errata ] { cfg.parse_feature(name_node.Scalar()) };
        if (name_errata.is_ok()) {
          auto &&[value_fmt, value_errata]{cfg.parse_feature(value_node.Scalar())};
          if (value_errata.is_ok()) {
            auto self{new self_type};
            self->_name_fmt = std::move(name_fmt);
            self->_value_fmt = std::move(value_fmt);
            return {Handle(self), {}};
          }
          return {{}, std::move(
              value_errata.error(R"(While parsing value (second item) for "{}" key at {}.)", KEY
                                 , key_node))};
        }
        return {{}, std::move(
            name_errata.error(R"(While parsing name (first item) for "{}" key at {}.)", KEY
                               , key_node))};
      }
      return { {}, Errata().error(R"(Value for "{}" key at {} does have exactly 2 strings as required.)", KEY, key_node.Mark()) };
    }
    return { {}, Errata().error(R"(Value for "{}" key at {} does have exactly 2 elements as required.)", KEY, key_node.Mark()) };
  }
  return { {}, Errata().error(R"(Value for "{}" key at {} is not a list as required.)", KEY, key_node.Mark()) };
}
/* ------------------------------------------------------------------------------------ */

/** @c with directive.
 * 
 * This a central part of the 
 */
class With : public Directive {
  using super_type = Directive;
  using self_type = With;
public:
  static const std::string KEY;
  static const std::string SELECT_KEY;

  Errata invoke(Context &ctx) override;
  static swoc::Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Extractor::Format _ex; ///< Extractor format.

  /// A single case in the select.
  struct Case {
    Comparison::Handle _cmp; ///< Comparison to perform.
    Directive::Handle _do; ///< Directives to execute.
  };
  using CaseGroup = std::vector<Case>;
  CaseGroup _cases; ///< List of cases for the select.

  With() = default;

  Errata load_case(Config & cfg, YAML::Node node);
};

const std::string With::KEY { "with" };
const std::string With::SELECT_KEY { "select" };

class WithTuple : public Directive {
  friend class With;
  using super_type = Directive;
  using self_type = WithTuple;
public:
  static const std::string KEY;
  static const std::string SELECT_KEY;
  static const std::string ANY_OF_KEY;
  static const std::string ALL_OF_KEY;
  static const std::string NONE_OF_KEY;
  static const std::string ELSE_KEY;

  /// Operation to combine the matches in a case.
  enum Op {
    ANY_OF, ALL_OF, NONE_OF, ELSE
  };

  static const swoc::Lexicon<Op> OpName;

  Errata invoke(Context &ctx) override;

protected:
  std::vector<Extractor::Format> _ex; /// Extractor tuple.

  /// A single case in the select.
  struct Case {
    std::vector<Comparison::Handle> _cmp; ///< Comparisons to perform.
    Directive::Handle _do; ///< Directives to execute.
    Op _op = ALL_OF; ///< Combining operation.
  };
  using CaseGroup = std::vector<Case>;
  CaseGroup _cases; ///< List of cases for the select.

  WithTuple() = default;

  static swoc::Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);
  Errata load_case(Config & cfg, YAML::Node node, unsigned size);
};

const std::string WithTuple::KEY { With::KEY };
const std::string WithTuple::SELECT_KEY { With::SELECT_KEY };
const std::string WithTuple::ANY_OF_KEY { "any-of" };
const std::string WithTuple::ALL_OF_KEY { "all-of" };
const std::string WithTuple::NONE_OF_KEY { "none-of" };
const std::string WithTuple::ELSE_KEY { "else" };

const swoc::Lexicon<WithTuple::Op> WithTuple::OpName { { ANY_OF, ANY_OF_KEY }, { ALL_OF , ALL_OF_KEY }, { NONE_OF , NONE_OF_KEY }, { ELSE, ELSE_KEY } };
BufferWriter& bwformat(BufferWriter& w, bwf::Spec const& spec, WithTuple::Op op) {
  if (spec.has_numeric_type()) {
    return bwformat(w, spec, static_cast<unsigned>(op));
  }
  return bwformat(w, spec, WithTuple::OpName[op]);
}


Errata With::invoke(Context &ctx) {
  FeatureData feature { ctx.extract(_ex) };
  for ( auto const& c : _cases ) {
    if ((*c._cmp)(feature)) {
      ctx._feature = feature;
      return c._do->invoke(ctx);
    }
  }
  return {};
}

Errata WithTuple::invoke(Context &ctx) {
  return {};
};

swoc::Rv<Directive::Handle> With::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  YAML::Node select_node { drtv_node[SELECT_KEY] };
  if (! select_node) {
    return {{}, Errata().error(R"(Required "{}" key not found in "{}" directive at {}.)", SELECT_KEY
                               , KEY, drtv_node.Mark())};
  } else if (!(select_node.IsMap() || select_node.IsSequence()) ) {
    return {{}, Errata().error(R"(The value for "{}" at {} in "{}" directive at {} is not a list or object.")"
               , SELECT_KEY, select_node.Mark(), KEY, drtv_node.Mark()) };
  }

  if (key_node.IsScalar()) {
    // Need to parse this first, so the feature type can be determined.
    auto && [ fmt, errata ] = cfg.parse_feature(key_node.Scalar());

    if (!errata.is_ok()) {
      return {{}, std::move(errata)};
    }

    self_type * self = new self_type;
    Handle handle(self); // for return, and cleanup in case of error.
    self->_ex = std::move(fmt);

    if (select_node.IsMap()) {
      errata = self->load_case(cfg, select_node);
      if (! errata.is_ok()) {
        return {{}, std::move(errata)};
      }
    } else {
      for (YAML::Node child : select_node) {
        errata = (self->load_case(cfg, child));
        if (!errata.is_ok()) {
          errata.error(R"(While loading "{}" directive at {} in "{}" at {}.)", KEY, drtv_node.Mark()
                     , SELECT_KEY, select_node.Mark());
          return { {}, std::move(errata) };
        }
      }
    }
    return {std::move(handle), {}};
  } else if (key_node.IsSequence()) {
    return WithTuple::load(cfg, drtv_node, key_node);
  }

  return { {}, Errata().error(R"("{}" value at {} is not a string or list of strings as required.)", KEY, key_node.Mark()) };
}

Errata With::load_case(Config & cfg, YAML::Node node) {
  if (node.IsMap()) {
    Case c;
    if (YAML::Node do_node{node[DO_KEY]}; do_node) {
      bool ref_p = false;
      auto &&[handle, errata]{cfg.load_directive(do_node, _ex._feature_type, ref_p)};
      if (errata.is_ok()) {
        c._do = std::move(handle);
      } else {
        errata.error(R"(While parsing "{}" key at {} in selection case at {}.)", DO_KEY
                     , do_node.Mark(), node.Mark());
        return errata;
      }
    } else {
      c._do.reset(new NilDirective);
    }

    auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, _ex._feature_type, node)};
    if (cmp_errata.is_ok()) {
      c._cmp = std::move(cmp_handle);
      _cases.emplace_back(std::move(c));
    } else {
      cmp_errata.error(R"(While parsing "{}" key at {}.)", SELECT_KEY, node.Mark());
      return std::move(cmp_errata);
    }

    return {}; // everything is fine!
  }
  return Errata().error(R"(The value at {} for "{}" is not an object as required.")", node.Mark(), SELECT_KEY);
}

// This is only called from @c With::load which calls this iff the @c with key value is a sequence.
swoc::Rv<Directive::Handle> WithTuple::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  YAML::Node select_node { drtv_node[SELECT_KEY] };
  std::vector<Extractor::Format> ex_tuple;

  // Get the feature extraction tuple.
  for ( auto const& child : key_node ) {
    if (child.IsScalar()) {
      auto &&[fmt, errata]{Extractor::parse(child.Scalar())};
      if (errata.is_ok()) {
        ex_tuple.emplace_back(std::move(fmt));
      } else {
        errata.error(R"(While processing element at {} in feature tuple at {} in "{}" directive at {}.)", child.Mark(), key_node.Mark(), KEY, key_node.Mark());
        return { {}, std::move(errata) };
      }
    } else {
      return { {}, Errata().error(R"(Element at {} in feature tuple at {} in "{}" directive at {} is not a string as required.)", child.Mark(), key_node.Mark(), KEY, key_node.Mark()) };
    }
  }

  self_type * self = new self_type;
  Handle handle(self); // for return, and cleanup in case of error.
  self->_ex = std::move(ex_tuple);

  // Next process the selection cases.
  if (select_node.IsMap()) {
    auto errata { self->load_case(cfg, select_node, ex_tuple.size()) };
    if (! errata.is_ok()) {
      return {{}, std::move(errata)};
    }
  } else if (select_node.IsSequence()) {
    for ( auto const& case_node : select_node ) {
      auto errata { self->load_case(cfg, case_node, ex_tuple.size()) };
      if (! errata.is_ok()) {
        errata.error(R"(While processing list in selection case at {}.)", select_node.Mark());
        return { {}, std::move(errata) };
      }
    }
  } else {
    return { {}, Errata().error(R"(Value at {} for "{}" is not an object or sequence as required.)", select_node.Mark(), SELECT_KEY) };
  }
  return { std::move(handle), {}};
}

Errata WithTuple::load_case(Config & cfg, YAML::Node node, unsigned size) {
  if (node.IsMap()) {
    Case c;
    if (YAML::Node do_node{node[DO_KEY]}; do_node) {
      auto &&[do_handle, do_errata]{cfg.load_directive(do_node)};
      if (do_errata.is_ok()) {
        c._do = std::move(do_handle);
      }
    } else {
      c._do.reset(new NilDirective);
    }

    // Each comparison is a list under one of the combinator keys.
    YAML::Node op_node;
    for ( auto const&  [ key , name ] : OpName ) {
      op_node.reset(node[name]);
      if (! op_node.IsNull()) {
        c._op = key;
        break;
      }
    }

    if (c._op == ELSE) {
      // ignore everything in the node.
    } else if (op_node.IsSequence()) {
      if (op_node.size() != size) {
        return Errata().error(R"(Comparison list at {} for "{}" has {} comparisons instead of the required {}.)", op_node.Mark(), OpName[c._op], op_node.size(), size);
      }

      for ( unsigned idx = 0 ; idx < size ; ++idx ) {
        auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, _ex[idx]._feature_type, op_node[idx])};
        if (cmp_errata.is_ok()) {
          c._cmp.emplace_back(std::move(cmp_handle));
        } else {
          cmp_errata.error(R"(While parsing comparison #{} at {} for "{}" at {}.)", idx, op_node[idx].Mark(), OpName[c._op], op_node.Mark());
          return std::move(cmp_errata);
        }
      }
    } else if (op_node.IsNull()) {
      return Errata().error(R"(Selection case at {} does not the required key of "{}", "{}", or "{}".)"
                            , node.Mark(), ALL_OF_KEY, ANY_OF_KEY, NONE_OF_KEY);
    } else {
      return Errata().error(R"(Selection case key "{}" at {} is not a list as required.)", OpName[c._op]
                            , op_node.Mark());
    }

    _cases.emplace_back(std::move(c));
    return {};
  }
  return Errata().error(R"(The case value at {} for "{}" is not an object.")", node.Mark(), SELECT_KEY);
}
/* ------------------------------------------------------------------------------------ */

const std::string When::KEY { "when" };

When::When(Hook hook_idx, Directive::Handle &&directive) : _hook(hook_idx), _directive(std::move
(directive)) {}

// Put the internal directive in the directive array for the specified hook.
Errata When::invoke(Context &ctx) {
  return ctx.when_do(_hook, _directive.get());
}

swoc::Rv<Directive::Handle> When::load(Config& cfg, YAML::Node drtv_node, YAML::Node key_node) {
  Errata zret;
  if (Hook hook_idx{HookName[key_node.Scalar()]} ; hook_idx != Hook::INVALID) {
    if (YAML::Node do_node{drtv_node[DO_KEY]}; do_node) {
      auto &&[do_handle, do_errata]{cfg.load_directive(do_node)};
      if (do_errata.is_ok()) {
        ++cfg._directive_count[static_cast<unsigned>(hook_idx)];
        return { Handle{new self_type{hook_idx, std::move(do_handle)}} , {}};
      } else {
        zret.note(do_errata);
        zret.error(R"(Failed to load directive in "{}" at {} in "{}" directive at {}.)", DO_KEY
                   , do_node.Mark(), KEY, key_node.Mark());
      }
    } else {
      zret.error(R"(The required "{}" key was not found in the "{}" directive at {}.")", DO_KEY, KEY
                 , drtv_node.Mark());
    }
  } else {
    zret.error(R"(Invalid hook name "{}" in "{}" directive at {}.)", key_node.Scalar(), When::KEY
               , key_node.Mark());
  }
  return {{}, std::move(zret)};
}

/* ------------------------------------------------------------------------------------ */

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Directive::define(When::KEY, When::load);
  Directive::define(With::KEY, With::load);
  Directive::define(Do_set_preq_field::KEY, Do_set_preq_field::load);
  Directive::define(Do_set_preq_url_host::KEY, Do_set_preq_url_host::load);
  Directive::define(Do_set_preq_host::KEY, Do_set_preq_host::load);
  return true;
} ();
} // namespace
