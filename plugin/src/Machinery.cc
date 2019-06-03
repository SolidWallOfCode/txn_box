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

#include "txn_box/yaml-util.h"
#include "txn_box/ts_util.h"

using swoc::TextView;
using swoc::Errata;
using swoc::Rv;

const std::string Directive::DO_KEY { "do" };
/* ------------------------------------------------------------------------------------ */
Rv<Directive::Handle> Directive::load(Config & cfg, YAML::Node drtv_node) {
  YAML::Node key_node;
  for ( auto const&  [ key_node, value ] : drtv_node ) {
    TextView key { key_node.Scalar() };
    // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
    // if there is ever more than one.
    if (key == DO_KEY) {
      continue;
    }
    // See if this is in the factory. It's not an error if it's not, to enable adding extra
    // keys to directives. First key that is in the factory determines the directive type.
    if ( auto spot { _factory.find(key) } ; spot != _factory.end()) {
      return spot->second(cfg, drtv_node, key_node);
    }
  }
  return { {}, Errata().error(R"(Directive at {} has no recognized tag.)", drtv_node.Mark()) };
}

/* ------------------------------------------------------------------------------------ */
class Do_Set_Preq_Url_Host : public Directive {
  using super_type = Directive;
  using self_type = Do_Set_Preq_Url_Host;
public:
  static const std::string KEY;

  explicit Do_Set_Preq_Url_Host(TextView text) : _host(text) {}

  Errata invoke(Context &ctx) override;
  static swoc::Rv<Handle> load(YAML::Node node);
protected:
  std::string _host;
};

const std::string Do_Set_Preq_Url_Host::KEY { "set-preq-url-host" };

Errata Do_Set_Preq_Url_Host::invoke(Context &ctx) {
  Errata zret;
  return zret;
}

swoc::Rv<Directive::Handle> Do_Set_Preq_Url_Host::load(YAML::Node node) {
  Errata errata;
  if ( auto item { node[KEY] } ; item ) {
    return { Handle{new self_type{item.Scalar()}}, {} };
  }
  errata.error(R"(Key "{}" not found in directive at {})", KEY, node.Mark());
  return { {}, errata };
}

/* ------------------------------------------------------------------------------------ */

/// @c with directive.
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
  /// Data type for @a _ex.
  /// This is a @c STRING unless @a _ex is a singleton with a different preferred type.
  Extractor::Type _feature_type = Extractor::VIEW;
  Extractor::Feature _feature; ///< Extracted feature.

  /// A single case in the select.
  struct Case {
    Comparison::Handle _cmp; ///< Comparison to perform.
    Directive::Handle _do; ///< Directives to execute.
  };
  using CaseGroup = std::vector<Case>;
  CaseGroup _cases; ///< List of cases for the select.

  With(Extractor::Format && fmt, CaseGroup && cases) : _ex(std::move(fmt)), _cases(std::move(cases)) {}

  static Errata load_case(Config & cfg, CaseGroup& cases, YAML::Node node);
};

const std::string With::KEY { "with" };
const std::string With::SELECT_KEY { "select" };

Errata With::invoke(Context &ctx) {
  return {};
}

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

  /// Operation to combine the matches in a case.
  enum Op {
    ANY_OF, ALL_OF, NONE_OF
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

  WithTuple(std::vector<Extractor::Format> && fmt, CaseGroup && cases) : _ex(std::move(fmt)), _cases(std::move(cases)) {}

  static swoc::Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);
  static Errata load_case(Config & cfg, CaseGroup& cases, YAML::Node node, unsigned size);
};

const std::string WithTuple::KEY { With::KEY };
const std::string WithTuple::SELECT_KEY { With::SELECT_KEY };
static const std::string ANY_OF_KEY { "any-of" };
static const std::string ALL_OF_KEY { "all-of" };
static const std::string NONE_OF_KEY { "none-of" };

const swoc::Lexicon<WithTuple::Op> WithTuple::OpName { { ANY_OF, ANY_OF_KEY }, { ALL_OF , ALL_OF_KEY }, { NONE_OF , NONE_OF_KEY } };

Errata WithTuple::invoke(Context &ctx) {
  Errata zret;
  return zret;
};

swoc::Rv<Directive::Handle> With::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  YAML::Node select_node { drtv_node[SELECT_KEY] };
  if (! select_node) {
    return {{}, Errata().error(R"(Required "{}" key not found in "{}" directive at {}.)", SELECT_KEY
                               , KEY, drtv_node.Mark())};
  }

  if (key_node.IsScalar()) {
    Errata zret;
    With::CaseGroup cases;

    auto &&[fmt, errata]{Extractor::parse(key_node.Scalar())};

    if (errata.is_ok()) {
      if (select_node.IsMap()) {
        zret = self_type::load_case(cfg, cases, select_node);
      } else if (select_node.IsSequence()) {
        for (YAML::Node child : select_node) {
          zret.note(self_type::load_case(cfg, cases, child));
        }
        if (!zret.is_ok()) {
          zret.error(R"(While loading "{}" directive at {} in "{}" at {}.)", KEY, drtv_node.Mark()
                     , SELECT_KEY, select_node.Mark());
        }
      } else {
        zret.error(R"(The value for "{}" at {} in "{}" directive at {} is not a list or object.")"
                   , SELECT_KEY, select_node.Mark(), KEY, drtv_node.Mark());
      }

      if (zret.is_ok()) {
        auto drtv { new With(std::move(fmt), std::move(cases)) };
        cfg.provides(drtv->_ex);
        return {Handle{drtv}, {}};
      }
      return {{}, std::move(zret)};
    } else {
      return { {}, std::move(errata) };
    }
  } else if (key_node.IsSequence()) {
    return WithTuple::load(cfg, drtv_node, key_node);
  }
  return { {}, Errata().error(R"("{}" value at {} is not a string or list of strings as required.)", KEY, key_node.Mark()) };
}

Errata With::load_case(Config & cfg, CaseGroup & cases, YAML::Node node) {
  if (node.IsMap()) {
    Case c;
    if (YAML::Node do_node{node[DO_KEY]}; do_node) {
      auto &&[handle, errata]{cfg.load_directive(do_node)};
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

    auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, node)};
    if (cmp_errata.is_ok()) {
      c._cmp = std::move(cmp_handle);
      cases.emplace_back(std::move(c));
    } else {
      cmp_errata.error(R"(While parsing "{}" key at {}.)", SELECT_KEY, node.Mark());
      return std::move(cmp_errata);
    }

    return {}; // everything is fine!
  }
  return Errata().error(R"(The value at {} for "{}" is not an object.")", node.Mark(), SELECT_KEY);
}

// This is only called from @c With::load which calls this iff the @c with key value is a sequence.
swoc::Rv<Directive::Handle> WithTuple::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  YAML::Node select_node { drtv_node[SELECT_KEY] };
  Errata zret;
  CaseGroup cases;
  std::vector<Extractor::Format> ex_tuple;

  // Get the feature extraction tuple.
  for ( auto const& child : key_node ) {
    if (child.IsScalar()) {
      auto &&[fmt, errata]{Extractor::parse(child.Scalar())};
      if (errata.is_ok()) {
        ex_tuple.emplace_back(std::move(fmt));
      } else {
        zret = std::move(errata);
        break;
      }
    } else {
      zret.error(R"(Value at {} is not a string as required.)", child.Mark());
      break;
    }
  }
  if (!zret.is_ok()) {
    zret.error(R"(While parsing feature extraction tuple for "{}" at {} in directive at {}.)", KEY, key_node.Mark(), drtv_node.Mark());
    return {{}, std::move(zret)};
  }

  // Next process the selection cases.
  if (select_node.IsMap()) {
    zret = self_type::load_case(cfg, cases, select_node, ex_tuple.size());
  } else if (select_node.IsSequence()) {
    for ( auto const& case_node : select_node ) {
      zret = self_type::load_case(cfg, cases, case_node, ex_tuple.size());
      if (! zret.is_ok()) {
        zret.error(R"(While processing list in selection case at {}.)", select_node.Mark());
        break;
      }
    }
  } else {
    zret.error(R"(Value at {} for "{}" is not an object or sequence as required.)", select_node.Mark(), SELECT_KEY);
  }
  if (zret.is_ok()) {
    return {Handle(new WithTuple(std::move(ex_tuple), std::move(cases))), {}};
  }
  return { {}, std::move(zret) };
}

Errata WithTuple::load_case(Config & cfg, CaseGroup & cases, YAML::Node node, unsigned size) {
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
    if (op_node.IsSequence()) {
      for (auto const &cmp_node : op_node) {
        auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, cmp_node)};
        if (cmp_errata.is_ok()) {
          c._cmp.emplace_back(std::move(cmp_handle));
        } else {
          cmp_errata.error(R"(While parsing "{}" key at {}.)", SELECT_KEY, node.Mark());
          return std::move(cmp_errata);
        }
      }
      if (c._cmp.size() != size) {
        return Errata().error(R"(Comparison list at {} has {} comparisons instead of the required {}.)", op_node.Mark(), c._cmp.size(), size);
      }
    } else if (op_node.IsNull()) {
      return Errata().error(R"(Selection case at {} does not the required key of "{}", "{}", or "{}".)"
                            , node.Mark(), ALL_OF_KEY, ANY_OF_KEY, NONE_OF_KEY);
    } else {
      return Errata().error(R"(Selection case key "{}" at {} is not a list as required.)", OpName[c._op]
                            , op_node.Mark());
    }

    cases.emplace_back(std::move(c));
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

