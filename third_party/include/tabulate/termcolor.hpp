//!
//! termcolor
//! ~~~~~~~~~
//!
//! termcolor is a header-only c++ library for printing colored messages
//! to the terminal. Written just for fun with a help of the Force.
//!
//! :copyright: (c) 2013 by Ihor Kalnytskyi
//! :license: BSD, see LICENSE for details
//!

#ifndef TABULATE_TERMCOLOR_COMPAT_HPP_
#define TABULATE_TERMCOLOR_COMPAT_HPP_

// Keep tabulate and indicators on a single termcolor implementation. Both
// upstream vendored copies use the global termcolor namespace, and the two
// revisions cannot be included independently without include-order-dependent
// behavior.
#include <indicators/termcolor.hpp>

#endif // TABULATE_TERMCOLOR_COMPAT_HPP_
