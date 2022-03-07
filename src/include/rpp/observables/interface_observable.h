// MIT License
// 
// Copyright (c) 2022 Aleksey Loginov
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <rpp/fwd.h>
#include <rpp/subscriber.h>
#include <rpp/utils/type_traits.h>

#include <type_traits>

namespace rpp
{
template<typename Type>
struct virtual_observable
{
    static_assert(std::is_same_v<std::decay_t<Type>, Type>, "Type of observable should be decayed");

    virtual              ~virtual_observable() = default;
    virtual subscription subscribe(const dynamic_subscriber<Type>& subscriber) const noexcept = 0;
};

template<typename Type, typename SpecificObservable>
struct interface_observable : public virtual_observable<Type>
{
private:
    template<typename NewType,
             typename OperatorFn>
    static constexpr bool is_callable_returns_subscriber_of_same_type_v = std::is_same_v<Type, utils::extract_subscriber_type_t<std::invoke_result_t<OperatorFn, dynamic_subscriber<NewType>>>>;
public:
    template<typename NewType,
             typename OperatorFn>
    auto lift(OperatorFn&& op) const &
    {
        return lift_impl<NewType>(std::forward<OperatorFn>(op), CastThis());
    }

    template<typename NewType,
             typename OperatorFn>
    auto lift(OperatorFn&& op) &&
    {
        return lift_impl<NewType>(std::forward<OperatorFn>(op), MoveThis());
    }

    template<typename OperatorFn,
             typename SubscriberType = utils::function_argument_t<OperatorFn>,
             typename NewType = utils::extract_subscriber_type_t<SubscriberType>>
    auto lift(OperatorFn&& op) const &
    {
        return lift_impl<NewType>(std::forward<OperatorFn>(op), CastThis());
    }

    template<typename OperatorFn,
             typename SubscriberType = utils::function_argument_t<OperatorFn>,
             typename NewType = utils::extract_subscriber_type_t<SubscriberType>>
    auto lift(OperatorFn&& op) &&
    {
        return lift_impl<NewType>(std::forward<OperatorFn>(op), MoveThis());
    }

private:
    const SpecificObservable& CastThis() const
    {
        return *static_cast<const SpecificObservable*>(this);
    }

    SpecificObservable&& MoveThis()
    {
        return std::move(*static_cast<SpecificObservable*>(this));
    }

    template<typename NewType,
             typename OperatorFn,
             typename FwdThis>
    static auto lift_impl(OperatorFn&& op, FwdThis&& _this)
    {
        static_assert(is_callable_returns_subscriber_of_same_type_v<NewType, OperatorFn>, "OperatorFn should return subscriber");

        return rpp::make_specific_observable<NewType>([new_this = std::forward<FwdThis>(_this), op = std::forward<OperatorFn>(op)](auto&& subscriber)
        {
            new_this.subscribe(op(std::forward<decltype(subscriber)>(subscriber)));
        });
    }
};
} // namespace rpp