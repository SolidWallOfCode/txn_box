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

#include "ts/ts.h"

#include "txn_box/Directive.h"
#include "txn_box/Extractor.h"
#include "txn_box/Comparison.h"
#include "txn_box/Config.h"
#include "txn_box/yaml-util.h"

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
  Errata zret;
  return zret;
}

class WithTuple : public Directive {
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

  Errata invoke(Context &ctx) override;

protected:
  static Errata load_case(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

  std::vector<Extractor::Format> _fmt; /// Extractor tuple.

  /// A single case in the select.
  struct Case {
    std::vector<Comparison::Handle> _cmp; ///< Comparisons to perform.
    Directive::Handle _do; ///< Directives to execute.
    Op _op = ALL_OF; ///< Combining operation.
  };
  using CaseGroup = std::vector<Case>;
  CaseGroup _cases; ///< List of cases for the select.
};

const std::string WithTuple::KEY { With::KEY };
const std::string WithTuple::SELECT_KEY { With::SELECT_KEY };
static const std::string ANY_OF_KEY { "any-of" };
static const std::string ALL_OF_KEY { "all-of" };
static const std::string NONE_OF_KEY { "none-of" };

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

    if (!errata.is_ok()) {
      return { {}, std::move(errata) };
    }

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
      zret.error(R"(The value for "{}" at {} in "{}" diretive at {} is not a list or object.")"
                 , SELECT_KEY, select_node.Mark(), KEY, drtv_node.Mark());
    }
    if (zret.is_ok()) {
      return {Handle{new With(std::move(fmt), std::move(cases))}, {}};
    }
    return {{}, std::move(zret)};
  } else if (key_node.IsSequence()) {
  }
  return { {}, Errata().error(R"("{}" value at {} is not a string or list of strings as required.)", KEY, key_node.Mark()) };
}

Errata With::load_case(Config & cfg, CaseGroup & cases, YAML::Node node) {
  Errata zret;
  Case c;

  if (node.IsMap()) {
    [[maybe_unused]] bool do_key_p = false; // found DO key?
    for ( auto const& [ key, value ] : node ) {
      if (key.Scalar() == DO_KEY) {
        auto && [ handle, errata ] { cfg.load_directive(value) };
        do_key_p = true;
        if (! errata.is_ok()) {
          errata.error(R"(While parsing "{}" key at {}.)", SELECT_KEY, node.Mark());
          return std::move(errata);
        }
        c._do = std::move(handle);
      } else {
        auto && [ handle, errata ] { Comparison::load(cfg, node) };
      }
    }
    // At some point we'll want to handle the no DO key specially.

      if (cmp_node.IsMap()) {
        for ( auto const& [ key, value ] : cmp_node ) {
          if ( auto cmp_asm { Comparison::find(key.Scalar()) } ; nullptr != cmp_asm ) {
            auto && [ handle, errata ] { (*cmp_asm)(cmp_node) };
          }
        }
      } else {
      }
    }
  } else {
    zret.error(R"(The clause at {} for "{}" is not an object.")", node.Mark(), SELECT_KEY);
  }
  return zret;
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

