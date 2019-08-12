/** @file
   Non-core directive implementations.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include "txn_box/common.h"

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
using namespace swoc::literals;

/* ------------------------------------------------------------------------------------ */
class Do_set_preq_url_host : public Directive {
  using super_type = Directive;
  using self_type = Do_set_preq_url_host;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  explicit Do_set_preq_url_host(TextView text) : _host(text) {}

  Errata invoke(Context &ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);
  std::string _host;
};

const std::string Do_set_preq_url_host::KEY { "set-preq-url-host" };
const HookMask Do_set_preq_url_host::HOOKS { MaskFor({Hook::PREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

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
  static const HookMask HOOKS; ///< Valid hooks for directive.

  explicit Do_set_preq_host(TextView text);

  Errata invoke(Context &ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);
  std::string _host;
};

const std::string Do_set_preq_host::KEY { "set-preq-host" };
const HookMask Do_set_preq_host::HOOKS { MaskFor({Hook::PREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

Do_set_preq_host::Do_set_preq_host(TextView text) : _host(text) {}

Errata Do_set_preq_host::invoke(Context &ctx) {
  Errata zret;
  return zret;
}

swoc::Rv<Directive::Handle> Do_set_preq_host::load(Config &, YAML::Node, YAML::Node key_node) {
  return { Handle{new self_type{key_node.Scalar()}}, {} };
}

/* ------------------------------------------------------------------------------------ */
/// Base class for directives that operate on an HTTP header field.
class FieldDirective {
protected:
  Extractor::Format _name_fmt; ///< Name of the field.
  Extractor::Format _value_fmt; ///< Value for the field.

  virtual TextView key() const = 0; ///< Inheriting class must provide this for diagnostic messages.
  /// Load from YAML configuration.
  Errata load(Config& cfg, YAML::Node const& node);
};

Errata FieldDirective::load(Config & cfg, YAML::Node const& node) {
  if (node.IsSequence()) {
    if (node.size() == 2) {
      auto name_node{node[0]};
      auto value_node{node[1]};

      // Load up the field name.
      auto &&[name_fmt, name_errata]{cfg.parse_feature(name_node)};
      if (!name_errata.is_ok()) {
        return std::move(
            name_errata.error(R"(While parsing name (first item) for "{}" key at {}.)", this->key()
                              , node.Mark()));
      }
      if (name_fmt._feature_type != STRING) {
        return Errata().error(
            R"(The field name for "{}" key at {} is not a string type as required.)", this->key()
            , node.Mark());
      }

      /// Load the field value.
      auto &&[value_fmt, value_errata]{cfg.parse_feature(value_node)};
      if (!value_errata.is_ok()) {
        return std::move(
            value_errata.error(R"(While parsing value (second item) for "{}" key at {}.)"
                               , this->key(), node.Mark()));
      }
      if (value_fmt._feature_type != STRING) {
        return Errata().error(
            R"(The field value for "{}" key at {} is not a string type as required.)", this->key()
            , node.Mark());
      }

      // success, update the instance and return no errors.
      _name_fmt = std::move(name_fmt);
      _value_fmt = std::move(value_fmt);
      return {};
    }
    return Errata().error(R"(Value for "{}" key at {} does not have exactly 2 elements as required.)", this->key(), node.Mark());
  }
  return Errata().error(R"(Value for "{}" key at {} is not a list of two elements as required.)", this->key(), node.Mark());
}
/* ------------------------------------------------------------------------------------ */
class Do_set_preq_field : public Directive, FieldDirective {
  using self_type = Do_set_preq_field;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Do_set_preq_field() = default;
  TextView key() const override { return KEY; }
};

const std::string Do_set_preq_field::KEY { "set-preq-field" };
const HookMask Do_set_preq_field::HOOKS { MaskFor({Hook::PREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

Errata Do_set_preq_field::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.preq_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<IndexFor(STRING)>(ctx.extract(_name_fmt));
    if (auto field { hdr.field_obtain(name) } ; field.is_valid()) {
      TextView value = std::get<IndexFor(STRING)>(ctx.extract(_value_fmt));
      field.assign(value);
    }
    return Errata().error(R"(Failed to find or create field "{}")", name);
  }
  return Errata().error(R"(Failed to assign field value due to invalid HTTP header.)");
}

Rv<Directive::Handle> Do_set_preq_field::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  auto * self = new self_type;
  Handle handle(self);
  Errata errata { self->FieldDirective::load(cfg, key_node) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata.info(R"(While parsing directive at {}.)", drtv_node.Mark()))};
  }
  return { std::move(handle), {} };
}

/* ------------------------------------------------------------------------------------ */
/// Set a field in the client request if not already set.
class Do_set_creq_field_default : public Directive, FieldDirective {
  using self_type = Do_set_creq_field_default; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Perform directive.
  Errata invoke(Context & ctx) override;
  /// Load from YAML configuration.
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Do_set_creq_field_default() = default;
  TextView key() const override { return KEY; }
};

const std::string Do_set_creq_field_default::KEY { "set-creq-field-default" };
const HookMask Do_set_creq_field_default::HOOKS { MaskFor({Hook::CREQ, Hook::PREQ, Hook::PRE_REMAP}) };

Errata Do_set_creq_field_default::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<IndexFor(STRING)>(ctx.extract(_name_fmt));
    if (ts::HttpField field { hdr.field_obtain(name) } ; field.is_valid()) {
      TextView value = std::get<IndexFor(STRING)>(ctx.extract(_value_fmt));
      field.assign_if_not_set(value);
    }
    return Errata().error(R"(Failed to find or create field "{}")", name);
  }
  return Errata().error(R"(Failed to assign field value due to invalid HTTP header.)");
}

Rv<Directive::Handle> Do_set_creq_field_default::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  auto * self = new self_type;
  Handle handle(self);
  Errata errata { self->FieldDirective::load(cfg, key_node) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata.info(R"(While parsing directive at {}.)", drtv_node.Mark()))};
  }
  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Remove a field from the client request.
class Do_remove_creq_field : public Directive {
  using self_type = Do_remove_creq_field; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Perform directive.
  Errata invoke(Context & ctx) override;
  /// Load from YAML configuration.
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Extractor::Format _name_fmt; ///< Field name.

  Do_remove_creq_field() = default;
};

const std::string Do_remove_creq_field::KEY { "remove-creq-field" };
const HookMask Do_remove_creq_field::HOOKS { MaskFor({Hook::CREQ, Hook::PREQ, Hook::PRE_REMAP}) };

Errata Do_remove_creq_field::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.creq_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<IndexFor(STRING)>(ctx.extract(_name_fmt));
    hdr.field_remove(name);
  }
  return {};
}

Rv<Directive::Handle> Do_remove_creq_field::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  // Load up the field name.
  auto &&[name_fmt, name_errata]{cfg.parse_feature(key_node)};
  if (!name_errata.is_ok()) {
    return { {}, std::move(
        name_errata.error(R"(While parsing field name for "{}" key at {}.)", KEY
                          , key_node.Mark())) };
  }
  if (name_fmt._feature_type != STRING) {
    return { {}, Errata().error(
        R"(The field name for "{}" key at {} is not a string type as required.)", KEY
        , key_node.Mark()) };
  }

  auto * self = new self_type;
  Handle handle(self);
  self->_name_fmt = std::move(name_fmt);

  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Set a field in the client request if not already set.
class Do_set_preq_field_default : public Directive, FieldDirective {
  using self_type = Do_set_preq_field_default; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Perform directive.
  Errata invoke(Context & ctx) override;
  /// Load from YAML configuration.
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Do_set_preq_field_default() = default;
  TextView key() const override { return KEY; }
};

const std::string Do_set_preq_field_default::KEY { "set-preq-field-default" };
const HookMask Do_set_preq_field_default::HOOKS { MaskFor({Hook::PRE_REMAP, Hook::PREQ}) };

Errata Do_set_preq_field_default::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.preq_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<IndexFor(STRING)>(ctx.extract(_name_fmt));
    if (ts::HttpField field { hdr.field_obtain(name) } ; field.is_valid()) {
      TextView value = std::get<IndexFor(STRING)>(ctx.extract(_value_fmt));
      field.assign_if_not_set(value);
    }
    return Errata().error(R"(Failed to find or create field "{}")", name);
  }
  return Errata().error(R"(Failed to assign field value due to invalid HTTP header.)");
}

Rv<Directive::Handle> Do_set_preq_field_default::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  auto * self = new self_type;
  Handle handle(self);
  Errata errata { self->FieldDirective::load(cfg, key_node) };
  if (! errata.is_ok()) {
    return { {}, std::move(errata.info(R"(While parsing directive at {}.)", drtv_node.Mark()))};
  }
  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Remove a field from the upstream response.
class Do_remove_ursp_field : public Directive {
  using self_type = Do_remove_ursp_field; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Perform directive.
  Errata invoke(Context & ctx) override;
  /// Load from YAML configuration.
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Extractor::Format _name_fmt; ///< Field name.

  Do_remove_ursp_field() = default;
};

const std::string Do_remove_ursp_field::KEY { "remove-ursp-field" };
const HookMask Do_remove_ursp_field::HOOKS { MaskFor(Hook::URSP) };

Errata Do_remove_ursp_field::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.ursp_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<STRING>(ctx.extract(_name_fmt));
    hdr.field_remove(name);
  }
  return {};
}

Rv<Directive::Handle> Do_remove_ursp_field::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  // Load up the field name.
  auto &&[name_fmt, name_errata]{cfg.parse_feature(key_node)};
  if (!name_errata.is_ok()) {
    return { {}, std::move(
        name_errata.error(R"(While parsing field name for "{}" key at {}.)", KEY
                          , key_node.Mark())) };
  }
  if (name_fmt._feature_type != STRING) {
    return { {}, Errata().error(
        R"(The field name for "{}" key at {} is not a string type as required.)", KEY
        , key_node.Mark()) };
  }

  auto * self = new self_type;
  Handle handle(self);
  self->_name_fmt = std::move(name_fmt);

  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Remove a field from the proxy response.
class Do_remove_prsp_field : public Directive {
  using self_type = Do_remove_prsp_field; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /// Perform directive.
  Errata invoke(Context & ctx) override;
  /// Load from YAML configuration.
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Extractor::Format _name_fmt; ///< Field name.

  Do_remove_prsp_field() = default;
};

const std::string Do_remove_prsp_field::KEY { "remove-prsp-field" };
const HookMask Do_remove_prsp_field::HOOKS { MaskFor(Hook::PRSP) };

Errata Do_remove_prsp_field::invoke(Context &ctx) {
  if (ts::HttpHeader hdr { ctx.prsp_hdr() } ; hdr.is_valid()) {
    TextView name = std::get<STRING>(ctx.extract(_name_fmt));
    hdr.field_remove(name);
  }
  return {};
}

Rv<Directive::Handle> Do_remove_prsp_field::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  // Load up the field name.
  auto &&[name_fmt, name_errata]{cfg.parse_feature(key_node)};
  if (!name_errata.is_ok()) {
    return { {}, std::move(
        name_errata.error(R"(While parsing field name for "{}" key at {}.)", KEY
                          , key_node.Mark())) };
  }
  if (name_fmt._feature_type != STRING) {
    return { {}, Errata().error(
        R"(The field name for "{}" key at {} is not a string type as required.)", KEY
        , key_node.Mark()) };
  }

  auto * self = new self_type;
  Handle handle(self);
  self->_name_fmt = std::move(name_fmt);

  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Set upstream response status code.
class Do_set_ursp_status : public Directive {
  using self_type = Do_set_ursp_status; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_node Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config & cfg, YAML::Node const& drtv_node, YAML::Node const& key_node);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Extractor::Format _status_fmt; ///< Return status.

  Do_set_ursp_status() = default;
};

const std::string Do_set_ursp_status::KEY { "set-ursp-status" };
const HookMask Do_set_ursp_status::HOOKS { MaskFor({Hook::URSP}) };

Errata Do_set_ursp_status::invoke(Context &ctx) {
  int status = TS_HTTP_STATUS_NONE;
  if (_status) {
    status = _status;
  } else {
    auto value = ctx.extract(_status_fmt);
    if (value.index() == IndexFor(INTEGER)) {
      status = std::get<IndexFor(INTEGER)>(value);
    } else { // it's a string.
      TextView src{std::get<IndexFor(STRING)>(value)}, parsed;
      auto n = swoc::svtou(src, &parsed);
      if (parsed.size() == src.size()) {
        status = n;
      } else {
        return Errata().error(R"(Invalid status "{}" for "{}" directive.)", value, KEY);
      }
    }
  }
  if (100 <= status && status <= 599) {
    ctx._txn.ursp_hdr().status_set(static_cast<TSHttpStatus>(status));
  } else {
    return Errata().error(R"(Status value {} out of range 100..599 for "{}" directive.)", status
                          , KEY);
  }
  return {};
}

Rv<Directive::Handle> Do_set_ursp_status::load(Config& cfg, YAML::Node const& drtv_node, YAML::Node const &key_node) {
  auto &&[fmt, errata]{cfg.parse_feature(key_node)};
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  auto self = new self_type;
  Handle handle(self);

  if (fmt._feature_type == INTEGER) {
    auto status = fmt._number;
    if (status < 100 || status > 599) {
      return { {}, Errata().error(R"(Status "{}" at {} is not a positive integer 100..599 as required.)"
                            , key_node.Scalar(), key_node.Mark()) };
    }
    self->_status = static_cast<TSHttpStatus>(status);
  } else if (fmt._feature_type == STRING) {
    self->_status_fmt = std::move(fmt);
  } else {
    return {{}, Errata().error(R"(Status "{}" at {} is not an integer nor string as required.)"
                               , key_node.Scalar(), key_node.Mark())};
  }
  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Set upstream response reason phrase.
class Do_set_ursp_reason : public Directive {
  using self_type = Do_set_ursp_reason; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_node Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config & cfg, YAML::Node const& drtv_node, YAML::Node const& key_node);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Extractor::Format _fmt; ///< Reason phrase.

  Do_set_ursp_reason() = default;
};

const std::string Do_set_ursp_reason::KEY { "set-ursp-reason" };
const HookMask Do_set_ursp_reason::HOOKS { MaskFor({Hook::URSP}) };

Errata Do_set_ursp_reason::invoke(Context &ctx) {
  auto value = ctx.extract(_fmt);
  ctx._txn.ursp_hdr().reason_set(std::get<IndexFor(STRING)>(value));
  return {};
}

Rv<Directive::Handle> Do_set_ursp_reason::load(Config& cfg, YAML::Node const& drtv_node, YAML::Node const &key_node) {
  auto &&[fmt, errata]{cfg.parse_feature(key_node)};
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  auto self = new self_type;
  Handle handle(self);

  self->_fmt = std::move(fmt);
  self->_fmt._feature_type = STRING;

  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Set body content for the proxy response.
class Do_set_prsp_body : public Directive {
  using self_type = Do_set_prsp_body; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_node Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config & cfg, YAML::Node const& drtv_node, YAML::Node const& key_node);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Extractor::Format _fmt; ///< Reason phrase.

  Do_set_prsp_body() = default;
};

const std::string Do_set_prsp_body::KEY { "set-prsp-body" };
const HookMask Do_set_prsp_body::HOOKS { MaskFor({Hook::URSP}) };

Errata Do_set_prsp_body::invoke(Context &ctx) {
  auto value = ctx.extract(_fmt);
  ctx._txn.error_body_set(std::get<IndexFor(STRING)>(value), "text/hmtl"_tv);
  return {};
}

Rv<Directive::Handle> Do_set_prsp_body::load(Config& cfg, YAML::Node const& drtv_node, YAML::Node const &key_node) {
  auto &&[fmt, errata]{cfg.parse_feature(key_node)};
  if (! errata.is_ok()) {
    return { {}, std::move(errata) };
  }
  auto self = new self_type;
  Handle handle(self);

  self->_fmt = std::move(fmt);
  self->_fmt._feature_type = STRING;

  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Redirect.
/// Although this could technically be done "by hand", it's common enough to justify
/// a specific directive.
class Do_redirect : public Directive {
  using self_type = Do_redirect; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const std::string STATUS_KEY; ///< Key for status value.
  static const std::string REASON_KEY; ///< Key for reason value.
  static const std::string LOCATION_KEY; ///< Key for location value.
  static const std::string BODY_KEY; ///< Key for body.

  static const HookMask HOOKS; ///< Valid hooks for directive.
  /// Need to do fixups on a later hook.
  static constexpr Hook FIXUP_HOOK = Hook::PRSP;
  /// Status code to use if not specified.
  static const int DEFAULT_STATUS = TS_HTTP_STATUS_MOVED_PERMANENTLY;

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_node Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  using index_type = FeatureGroup::index_type;
  FeatureGroup _fg;

  int _status = 0; ///< Return status is literal, 0 => extract at runtime.
  index_type _status_idx; ///< Return status.
  index_type _reason_idx; ///< Status reason text.
  index_type _location_idx; ///< Location field value.
  index_type _body_idx; ///< Body content of respons.
  /// Bounce from fixup hook directive back to @a this.
  Directive::Handle _set_location{new LambdaDirective([this] (Context& ctx) -> Errata { return this->fixup(ctx); })};

  /// Construct and do configuration related initialization.
  explicit Do_redirect(Config & cfg);

  Errata load_status();

  /// Load the location value from configuration.
  Errata load_location(Config& cfg, YAML::Node const& node);
  Errata load_reason(Config& cfg, YAML::Node const& node);
  Errata load_body(Config& cfg, YAML::Node const& node);

  /// Do post invocation fixup.
  Errata fixup(Context &ctx);
};

const std::string Do_redirect::KEY { "redirect" };
const std::string Do_redirect::STATUS_KEY { "status" };
const std::string Do_redirect::REASON_KEY { "reason" };
const std::string Do_redirect::LOCATION_KEY { "location" };
const std::string Do_redirect::BODY_KEY { "body" };

const HookMask Do_redirect::HOOKS { MaskFor({Hook::PRE_REMAP}) };

Do_redirect::Do_redirect(Config &cfg) {
  // Allocate a hook slot for the fixup directive.
  cfg.reserve_slot(FIXUP_HOOK);
}

Errata Do_redirect::invoke(Context& ctx) {
  // Finalize the location and stash it in context storage.
  FeatureData location = _fg.extract(ctx, _location_idx);
  ctx.commit(location);
  // Remember where it is so the fix up can find it.
  auto view = static_cast<TextView*>(ctx.storage_for(this).data());
  *view = std::get<IndexFor(STRING)>(location);

  // Set the status to prevent the upstream request.
  if (_status) {
    ctx._txn.status_set(static_cast<TSHttpStatus>(_status));
  } else {
    FeatureData value = _fg.extract(ctx, _status_idx);
    int status;
    if (value.index() == IndexFor(INTEGER)) {
      status = std::get<IndexFor(INTEGER)>(value);
    } else { // it's a string.
      TextView src{std::get<IndexFor(STRING)>(value)}, parsed;
      status = swoc::svtou(src, &parsed);
      if (parsed.size() != src.size()) {
        // Need to log an error.
        status = DEFAULT_STATUS;
      }
    }
    if (!(100 <= status && status <= 599)) {
      // need to log an error.
      status = DEFAULT_STATUS;
    }
    ctx._txn.status_set(static_cast<TSHttpStatus>(status));
  }
  // Arrange for fixup to get invoked.
  return ctx.on_hook_do(FIXUP_HOOK, _set_location.get());
}

Errata Do_redirect::fixup(Context &ctx) {
  auto hdr { ctx.prsp_hdr() };
  // Set the Location
  auto field { hdr.field_obtain(ts::HTTP_FIELD_LOCATION) };
  auto view = static_cast<TextView*>(ctx.storage_for(this).data());
  field.assign(*view);

  // Set the reason.
  if (_reason_idx != FeatureGroup::INVALID_IDX) {
    auto reason{_fg.extract(ctx, _reason_idx)};
    hdr.reason_set(std::get<IndexFor(STRING)>(reason));
  }
  // Set the body.
  if (_body_idx != FeatureGroup::INVALID_IDX) {
    auto body{_fg.extract(ctx, _body_idx)};
    ctx._txn.error_body_set(std::get<IndexFor(STRING)>(body), "text/html");
  }
  return {};
}

Errata Do_redirect::load_status() {
  _status_idx = _fg.exf_index(STATUS_KEY);

  if (_status_idx == FeatureGroup::INVALID_IDX) { // not present, use default value.
    _status = DEFAULT_STATUS;
    return {};
  }

  FeatureGroup::ExfInfo & info = _fg[_status_idx];
  FeatureGroup::ExfInfo::Single & ex = std::get<FeatureGroup::ExfInfo::SINGLE>(info._ex);

  if (ex._fmt.is_literal()) {
    TextView src{ex._fmt.literal()}, parsed;
    auto status = swoc::svtou(src, &parsed);
    if (parsed.size() != src.size() || status < 100 || status > 599) {
      return Errata().error(R"({} "{}" at {} is not a positive integer 100..599 as required.)", STATUS_KEY, src);
    }
    _status = status;
  } else {
    if (ex._fmt._feature_type != STRING && ex._fmt._feature_type != INTEGER) {
      return Errata().error(R"({} is not an integer nor string as required.)", STATUS_KEY);
    }
  }
  return {};
}

Rv<Directive::Handle> Do_redirect::load(Config &cfg, YAML::Node drtv_node, YAML::Node key_node) {
  Handle handle{new self_type{cfg}};
  Errata errata;
  auto self = static_cast<self_type *>(handle.get());
  if (key_node.IsScalar()) {
    errata = self->_fg.load_as_tuple(cfg, key_node, {{LOCATION_KEY, FeatureGroup::REQUIRED}});
  } else if (key_node.IsSequence()) {
    errata = self->_fg.load_as_tuple(cfg, key_node, { { STATUS_KEY, FeatureGroup::REQUIRED } , { LOCATION_KEY, FeatureGroup::REQUIRED } });
  } else if (key_node.IsMap()) {
    Errata errata = self->_fg.load(cfg, key_node, { { LOCATION_KEY, FeatureGroup::REQUIRED }, { STATUS_KEY }, { REASON_KEY }, { BODY_KEY } });
  } else {
    return {{}, Errata().error(
        R"(Value for "{}" key at {} is must be a scalar, a 2-tuple, or a map and is not.)", KEY
        , key_node.Mark())};
  }
  if (! errata.is_ok()) {
    errata.info(R"(While parsing value at {} in "{}" directive at {}.)", key_node.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(errata)};
  }

  self->_reason_idx = self->_fg.exf_index(REASON_KEY);
  self->_body_idx = self->_fg.exf_index(BODY_KEY);
  self->_location_idx = self->_fg.exf_index(LOCATION_KEY);
  errata.note(self->load_status());

  if (! errata.is_ok()) {
    errata.info(R"(While parsing value at {} in "{}" directive at {}.)", key_node.Mark(), KEY, drtv_node.Mark());
    return { {}, std::move(errata) };
  }

  return { std::move(handle), {} };
}
/* ------------------------------------------------------------------------------------ */
/// Send a debug message.
class Do_debug_msg : public Directive {
  using self_type = Do_debug_msg;
  using super_type = Directive;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node);

protected:
  Extractor::Format _tag_fmt;
  Extractor::Format _msg_fmt;

  Do_debug_msg(Extractor::Format && tag, Extractor::Format && msg);
};

const std::string Do_debug_msg::KEY { "debug" };
const HookMask Do_debug_msg::HOOKS { MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP }) };

Do_debug_msg::Do_debug_msg(Extractor::Format &&tag, Extractor::Format &&msg) : _tag_fmt(std::move(tag)), _msg_fmt(std::move(msg)) {}

Errata Do_debug_msg::invoke(Context &ctx) {
  TextView tag = std::get<IndexFor(STRING)>(ctx.extract(_tag_fmt));
  TextView msg = std::get<IndexFor(STRING)>(ctx.extract(_msg_fmt));
  TSDebug(tag.data(), "%.*s", static_cast<int>(msg.size()), msg.data());
  return {};
}

Rv<Directive::Handle> Do_debug_msg::load(Config & cfg, YAML::Node drtv_node, YAML::Node key_node) {
  if (key_node.IsScalar()) {
    auto && [ msg_fmt, msg_errata ] = cfg.parse_feature(key_node);
    if (! msg_errata.is_ok()) {
      msg_errata.info(R"(While parsing message at {} for "{}" directive at {}.)", key_node.Mark(), KEY, drtv_node.Mark());
      return { {}, std::move(msg_errata)};
    }
    return { Handle{new self_type{Extractor::literal(Config::PLUGIN_TAG), std::move(msg_fmt)}}, {} };
  } else if (key_node.IsSequence()) {
    if (key_node.size() > 2) {
      return {{}, Errata().error(R"(Value for "{}" key at {} is not a list of two strings as required.)", KEY
          , key_node.Mark())};
    } else if (key_node.size() < 1) {
      return {{}, Errata().error(R"(The list value for "{}" key at {} does not have at least one string as required.)", KEY
          , key_node.Mark())};
    }
    auto && [ tag_fmt, tag_errata ] = cfg.parse_feature(key_node[0], Config::StrType::C);
    if (!tag_errata.is_ok()) {
      tag_errata.info(R"(While parsing tag at {} for "{}" directive at {}.)", key_node[0].Mark(), KEY, drtv_node.Mark());
      return { {}, std::move(tag_errata) };
    }
    auto && [ msg_fmt, msg_errata ] = cfg.parse_feature(key_node[1]);
    if (!tag_errata.is_ok()) {
      tag_errata.info(R"(While parsing message at {} for "{}" directive at {}.)", key_node[1].Mark(), KEY, drtv_node.Mark());
      return { {}, std::move(tag_errata) };
    }
    return { Handle(new self_type(std::move(tag_fmt), std::move(msg_fmt))), {} };
  }
  return { {}, Errata().error(R"(Value for "{}" key at {} is not a string or a list of strings as required.)", KEY, key_node.Mark()) };
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
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override;
  static swoc::Rv<Handle> load(Config & cfg, YAML::Node const& drtv_node, YAML::Node const& key_node);

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
const HookMask With::HOOKS  { MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP }) };

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

  static const HookMask HOOKS; ///< Valid hooks for directive.

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
const HookMask WithTuple::HOOKS { With::HOOKS };

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
    if ((*c._cmp)(ctx, feature)) {
      ctx._feature = feature;
      return c._do->invoke(ctx);
    }
  }
  return {};
}

Errata WithTuple::invoke(Context &ctx) {
  return {};
};

swoc::Rv<Directive::Handle> With::load(Config & cfg, YAML::Node const& drtv_node, YAML::Node const& key_node) {
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
    auto && [ fmt, errata ] = cfg.parse_feature(key_node);

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
    auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg, _ex._feature_type, node)};
    if (cmp_errata.is_ok()) {
      c._cmp = std::move(cmp_handle);
    } else {
      cmp_errata.error(R"(While parsing "{}" key at {}.)", SELECT_KEY, node.Mark());
      return std::move(cmp_errata);
    }

    if (YAML::Node do_node{node[DO_KEY]}; do_node) {
      Config::FeatureRefState ref;
      ref._feature_active_p = true;
      ref._type = _ex._feature_type;
      ref._rxp_group_count = c._cmp->rxp_group_count();
      ref._rxp_line = node.Mark().line;
      auto &&[handle, errata]{cfg.parse_directive(do_node, ref)};
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
    // Everything is fine, update the case load and return.
    _cases.emplace_back(std::move(c));
    return {};
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
      auto &&[fmt, errata]{cfg.parse_feature(child)};
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
      auto &&[do_handle, do_errata]{cfg.parse_directive(do_node)};
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
const HookMask When::HOOKS  { MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP }) };

When::When(Hook hook_idx, Directive::Handle &&directive) : _hook(hook_idx), _directive(std::move
(directive)) {}

// Put the internal directive in the directive array for the specified hook.
Errata When::invoke(Context &ctx) {
  return ctx.on_hook_do(_hook, _directive.get());
}

swoc::Rv<Directive::Handle> When::load(Config& cfg, YAML::Node const& drtv_node, YAML::Node const& key_node) {
  Errata zret;
  if (Hook hook{HookName[key_node.Scalar()]} ; hook != Hook::INVALID) {
    if (YAML::Node do_node{drtv_node[DO_KEY]}; do_node) {
      auto save = cfg._hook;
      cfg._hook = hook;
      auto &&[do_handle, do_errata]{cfg.parse_directive(do_node)};
      cfg._hook = save;
      if (do_errata.is_ok()) {
        cfg.reserve_slot(hook);
        return { Handle{new self_type{hook, std::move(do_handle)}} , {}};
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
  Config::define(When::KEY, When::HOOKS, When::load);
  Config::define(With::KEY, With::HOOKS, With::load);
  Config::define(Do_set_creq_field_default::KEY, Do_set_creq_field_default::HOOKS, Do_set_creq_field_default::load);
  Config::define(Do_remove_creq_field::KEY, Do_remove_creq_field::HOOKS, Do_remove_creq_field::load);
  Config::define(Do_remove_ursp_field::KEY, Do_remove_ursp_field::HOOKS, Do_remove_ursp_field::load);
  Config::define(Do_remove_prsp_field::KEY, Do_remove_prsp_field::HOOKS, Do_remove_prsp_field::load);
  Config::define(Do_set_preq_field::KEY, Do_set_preq_field::HOOKS, Do_set_preq_field::load);
  Config::define(Do_set_preq_field_default::KEY, Do_set_preq_field_default::HOOKS, Do_set_preq_field_default::load);
  Config::define(Do_set_preq_url_host::KEY, Do_set_preq_url_host::HOOKS, Do_set_preq_url_host::load);
  Config::define(Do_set_preq_host::KEY, Do_set_preq_host::HOOKS, Do_set_preq_host::load);
  Config::define(Do_set_ursp_status::KEY, Do_set_ursp_status::HOOKS, Do_set_ursp_status::load);
  Config::define(Do_set_ursp_reason::KEY, Do_set_ursp_reason::HOOKS, Do_set_ursp_reason::load);
  Config::define(Do_set_prsp_body::KEY, Do_set_prsp_body::HOOKS, Do_set_prsp_body::load);
  Config::define(Do_redirect::KEY, Do_redirect::HOOKS, Do_redirect::load, Directive::Options().ctx_storage(sizeof(TextView)));
  Config::define(Do_debug_msg::KEY, Do_debug_msg::HOOKS, Do_debug_msg::load);
  return true;
} ();
} // namespace
