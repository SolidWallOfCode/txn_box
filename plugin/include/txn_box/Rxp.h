/** @file
 * Regular expression support.
 *
 * Copyright 2019 Oath, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <memory>
#include <bitset>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <swoc/TextView.h>
#include <swoc/Errata.h>

/** Regular expression support.
 *
 * This is split out from @c Comparison because regular expressions will be used in additional
 * situations. It is non-copyable because it is basically a wrapper on a non-shared PCRE code
 * block and it seems silly to have a handle to what is effectively a handle. Aggregrating classes
 * can deal with it the same way as a @c std::unique_ptr.
 */
class Rxp {
  using self_type = Rxp; ///< Self reference type.
  /// Cleanup for compiled regular expressions.
  struct PCRE_Deleter {
    void operator()(pcre2_code* ptr) { pcre2_code_free(ptr); }
  };
  /// Handle for compiled regular expression.
  using RxpHandle = std::unique_ptr<pcre2_code, PCRE_Deleter>;

public:
  Rxp() = default;
  Rxp(self_type const&) = delete;
  Rxp(self_type && that) : _rxp(std::move(that._rxp)) {}
  self_type & operator = (self_type const&) = delete;

  /** Apply the regular expression.
   *
   * @param text Subject for application.
   * @param match Match data.
   * @return The match result - negative for failure, 0 for not match, or largest capture group matched.
   *
   * @a match must be provided externally and must be of sufficient length.
   *
   * @see capture_count
   */
  int operator()(swoc::TextView text, pcre2_match_data* match) const;

  size_t capture_count() const;

  /// Regular expression compile time opotions.
  enum Option {
    OPT_NULL, ///< Nothing.
    OPT_NOCASE, ///< Case insensitive.
  };

  /// A set of options.
  using OptionGroup = std::bitset<2>;

  /** Create a regular expression instance from @a str.
   *
   * @param str Regular expressions.
   * @param options Compile time options as a list of @c Option values.
   * @return An instance if successful, errors if not.
   */
  static swoc::Rv<self_type> parse(swoc::TextView const& str, std::initializer_list<Option> const& options);

  /** Create a regular expression instance from @a str.
   *
   * @param str Regular expressions.
   * @param options Compile time options as a bit mask.
   * @return An instance if successful, errors if not.
   */
  static swoc::Rv<self_type> parse(swoc::TextView const& str, OptionGroup const& options = OptionGroup{});
protected:
  RxpHandle _rxp; /// Compiled regular expression.

  /// Internal constructor used by @a parse.
  Rxp(pcre2_code* rxp) : _rxp(rxp) {}
};
