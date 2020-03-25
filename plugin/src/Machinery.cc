/** @file
   Non-core directive implementations.

 * Copyright 2019, Oath Inc.
 * SPDX-License-Identifier: Apache-2.0
*/

#include "txn_box/common.h"

#include <swoc/TextView.h>
#include <swoc/Errata.h>
#include <swoc/ArenaWriter.h>
#include <swoc/BufferWriter.h>
#include <swoc/bwf_base.h>
#include <swoc/bwf_ex.h>

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
Feature Generic::extract() const { return NIL_FEATURE; }
/* ------------------------------------------------------------------------------------ */
class Do_preq_url_host : public Directive {
  using super_type = Directive;
  using self_type = Do_preq_url_host;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  explicit Do_preq_url_host(TextView text) : _host(text) {}

  Errata invoke(Context &ctx) override;
  static Rv<Handle> load( Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node const& key_value);
  std::string _host;
};

const std::string Do_preq_url_host::KEY { "preq-url-host" };
const HookMask Do_preq_url_host::HOOKS { MaskFor({Hook::PREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

Errata Do_preq_url_host::invoke(Context &ctx) {
  Errata zret;
  return zret;
}

swoc::Rv<Directive::Handle> Do_preq_url_host::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  return { Handle{new self_type{key_value.Scalar()}}, {} };
}

/* ------------------------------------------------------------------------------------ */
/** Set the host for the request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_creq_host : public Directive {
  using super_type = Directive; ///< Parent type.
  using self_type = Do_creq_host; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  Do_creq_host(Expr && fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const std::string Do_creq_host::KEY { "creq-host" };
const HookMask Do_creq_host::HOOKS { MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

Do_creq_host::Do_creq_host(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata Do_creq_host::invoke(Context &ctx) {
  TextView host{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.creq_hdr()}; hdr.is_valid()) {
    hdr.host_set(host);
  }
  return {};
}

swoc::Rv<Directive::Handle> Do_creq_host::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (expr.result_type() != STRING) {
    return Error(R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the host for the request.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_preq_host : public Directive {
  using super_type = Directive; ///< Parent type.
  using self_type = Do_preq_host; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  Do_preq_host(Expr && fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name
                        , swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const std::string Do_preq_host::KEY { "preq-host" };
const HookMask Do_preq_host::HOOKS { MaskFor({Hook::PREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

Do_preq_host::Do_preq_host(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata Do_preq_host::invoke(Context &ctx) {
  TextView host{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  if (auto hdr{ctx.preq_hdr()}; hdr.is_valid()) {
    hdr.host_set(host);
  }
  return {};
}

swoc::Rv<Directive::Handle> Do_preq_host::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (expr.result_type() != STRING) {
    return Error(R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return  Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Set the host for remap.
 * This updates both the URL and the "Host" field, if appropriate.
 */
class Do_remap_host : public Directive {
  using super_type = Directive; ///< Parent type.
  using self_type = Do_remap_host; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /** Construct with feature extractor @a fmt.
   *
   * @param fmt Feature for host.
   */
  Do_remap_host(Expr && fmt);

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _fmt; ///< Host feature.
};

const std::string Do_remap_host::KEY { "remap-host" };
const HookMask Do_remap_host::HOOKS { MaskFor(Hook::REMAP) };

Do_remap_host::Do_remap_host(Expr &&fmt) : _fmt(std::move(fmt)) {}

Errata Do_remap_host::invoke(Context &ctx) {
  TextView host{std::get<IndexFor(STRING)>(ctx.extract(_fmt))};
  ts::URL(ctx._remap_info->requestBufp, ctx._remap_info->requestUrl).host_set(host);
  ctx._remap_status = TSREMAP_DID_REMAP;
  return {};
}

swoc::Rv<Directive::Handle> Do_remap_host::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }
  if (expr.result_type() != STRING) {
    return Error(R"(Value for "{}" directive at {} must be a string.)", KEY, drtv_node.Mark());
  }
  return Handle(new self_type(std::move(expr)));
}
/* ------------------------------------------------------------------------------------ */
/** Do the remap.
 */
class Do_apply_remap_rule : public Directive {
  using super_type = Directive; ///< Parent type.
  using self_type = Do_apply_remap_rule; ///< Self reference type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  /** Invoke directive.
   *
   * @param ctx Transaction context.
   * @return Errors, if any.
   */
  Errata invoke(Context &ctx) override;

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load( Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name
                          , swoc::TextView const& arg, YAML::Node const& key_value);
};

const std::string Do_apply_remap_rule::KEY { "apply-remap-rule" };
const HookMask Do_apply_remap_rule::HOOKS { MaskFor(Hook::REMAP) };

Errata Do_apply_remap_rule::invoke(Context &ctx) {
  ctx._remap_status = TSREMAP_DID_REMAP;
  // This is complex because the internal logic is as well. A bit fragile, but this is
  // really only useful as a backwards compatibility fix for pre ATS 9 and should eventually
  // be removed.
  // Copy over the host and port.
  ts::URL replacement_url { ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl };
  ts::URL target_url { ctx._remap_info->requestBufp, ctx._remap_info->mapFromUrl };
  ts::URL request_url { ctx._remap_info->requestBufp, ctx._remap_info->requestUrl };

  in_port_t port = replacement_url.port_get();
  // decanonicalize the port - may need to dig in and see if it was explicitly set.
  if ((port == 80 && replacement_url.scheme() == ts::URL_SCHEME_HTTP) ||
      (port == 443 && replacement_url.scheme() == ts::URL_SCHEME_HTTPS)) {
    port = 0;
  }
  request_url.port_set(port);
  request_url.host_set(replacement_url.host());
  if (ts::HttpRequest{ctx._remap_info->requestBufp, ctx._remap_info->requestHdrp}.method() != "CONNECT"_tv) {
    request_url.scheme_set(replacement_url.scheme());
    // Update the path as needed.
    auto replacement_path { replacement_url.path() };
    auto target_path { target_url.path() };
    auto request_path { request_url.path() };

    // Need to do better - see if Context can provide an ArenaWriter?
    swoc::LocalBufferWriter<(1<<16) - 1> url_w;
    url_w.write(replacement_path);
    if (request_path.size() > target_path.size()) {
      // Always slash separate the replacement from the remnant of the incoming request path.
      if (url_w.size() && url_w.view()[url_w.size()-1] != '/') {
        url_w.write('/');
      }
      // Already have the separating slash, trim it from the target path.
      url_w.write(request_path.substr(target_path.size()).ltrim('/'));
    }
    request_url.path_set(TextView{url_w.view()}.ltrim('/'));
  };

//  TSUrlCopy(ctx._remap_info->requestBufp, ctx._remap_info->requestUrl, ctx._remap_info->requestBufp, ctx._remap_info->mapToUrl);
  return {};
}

swoc::Rv<Directive::Handle> Do_apply_remap_rule::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  return Handle(new self_type);
}
/* ------------------------------------------------------------------------------------ */
class FieldDirective : public Directive {
  using self_type = FieldDirective; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
protected:
  TextView _name; ///< Field name.
  Expr _expr; ///< Value for field.

  /** Base constructor.
   *
   * @param name Name of the field.
   * @param expr Value to assign to the field.
   */
  FieldDirective(TextView const& name, Expr && expr);

  /** Load from configuration.
   *
   * @param cfg Configuration.
   * @param maker Subclass maker.
   * @param key Name of the key identifying the directive.
   * @param name Field name (directive argumnet).
   * @param key_value  Value of the node for @a key.
   * @return An instance of the directive for @a key, or errors.
   */
  static Rv<Handle> load(Config& cfg, std::function<Handle (TextView const& name, Expr && fmt)> const& maker, TextView const& key, TextView const& name, YAML::Node const& key_value);

  /** Invoke the directive.
   *
   * @param ctx Runtime context.
   * @param hdr HTTP header containing the field.
   * @return Errors, if any.
   */
  Errata invoke(Context& ctx, ts::HttpHeader && hdr);

  void apply(Context& ctx, ts::HttpHeader & hdr, TextView const& name);

  /** Get the directive name (key).
   *
   * @return The directive key.
   *
   * Used by subclasses to provide the key for diagnostics.
   */
  virtual swoc::TextView key() const = 0;

  template < typename T > void apply(Context& ctx, ts::HttpHeader && hdr, T const& value) {}

  /// Visitor which performs the assignemnt.
  struct Apply {
    Context & _ctx; ///< Runtime context.
    ts::HttpHeader & _hdr; ///< HTTP Header to modify.
    ts::HttpField _field; ///< Working field.
    TextView const& _name; ///< Field name.

    Apply(Context & ctx, ts::HttpHeader & hdr, TextView const& name) : _ctx(ctx), _hdr(hdr), _field(hdr.field(name)), _name(name) {}

    /// Clear all duplicate fields past @a _field.
    void clear_dups() {
      if (_field.is_valid()) {
        for (auto nf = _field.next_dup(); nf.is_valid();) {
          nf.destroy();
        }
      }
    }

    /// Assigned @a text to a single field, creating as needed.
    /// @return @c true if a field value was changed, @c false if the current value is @a text.
    void assign(TextView const& text) {
      if (_field.is_valid()) {
        if (_field.value() != text) {
          _field.assign(text);
        }
      } else {
        _hdr.field_create(_name).assign(text);
      }
    }

    /// Nil / NULL means destroy the field.
    void operator()(feature_type_for<NIL>) {
      if (_field.is_valid()) {
        this->clear_dups();
        _field.destroy();
      }
    }

    /// Assign the string, clear out any dups.
    void operator()(feature_type_for<STRING>& text) {
      this->assign(text);
      this->clear_dups();
    }

    /// Assign the tuple elements to duplicate fields.
    void operator()(feature_type_for<TUPLE>& t) {
      bool reuse_p = false; // reuse existing fields.
      for (auto const& tf : t) {
        auto text { std::get<STRING>(tf.join(_ctx, ", "_tv)) };
        // skip to next equal field, destroying mismatched fields.
        // once @a _field becomes invalid, it remains in that state.
        while (_field.is_valid() && _field.value() != text) {
          auto tmp = _field.next_dup();
          _field.destroy();
          _field = std::move(tmp);
        }
        this->assign(text);
        _field = _field.next_dup(); // does nothing if @a _field is invalid.
      }
    }

    // Other types, do nothing.
    template<typename T> auto operator()(T&&value) -> EnableForFeatureTypes<T, void> {}
  };

};

FieldDirective::FieldDirective(TextView const &name, Expr &&expr) : _name(name), _expr(std::move(expr)) {}

void FieldDirective::apply(Context & ctx, ts::HttpHeader & hdr, TextView const& name) {
  auto value{ctx.extract(_expr)};
  std::visit(Apply{ctx, hdr, name}, value);
}

Errata FieldDirective::invoke(Context & ctx, ts::HttpHeader && hdr) {
  if (hdr.is_valid()) {
    ts::HttpField field;
    if (_name == ACTIVE_FEATURE_KEY) {
      if (auto g { std::get_if<IndexFor(GENERIC)>(&ctx._active_ext)} ; g) {
        if (TupleIterator::TAG == (*g)->_tag) {
          if (static_cast<TupleIterator*>(*g)->iter_tag() == this->key()) {
            auto & iter = *static_cast<HttpFieldTuple*>(*g);
            auto & field = iter.current();
            if (field.is_valid()) {
              auto value { ctx.extract(_expr)};
              if (ValueTypeOf(value) == NIL) {
                field.destroy();
              } else {
                field.assign(std::get<STRING>(value.join(ctx, ", "_tv)));
              }
            }
            return {};
          }
        }
      }
    } else {
      this->apply(ctx, hdr, _name);
      return {};
    }
    return Errata().error(R"(Failed to find or create field "{}")", _name);
  }
  return Errata().error(R"(Failed to assign field value due to invalid HTTP header.)");
}

auto FieldDirective::load(Config &cfg, std::function<Handle(TextView const &
                         , Expr &&)> const &maker
                         , TextView const &key, TextView const &arg
                         , YAML::Node const &key_value) -> Rv<Handle> {
  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing value for "{}".)", key);
    return std::move(errata);
  }

  auto expr_type = expr.result_type();
  if (expr_type == NO_VALUE) {
    return Error(R"(Directive "{}" must have a value.)", key);
  }
  if (STRING != expr_type && NIL != expr_type && TUPLE != expr_type) {
    return Error(R"(Value for "{}" directive at {} must be a NULL, a string or a list of strings.)", key, key_value.Mark());
  }

  return maker(cfg.localize(arg), std::move(expr));
}

// -- Implementations --

// --
class Do_creq_field : public FieldDirective {
  using self_type = Do_creq_field;
  using super_type = FieldDirective;
public:
  static const std::string KEY; ///< Directive key.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView key() const override { return KEY; }
};

const std::string Do_creq_field::KEY { "creq-field" };
const HookMask Do_creq_field::HOOKS { MaskFor({ Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP }) };

Errata Do_creq_field::invoke(Context &ctx) {
  return this->super_type::invoke(ctx, ctx.creq_hdr());
}

Rv<Directive::Handle> Do_creq_field::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  return super_type::load(cfg, [](TextView const& name, Expr && fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg, key_value);
}

// --
class Do_preq_field : public FieldDirective {
  using self_type = Do_preq_field;
  using super_type = FieldDirective;
public:
  static const std::string KEY; ///< Directive key.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView key() const override { return KEY; }
};

const std::string Do_preq_field::KEY { "preq-field" };
const HookMask Do_preq_field::HOOKS { MaskFor({Hook::PREQ, Hook::PRE_REMAP, Hook::POST_REMAP}) };

Errata Do_preq_field::invoke(Context &ctx) {
  return this->super_type::invoke(ctx, ctx.preq_hdr());
}

Rv<Directive::Handle> Do_preq_field::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  return super_type::load(cfg, [](TextView const& name, Expr && fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg, key_value);
}

// --
class Do_prsp_field : public FieldDirective {
  using self_type = Do_prsp_field;
  using super_type = FieldDirective;
public:
  static const std::string KEY; ///< Directive key.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView key() const override { return KEY; }
};

const std::string Do_prsp_field::KEY { "prsp-field" };
const HookMask Do_prsp_field::HOOKS { MaskFor(Hook::PRSP) };

Errata Do_prsp_field::invoke(Context &ctx) {
  return this->super_type::invoke(ctx, ctx.prsp_hdr());
}

Rv<Directive::Handle> Do_prsp_field::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  return super_type::load(cfg, [](TextView const& name, Expr && fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg, key_value);
}

// --
class Do_ursp_field : public FieldDirective {
  using self_type = Do_ursp_field;
  using super_type = FieldDirective;
public:
  static const std::string KEY; ///< Directive key.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  using super_type::super_type; // Inherit super_type constructors.
  TextView key() const override { return KEY; }
};

const std::string Do_ursp_field::KEY { "ursp-field" };
const HookMask Do_ursp_field::HOOKS { MaskFor(Hook::URSP) };

Errata Do_ursp_field::invoke(Context &ctx) {
  return this->super_type::invoke(ctx, ctx.ursp_hdr());
}

Rv<Directive::Handle> Do_ursp_field::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  return super_type::load(cfg, [](TextView const& name, Expr && fmt) -> Handle { return Handle(new self_type(name, std::move(fmt))); }, KEY, arg, key_value);
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
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Expr _status_fmt; ///< Return status.

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

Rv<Directive::Handle> Do_set_ursp_status::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }
  auto self = new self_type;
  Handle handle(self);

  auto expr_type = expr.result_type();
  if (expr_type == INTEGER) {
    feature_type_for<INTEGER> status = 0; // BROKEN - need to fix up for new literal and extraction support.
    if (status < 100 || status > 599) {
      return Error(R"(Status "{}" at {} is not a positive integer 100..599 as required.)", key_value.Scalar(), key_value.Mark());
    }
    self->_status = static_cast<TSHttpStatus>(status);
  } else if (expr_type == STRING) {
    self->_status_fmt = std::move(expr);
  } else {
    return Error(R"(Status "{}" at {} is not an integer nor string as required.)", key_value.Scalar(), key_value.Mark());
  }
  return std::move(handle);
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
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Expr _fmt; ///< Reason phrase.

  Do_set_ursp_reason() = default;
};

const std::string Do_set_ursp_reason::KEY { "set-ursp-reason" };
const HookMask Do_set_ursp_reason::HOOKS { MaskFor({Hook::URSP}) };

Errata Do_set_ursp_reason::invoke(Context &ctx) {
  auto value = ctx.extract(_fmt);
  ctx._txn.ursp_hdr().reason_set(std::get<IndexFor(STRING)>(value));
  return {};
}

Rv<Directive::Handle> Do_set_ursp_reason::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }
  if (STRING != expr.result_type()) {
    return Error(R"(The value for "{}" must be a string.)", KEY, drtv_node.Mark());
  }
  auto self = new self_type;
  Handle handle(self);

  self->_fmt = std::move(expr);

  return std::move(handle);
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
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  TSHttpStatus _status = TS_HTTP_STATUS_NONE; ///< Return status is literal, 0 => extract at runtime.
  Expr _expr; ///< Reason phrase.

  Do_set_prsp_body() = default;
};

const std::string Do_set_prsp_body::KEY { "set-prsp-body" };
const HookMask Do_set_prsp_body::HOOKS { MaskFor({Hook::URSP}) };

Errata Do_set_prsp_body::invoke(Context &ctx) {
  auto value = ctx.extract(_expr);
  ctx._txn.error_body_set(std::get<IndexFor(STRING)>(value), "text/hmtl"_tv);
  return {};
}

Rv<Directive::Handle> Do_set_prsp_body::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }
  if (STRING != expr.result_type()) {
    return Error(R"(The value for "{}" must be a string.)", KEY, drtv_node.Mark());
  }
  auto self = new self_type;
  Handle handle(self);

  self->_expr = std::move(expr);

  return std::move(handle);
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

  /** Load from YAML node.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param name Name from key node tag.
   * @param arg Arg from key node tag.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

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
  Feature location = _fg.extract(ctx, _location_idx);
  ctx.commit(location);
  // Remember where it is so the fix up can find it.
  auto view = static_cast<TextView*>(ctx.storage_for(this).data());
  *view = std::get<IndexFor(STRING)>(location);

  // Set the status to prevent the upstream request.
  if (_status) {
    ctx._txn.status_set(static_cast<TSHttpStatus>(_status));
  } else {
    Feature value = _fg.extract(ctx, _status_idx);
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

  #if 0
  if (ex._expr.is_literal()) {
    TextView src{ex._expr.literal()}, parsed;
    auto status = swoc::svtou(src, &parsed);
    if (parsed.size() != src.size() || status < 100 || status > 599) {
      return Errata().error(R"({} "{}" at {} is not a positive integer 100..599 as required.)", STATUS_KEY, src);
    }
    _status = status;
  } else {
    if (ex._expr._result_type != STRING && ex._expr._result_type != INTEGER) {
      return Errata().error(R"({} is not an integer nor string as required.)", STATUS_KEY);
    }
  }
  #endif
  return {};
}

Rv<Directive::Handle> Do_redirect::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  Handle handle{new self_type{cfg}};
  Errata errata;
  auto self = static_cast<self_type *>(handle.get());
  if (key_value.IsScalar()) {
    errata = self->_fg.load_as_tuple(cfg, key_value, {{LOCATION_KEY, FeatureGroup::REQUIRED}});
  } else if (key_value.IsSequence()) {
    errata = self->_fg.load_as_tuple(cfg, key_value, { { STATUS_KEY, FeatureGroup::REQUIRED } , { LOCATION_KEY, FeatureGroup::REQUIRED } });
  } else if (key_value.IsMap()) {
    Errata errata = self->_fg.load(cfg, key_value, { { LOCATION_KEY, FeatureGroup::REQUIRED }, { STATUS_KEY }, { REASON_KEY }, { BODY_KEY } });
  } else {
    return Error(R"(Value for "{}" key at {} is must be a scalar, a 2-tuple, or a map and is not.)", KEY, key_value.Mark());
  }
  if (! errata.is_ok()) {
    errata.info(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
    return {{}, std::move(errata)};
  }

  self->_reason_idx = self->_fg.exf_index(REASON_KEY);
  self->_body_idx = self->_fg.exf_index(BODY_KEY);
  self->_location_idx = self->_fg.exf_index(LOCATION_KEY);
  errata.note(self->load_status());

  if (! errata.is_ok()) {
    errata.info(R"(While parsing value at {} in "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
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
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _tag;
  Expr _msg;

  Do_debug_msg(Expr && tag, Expr && msg);
};

const std::string Do_debug_msg::KEY { "debug" };
const HookMask Do_debug_msg::HOOKS { MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP }) };

Do_debug_msg::Do_debug_msg(Expr &&tag, Expr &&msg) : _tag(std::move(tag)), _msg(std::move(msg)) {}

Errata Do_debug_msg::invoke(Context &ctx) {
  TextView tag = std::get<IndexFor(STRING)>(ctx.extract(_tag));
  auto msg = ctx.extract(_msg);
  swoc::ArenaWriter w(*(ctx._arena));
  w.print("{}", msg);
  TSDebug(tag.data(), "%.*s", static_cast<int>(w.view().size()), w.view().data());
  return {};
}

Rv<Directive::Handle> Do_debug_msg::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  if (key_value.IsScalar()) {
    auto && [ msg_fmt, msg_errata ] = cfg.parse_expr(key_value);
    if (! msg_errata.is_ok()) {
      msg_errata.info(R"(While parsing message at {} for "{}" directive at {}.)", key_value.Mark(), KEY, drtv_node.Mark());
      return { {}, std::move(msg_errata)};
    }
    return { Handle{new self_type{Expr{Config::PLUGIN_TAG}, std::move(msg_fmt)}}, {} };
  } else if (key_value.IsSequence()) {
    if (key_value.size() > 2) {
      return Error(R"(Value for "{}" key at {} is not a list of two strings as required.)", KEY, key_value.Mark());
    } else if (key_value.size() < 1) {
      return Error(R"(The list value for "{}" key at {} does not have at least one string as required.)", KEY, key_value.Mark());
    }
    auto && [ tag_fmt, tag_errata ] = cfg.parse_expr(key_value[0]);
    if (!tag_errata.is_ok()) {
      tag_errata.info(R"(While parsing tag at {} for "{}" directive at {}.)", key_value[0].Mark(), KEY, drtv_node.Mark());
      return { {}, std::move(tag_errata) };
    }
    auto && [ msg_fmt, msg_errata ] = cfg.parse_expr(key_value[1]);
    if (!tag_errata.is_ok()) {
      tag_errata.info(R"(While parsing message at {} for "{}" directive at {}.)", key_value[1].Mark(), KEY, drtv_node.Mark());
      return { {}, std::move(tag_errata) };
    }
    return { Handle(new self_type(std::move(tag_fmt), std::move(msg_fmt))), {} };
  }
  return Error(R"(Value for "{}" key at {} is not a string or a list of strings as required.)", KEY, key_value.Mark());
}

/* ------------------------------------------------------------------------------------ */
class QueryDirective {
public:
  Errata invoke(Context &ctx, Expr& fmt, ts::URL url, TextView key);
};

Errata QueryDirective::invoke(Context &ctx, Expr& fmt, ts::URL url, TextView key) {
  auto feature = ctx.extract(fmt);
  if (key.empty()) {
    url.query_set(std::get<IndexFor(STRING)>(feature));
    return {};
  }

  // Need remnant space therefore this needs to be permanent.
  ctx.commit(feature);

  swoc::ArenaWriter aw{*ctx._arena};
  TextView sep; // last separator found.
  TextView::size_type offset = 0;
  auto query { url.query() };
  // Check each parameter for matching @a _arg.
  while (offset < query.size()) {
    if (query.substr(offset).starts_with(key) &&
        ((query.size() == offset + key.size()) ||
         ("=&;"_tv.find(query[key.size()]) != TextView::npos ))) {
      aw.write(sep).write(query.remove_prefix(offset)); // write out prior query section
      if (!is_nil(feature)) {
        Feature value = car(feature);
        if (is_nil(value)) {
          aw.write(key);
        } else {
          aw.print("{}={}", key, value);
        }
      }
      query.remove_prefix(offset).ltrim_if([](char c){return c != '&' && c != ';';});
      sep = query.take_prefix(1);
      offset = 0;
      feature = cdr(feature);
    } else {
      offset = query.find_first_of(";&"_sv, offset+1);
    }
  }
  if (query) {
    aw.write(sep).write(query);
  }
  while (! is_nil(feature)) {
    Feature value = car(feature);
    if (is_nil(value)) {
      aw.write(key);
    } else {
      aw.print("{}={}", key, value);
    }
    feature = cdr(feature);
  }
  return {};
}

class Do_set_creq_query : public Directive, QueryDirective {
  using self_type = Do_set_creq_query;
  using super_type = Directive;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView arg, YAML::Node const& key_value);

protected:
  TextView _arg;
  Expr _expr;

  Do_set_creq_query(TextView arg, Expr && fmt) : _arg(arg), _expr(std::move(fmt)) {}
};

const std::string Do_set_creq_query::KEY { "set-creq-query" };
const HookMask Do_set_creq_query::HOOKS { MaskFor({Hook::CREQ, Hook::PRE_REMAP}) };

Errata Do_set_creq_query::invoke(Context &ctx) {
  return this->QueryDirective::invoke(ctx, _expr, ctx.creq_hdr().url(), _arg);
}

Rv<Directive::Handle> Do_set_creq_query::load(Config &cfg, YAML::Node const &drtv_node
                                             , swoc::TextView const &name, swoc::TextView arg
                                             , YAML::Node const &key_value) {

  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }

  return Handle(new self_type(cfg.localize(arg), std::move(expr)));
}

class Do_remap_query : public Directive, QueryDirective {
  using self_type = Do_remap_query;
  using super_type = Directive;
public:
  static const std::string KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override;
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView arg, YAML::Node const& key_value);

protected:
  TextView _arg;
  Expr _expr;

  Do_remap_query(TextView arg, Expr && fmt) : _arg(arg), _expr(std::move(fmt)) {}
};

const std::string Do_remap_query::KEY { "remap-query" };
const HookMask Do_remap_query::HOOKS { MaskFor({Hook::CREQ, Hook::REMAP}) };

Rv<Directive::Handle> Do_remap_query::load(Config &cfg, YAML::Node const &drtv_node
                                              , swoc::TextView const &name, swoc::TextView arg
                                              , YAML::Node const &key_value) {

  auto && [ expr, errata ] { cfg.parse_expr(key_value) };
  if (! errata.is_ok()) {
    errata.info(R"(While parsing "{}" directive at {}.)", KEY, drtv_node.Mark());
    return std::move(errata);
  }

  return Handle(new self_type(cfg.localize(arg), std::move(expr)));
}

Errata Do_remap_query::invoke(Context &ctx) {
//  ctx._remap_status = TSREMAP_DID_REMAP;
  return this->QueryDirective::invoke(ctx, _expr, ts::URL(ctx._remap_info->requestBufp, ctx._remap_info->requestUrl), _arg);
}
/* ------------------------------------------------------------------------------------ */
/// Set the cache key.
class Do_cache_key : public Directive {
  using self_type = Do_cache_key; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _fmt; ///< Cache key.

  Do_cache_key(Expr && fmt) : _fmt(std::move(fmt)) {}
};

const std::string Do_cache_key::KEY { "cache-key" };
const HookMask Do_cache_key::HOOKS { MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP}) };

Errata Do_cache_key::invoke(Context &ctx) {
  auto value = ctx.extract(_fmt);
  ctx._txn.cache_key_assign(std::get<IndexFor(STRING)>(value));
  return {};
}

Rv<Directive::Handle> Do_cache_key::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto &&[fmt, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }

  return std::move(Handle(new self_type(std::move(fmt))));
}

/* ------------------------------------------------------------------------------------ */
/// Set a transaction configuration variable override.
class Do_txn_conf : public Directive {
  using self_type = Do_txn_conf; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _expr; ///< Value for override.
  ts::TxnConfigVar *_var = nullptr;

  Do_txn_conf(Expr && fmt, ts::TxnConfigVar * var) : _expr(std::move(fmt)), _var(var) {}
};

const std::string Do_txn_conf::KEY { "txn-conf" };
const HookMask Do_txn_conf::HOOKS { MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PREQ}) };

Errata Do_txn_conf::invoke(Context &ctx) {
  auto value = ctx.extract(_expr);
  if (value.index() == IndexFor(INTEGER)) {
    ctx._txn.override_assign(*_var, std::get<IndexFor(INTEGER)>(value));
  } else if (value.index() == IndexFor(BOOLEAN)) {
    ctx._txn.override_assign(*_var, std::get<IndexFor(BOOLEAN)>(value) ? 1 : 0);
  } else if (value.index() == IndexFor(STRING)) {
    ctx._txn.override_assign(*_var, std::get<IndexFor(STRING)>(value));
  }
  return {};
}

Rv<Directive::Handle> Do_txn_conf::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto txn_var = ts::HttpTxn::find_override(arg);
  if (! txn_var) {
    return Error(R"("{}" is not recognized as an overridable transaction configuration variable.)", arg);
  }
  if (txn_var->type() != TS_RECORDDATATYPE_INT && txn_var->type() != TS_RECORDDATATYPE_STRING) {
    return Error(R"("{}" is of type "{}" which is not currently supported.)", arg, ts::TSRecordDataTypeNames[txn_var->type()]);
  }
  auto &&[fmt, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }

  return std::move(Handle(new self_type(std::move(fmt), txn_var)));
}

/* ------------------------------------------------------------------------------------ */
/// Set the address for the upstream.
class Do_upstream_addr : public Directive {
  using self_type = Do_upstream_addr; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _expr; ///< Address.
  ts::TxnConfigVar *_var = nullptr;

  Do_upstream_addr(Expr && expr) : _expr(std::move(expr)) {}
};

const std::string Do_upstream_addr::KEY { "upstream-addr" };
const HookMask Do_upstream_addr::HOOKS { MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PREQ}) };

Errata Do_upstream_addr::invoke(Context &ctx) {
  auto value = ctx.extract(_expr);
  if (value.index() == IndexFor(IP_ADDR)) {
    ctx._txn.set_upstream_addr(std::get<IndexFor(IP_ADDR)>(value));
  }
  return {};
}

Rv<Directive::Handle> Do_upstream_addr::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto &&[expr, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }

  if (IP_ADDR != expr.result_type()) {
    return Error(R"(Value for "{}" must be an IP address.)");
  }

  return std::move(Handle(new self_type(std::move(expr))));
}
/* ------------------------------------------------------------------------------------ */
/// Set a transaction local variable.
class Do_var : public Directive {
  using self_type = Do_var; ///< Self reference type.
  using super_type = Directive; ///< Parent type.
public:
  static const std::string KEY; ///< Directive name.
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context & ctx) override; ///< Runtime activation.

  /** Load from YAML configuration.
   *
   * @param cfg Configuration data.
   * @param drtv_node Node containing the directive.
   * @param key_value Value for directive @a KEY
   * @return A directive, or errors on failure.
   */
  static Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  TextView _name; ///< Variable name.
  Expr _value; ///< Value for variable.

  Do_var(TextView const& arg, Expr && value) : _name(arg), _value(std::move(value)) {}
};

const std::string Do_var::KEY { "var" };
const HookMask Do_var::HOOKS { MaskFor({Hook::CREQ, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP, Hook::PREQ}) };

Errata Do_var::invoke(Context &ctx) {
  ctx.store_txn_var(_name, ctx.extract(_value));
  return {};
}

Rv<Directive::Handle> Do_var::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  auto &&[fmt, errata]{cfg.parse_expr(key_value)};
  if (! errata.is_ok()) {
    return std::move(errata);
  }

  return std::move(Handle(new self_type(cfg.localize(arg), std::move(fmt))));
}
/* ------------------------------------------------------------------------------------ */
/** @c with directive.
 *
 * This is a core directive that has lots of special properties.
 */
class With : public Directive {
  using super_type = Directive;
  using self_type = With;
public:
  static const std::string KEY;
  static const std::string SELECT_KEY;
  static const std::string FOR_EACH_KEY;
  static const HookMask HOOKS; ///< Valid hooks for directive.

  Errata invoke(Context &ctx) override;
  static swoc::Rv<Handle> load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value);

protected:
  Expr _ex; ///< Extractor format.
  Directive::Handle _do; ///< Explicit actions.

  union {
    uint32_t all = 0;
    struct {
      unsigned for_each_p : 1; ///< Direct action is per tuple element.
    } f;
  } opt;

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
const std::string With::FOR_EACH_KEY { "for-each" };
const HookMask With::HOOKS  { MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::POST_REMAP, Hook::REMAP }) };

Errata With::invoke(Context &ctx) {
  Feature feature { ctx.extract(_ex) };
  Feature save { ctx._active };
  ctx._active = feature;

  if (_do) {
    if (opt.f.for_each_p) {
      ctx._active_ext = feature;
      while (! is_nil(feature)) {
        ctx._active = car(feature);
        _do->invoke(ctx);
        cdr(feature);
      }
      clear(feature);
      ctx._active_ext = NIL_FEATURE;
      // Iteration can potentially modify the extracted feature value, so if there are comparisons
      // reset the feature.
      if (! _cases.empty()) {
        ctx._active = feature = ctx.extract(_ex);
      }
    } else {
      _do->invoke(ctx);
    }
  }

  for ( auto const& c : _cases ) {
    if (! c._cmp || (*c._cmp)(ctx, feature)) {
      return c._do ? c._do->invoke(ctx) : Errata{};
    }
  }
  // Need to restore to previous state if nothing matched.
  clear(ctx._active);
  ctx._active = save;
  return {};
}

swoc::Rv<Directive::Handle> With::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  // Need to parse this first, so the feature type can be determined.
  auto && [ expr, errata ] = cfg.parse_expr(key_value);

  if (!errata.is_ok()) {
    return std::move(errata);
  }

  self_type * self = new self_type;
  Handle handle(self); // for return, and cleanup in case of error.
  self->_ex = std::move(expr);
  auto f_scope = cfg.feature_scope(self->_ex.result_type());

  YAML::Node select_node { drtv_node[SELECT_KEY] };
  if (select_node) {
    if (select_node.IsMap()) {
      errata = self->load_case(cfg, select_node);
      if (!errata.is_ok()) {
        return std::move(errata);
      }
    } else if (select_node.IsSequence()) {
      for (YAML::Node child : select_node) {
        errata = (self->load_case(cfg, child));
        if (!errata.is_ok()) {
          errata.info(R"(While loading "{}" directive at {} in "{}" at {}.)", KEY, drtv_node.Mark()
                       , SELECT_KEY, select_node.Mark());
          return std::move(errata);
        }
      }
    } else {
      return Error(R"(The value for "{}" at {} in "{}" directive at {} is not a list or object.")", SELECT_KEY, select_node.Mark(), KEY, drtv_node.Mark());
    }
  }

  YAML::Node do_node { drtv_node[DO_KEY] };
  YAML::Node for_each_node { drtv_node[FOR_EACH_KEY]};
  if (do_node && for_each_node) {
    return Error(R"("{}" directive cannot have both "{}" and "{}" as keys - {}.)", DO_KEY, FOR_EACH_KEY, drtv_node.Mark());
  } else if (do_node) {
    auto &&[do_handle, errata]{cfg.parse_directive(do_node)};
    if (errata.is_ok()) {
      self->_do = std::move(do_handle);
    } else {
      errata.info(R"(While parsing "{}" key at {} in selection case at {}.)", DO_KEY, do_node.Mark(), drtv_node.Mark());
      return std::move(errata);
    }
  } else if (for_each_node) {
    auto &&[fe_handle, errata]{cfg.parse_directive(for_each_node)};
    if (errata.is_ok()) {
      self->_do = std::move(fe_handle);
      self->opt.f.for_each_p = true;
    } else {
      errata.info(R"(While parsing "{}" key at {} in selection case at {}.)", FOR_EACH_KEY, for_each_node.Mark(), drtv_node.Mark());
      return std::move(errata);
    }
  }
  return std::move(handle);
}

Errata With::load_case(Config & cfg, YAML::Node node) {
  if (node.IsMap()) {
    Case c;
    YAML::Node do_node { node[DO_KEY]};
    auto n_count = node.size();
    // It's allowed to have no comparison, which is either an empty map or only a DO key.
    // In that case the comparison always matches.
    if (node.size() > 1 || (node.size() == 1 && !do_node)) {
      auto f_scope = cfg.feature_scope(_ex.result_type());
      auto &&[cmp_handle, cmp_errata]{Comparison::load(cfg,  node)};
      if (cmp_errata.is_ok()) {
        c._cmp = std::move(cmp_handle);
      } else {
        return std::move(cmp_errata);
      }
    }

    if (do_node) {
      auto c_scope = cfg.capture_scope((c._cmp ? c._cmp->rxp_group_count() : 0), node.Mark().line);
      auto &&[handle, errata]{cfg.parse_directive(do_node)};
      if (errata.is_ok()) {
        c._do = std::move(handle);
      } else {
        errata.info(R"(While parsing "{}" key at {} in selection case at {}.)", DO_KEY
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
  return Error(R"(The value at {} for "{}" is not an object as required.")", node.Mark(), SELECT_KEY);
}

/* ------------------------------------------------------------------------------------ */
const std::string When::KEY { "when" };
const HookMask When::HOOKS  { MaskFor({Hook::CREQ, Hook::PREQ, Hook::URSP, Hook::PRSP, Hook::PRE_REMAP, Hook::REMAP, Hook::POST_REMAP }) };

When::When(Hook hook_idx, Directive::Handle &&directive) : _hook(hook_idx), _directive(std::move
(directive)) {}

// Put the internal directive in the directive array for the specified hook.
Errata When::invoke(Context &ctx) {
  return ctx.on_hook_do(_hook, _directive.get());
}

swoc::Rv<Directive::Handle> When::load(Config& cfg, YAML::Node const& drtv_node, swoc::TextView const& name, swoc::TextView const& arg, YAML::Node const& key_value) {
  Errata zret;
  if (Hook hook{HookName[key_value.Scalar()]} ; hook != Hook::INVALID) {
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
                   , do_node.Mark(), KEY, key_value.Mark());
      }
    } else {
      zret.error(R"(The required "{}" key was not found in the "{}" directive at {}.")", DO_KEY, KEY
                 , drtv_node.Mark());
    }
  } else {
    zret.error(R"(Invalid hook name "{}" in "{}" directive at {}.)", key_value.Scalar(), When::KEY
               , key_value.Mark());
  }
  return {{}, std::move(zret)};
}

/* ------------------------------------------------------------------------------------ */

namespace {
[[maybe_unused]] bool INITIALIZED = [] () -> bool {
  Config::define(When::KEY, When::HOOKS, When::load);
  Config::define(With::KEY, With::HOOKS, With::load);
  Config::define(Do_creq_field::KEY, Do_creq_field::HOOKS, Do_creq_field::load);
  Config::define(Do_preq_field::KEY, Do_preq_field::HOOKS, Do_preq_field::load);
  Config::define(Do_prsp_field::KEY, Do_prsp_field::HOOKS, Do_prsp_field::load);
  Config::define(Do_ursp_field::KEY, Do_ursp_field::HOOKS, Do_ursp_field::load);
  Config::define(Do_preq_url_host::KEY, Do_preq_url_host::HOOKS, Do_preq_url_host::load);
  Config::define(Do_creq_host::KEY, Do_creq_host::HOOKS, Do_creq_host::load);
  Config::define(Do_preq_host::KEY, Do_preq_host::HOOKS, Do_preq_host::load);
  Config::define(Do_remap_host::KEY, Do_remap_host::HOOKS, Do_remap_host::load);
  Config::define(Do_apply_remap_rule::KEY, Do_apply_remap_rule::HOOKS, Do_apply_remap_rule::load);
  Config::define(Do_set_ursp_status::KEY, Do_set_ursp_status::HOOKS, Do_set_ursp_status::load);
  Config::define(Do_set_ursp_reason::KEY, Do_set_ursp_reason::HOOKS, Do_set_ursp_reason::load);
  Config::define(Do_set_prsp_body::KEY, Do_set_prsp_body::HOOKS, Do_set_prsp_body::load);
  Config::define(Do_remap_query::KEY, Do_remap_query::HOOKS, Do_remap_query::load);
  Config::define(Do_cache_key::KEY, Do_cache_key::HOOKS, Do_cache_key::load);
  Config::define(Do_txn_conf::KEY, Do_txn_conf::HOOKS, Do_txn_conf::load);
  Config::define(Do_redirect::KEY, Do_redirect::HOOKS, Do_redirect::load, Directive::Options().ctx_storage(sizeof(TextView)));
  Config::define(Do_upstream_addr::KEY, Do_upstream_addr::HOOKS, Do_upstream_addr::load);
  Config::define(Do_debug_msg::KEY, Do_debug_msg::HOOKS, Do_debug_msg::load);
  Config::define(Do_var::KEY, Do_var::HOOKS, Do_var::load);
  return true;
} ();
} // namespace
