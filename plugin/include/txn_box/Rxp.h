/** @file
 * Regular expression support.
 *
 * Copyright 2019, Oath Inc.
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

  /// @return The number of capture groups in the expression.
  size_t capture_count() const;

  /// Regular expression options.
  union Options {
    unsigned int all; ///< All of the flags.
    struct {
      unsigned int nc : 1; ///< Case insensitive
    } f;
  };

  /** Create a regular expression instance from @a str.
   *
   * @param str Regular expressions.
   * @param options Compile time options.
   * @return An instance if successful, errors if not.
   */
  static swoc::Rv<self_type> parse(swoc::TextView const& str, Options const& options);

protected:
  RxpHandle _rxp; /// Compiled regular expression.

  /// Internal constructor used by @a parse.
  Rxp(pcre2_code* rxp) : _rxp(rxp) {}
};
