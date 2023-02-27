//                  ReactivePlusPlus library
//
//          Copyright Aleksey Loginov 2022 - present.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          https://www.boost.org/LICENSE_1_0.txt)
//
// Project home: https://github.com/victimsnino/ReactivePlusPlus
//

#pragma once

#include <rpp/observers/fwd.hpp>

#include <exception>

namespace rpp
{
template<constraint::decayed_type Type>
struct interface_observer
{
    virtual ~interface_observer() = default;

    virtual void on_next(const Type& v) const = 0;
    virtual void on_next(Type&& v) const = 0;
    virtual void on_error(const std::exception_ptr& err) const = 0;
    virtual void on_completed() const = 0;
};
} // namespace rpp