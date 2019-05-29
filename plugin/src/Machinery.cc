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

#include "ts/ts.h"

#include "swoc/TextView.h"

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
  // Ignorable keys in the directive. Currently just one, so hand code it. Make this better
  // if there is ever more than one.
  YAML::Node key_node;
  for ( auto const&  [ key_node, value ] : drtv_node ) {
    TextView key { key_node.Scalar() };
    if (key == DO_KEY) {
      continue;
    }
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
  static swoc::Rv<Handle> load(YAML::Node node);
protected:
  static Errata load_case(YAML::Node node);

  /// A single case in the select.
  struct Case {
    Comparison::Handle _cmp; ///< Comparison to perform.
    Directive::Handle _do; ///< Directives to execute.
  };
  using CaseGroup = std::vector<Case>;
  CaseGroup _cases; ///< List of cases for the select.
};

const std::string With::KEY { "with" };
const std::string With::SELECT_KEY { "select" };

Errata With::invoke(Context &ctx) {
  Errata zret;
  return zret;
}

swoc::Rv<Directive::Handle> With::load(YAML::Node node) {
  Errata errata;
  if ( auto with_node { node[KEY] } ; with_node ) {
    if (auto select_node { node[SELECT_KEY] } ; select_node ) {
      if (select_node.IsMap()) {
        errata = self_type::load_case(select_node);
      } else if (select_node.IsSequence()) {
        for ( YAML::Node child : select_node ) {
          errata.note(self_type::load_case(child));
        }
        if (! errata.is_ok()) {
          errata.error(R"(While loading "{}" clauses at {} in "{}" at {}.)", SELECT_KEY,
              select_node.Mark(), KEY, node.Mark());
        }
      } else {
        errata.error(R"(The value for "{}" at {} in "{}" at {} is not a list or object.")",
            SELECT_KEY, select_node.Mark(), KEY, with_node.Mark());
      }
    } else {
      errata.error(R"("{}" clause at {} does not contain required key "{}")",
          KEY, node.Mark(), SELECT_KEY);
    }
  } else {
    errata.error(R"(Key "{}" not found in directive at {})", KEY, node.Mark());
  }
  return { {}, errata };
}

Errata With::load_case(YAML::Node node) {
  Errata errata;
  if (node.IsMap()) {
    YAML::Node do_node { node[DO_KEY] }, cmp_node;
    if (node.size() > 2) {
      errata.error(R"(The clause at {} for "{}" has too many ({}) keys (at most two allowed).")",
          node.Mark(), SELECT_KEY, node.size());
    } else if (node.size() < 1) {
      errata.error(R"(The clause at {} for "{}" has no keys (must have at least one).")", node.Mark(), SELECT_KEY);
    } else if (do_node.IsDefined() && node.size() == 1) {
      errata.error(R"(The case at {} has no comparison.")", node.Mark());
    } else {
      cmp_node.reset(node[0] == do_node ? node[1] : node[0]);

    }
  } else {
    errata.error(R"(The clause at {} for "{}" is not an object.")", node.Mark(), SELECT_KEY);
  }
  return errata;
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

