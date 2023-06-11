//                  ReactivePlusPlus library
//
//          Copyright Aleksey Loginov 2023 - present.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          https://www.boost.org/LICENSE_1_0.txt)
//
// Project home: https://github.com/victimsnino/ReactivePlusPlus
//

#pragma once

#include <rpp/defs.hpp>
#include <rpp/observers/fwd.hpp>
#include <rpp/observers/dynamic_observer.hpp>
#include <rpp/disposables/base_disposable.hpp>
#include <rpp/disposables/disposable_wrapper.hpp>
#include <rpp/utils/functors.hpp>
#include <rpp/utils/exceptions.hpp>
#include <rpp/utils/utils.hpp>

#include <exception>
#include <stdexcept>
#include <type_traits>

namespace rpp::details
{
class upstream_disposable
{
protected:
    void set_upstream_impl(const disposable_wrapper& d)
    {
        m_upstream.dispose();
        m_upstream = d;
    }

    void dispose_impl() const
    {
        m_upstream.dispose();
    }

private:
    disposable_wrapper m_upstream{};
};
class external_disposable_strategy : private upstream_disposable
{
public:
    explicit external_disposable_strategy(disposable_wrapper disposable) : m_external_disposable(std::move(disposable)) {}

    void set_upstream(const disposable_wrapper& d)
    {
        upstream_disposable::set_upstream_impl(d);
        m_external_disposable.add(d.get_original());
    }

    bool is_disposed() const noexcept
    {
        return m_external_disposable.is_disposed();
    }

    void dispose() const
    {
        m_external_disposable.dispose();
        upstream_disposable::dispose_impl();
    }

private:
    disposable_wrapper             m_external_disposable{};
};

class local_disposable_strategy : private upstream_disposable
{
public:
    local_disposable_strategy() = default;
    void set_upstream(const disposable_wrapper& d)
    {
        upstream_disposable::set_upstream_impl(d);
    }

    bool is_disposed() const noexcept
    {
        return m_is_disposed;
    }

    void dispose() const
    {
        m_is_disposed = true;
        upstream_disposable::dispose_impl();
    }

private:
    mutable bool m_is_disposed{false};
};

struct none_disposable_strategy
{
    static void set_upstream(const disposable_wrapper&) {}
    static bool is_disposed() noexcept { return false; }
    static void dispose() {}
};

template<constraint::decayed_type Type, constraint::observer_strategy<Type> Strategy, typename DisposablesStrategy>
class observer_impl
{
protected:
    template<typename... Args>
        requires constraint::is_constructible_from<Strategy, Args&&...>
    explicit observer_impl(DisposablesStrategy&& strategy, Args&&... args)
        : m_strategy{std::forward<Args>(args)...}, m_disposable{std::move(strategy)}
    {
    }

    observer_impl(const observer_impl&)     = default;
    observer_impl(observer_impl&&) noexcept = default;

public:
    /**
     * @brief Observable calls this method to pass disposable. Observer disposes this disposable WHEN observer wants to unsubscribe.
     * @note This method can be called multiple times, but new call means "replace upstream with this new one". So, tracked only last one
     */
    void set_upstream(const disposable_wrapper& d)
    {
        if (is_disposed()) {
            d.dispose();
            return;
        }

        m_disposable.set_upstream(d);
        m_strategy.set_upstream(d);
    }

    /**
     * @brief Observable calls this method to check if observer interested or not in emissions
     *
     * @return true if observer disposed and no longer interested in emissions
     * @return false if observer still interested in emissions
     */
    bool is_disposed() const noexcept
    {
        return m_disposable.is_disposed() ||  m_strategy.is_disposed();
    }
    /**
     * @brief Observable calls this method to notify observer about new value.
     *
     * @note obtains value by const-reference to original object.
     */
    void on_next(const Type& v) const noexcept
    {
        try
        {
            if (!is_disposed())
                m_strategy.on_next(v);
        }
        catch(...)
        {
            on_error(std::current_exception());
        }
    }

    /**
     * @brief Observable calls this method to notify observer about new value.
     *
     * @note obtains value by rvalue-reference to original object
     */
    void on_next(Type&& v) const noexcept
    {
        try
        {
            if (!is_disposed())
                m_strategy.on_next(std::move(v));
        }
        catch(...)
        {
            on_error(std::current_exception());
        }
    }

    /**
     * @brief Observable calls this method to notify observer about some error during generation next data.
     * @warning Obtaining this of this call means no any further on_next/on_error or on_completed calls from this Observable
     * @param err details of error
     */
    void on_error(const std::exception_ptr& err) const noexcept
    {
        if (!is_disposed())
        {
            rpp::utils::finally_action finally{[&]{ m_disposable.dispose(); }};
            m_strategy.on_error(err);
        }
    }

    /**
     * @brief Observable calls this method to notify observer about completion of emissions.
     * @warning Obtaining this of this call means no any further on_next/on_error or on_completed calls from this Observable
     */
    void on_completed() const noexcept
    {
        if (!is_disposed())
        {
            rpp::utils::finally_action finally{[&]{ m_disposable.dispose(); }};
            m_strategy.on_completed();
        }
    }

private:
    RPP_NO_UNIQUE_ADDRESS Strategy            m_strategy;
    RPP_NO_UNIQUE_ADDRESS DisposablesStrategy m_disposable;
};
}
namespace rpp
{
/**
 * @brief Base class for any observer used in RPP. It handles core callbacks of observers. Objects of this class would
 * be passed to subscribe of observable
 *
 * @warning By default observer is not copyable, only movable. If you need to COPY your observer, you need to convert it to rpp::dynamic_observer via rpp::observer::as_dynamic
 * @warning Expected that observer would be subscribed only to ONE observable ever. It can keep internal state and track it it was disposed or not. So, subscribing same observer multiple time follows unspecified behavior.
 * @warning If you are passing disposable to ctor, then state of this disposable would be used used (if empty disposable or disposed -> observer is disposed by default)
 *
 * @tparam Type of value this observer can handle
 * @tparam Strategy used to provide logic over observer's callbacks
 */
template<constraint::decayed_type Type, constraint::observer_strategy<Type> Strategy>
class observer;

template<constraint::decayed_type Type, constraint::observer_strategy<Type> Strategy>
class observer final : public details::observer_impl<Type, Strategy, details::local_disposable_strategy>
{
public:
    template<typename ...Args>
        requires (!constraint::variadic_decayed_same_as<observer<Type, Strategy>, Args...> && constraint::is_constructible_from<Strategy, Args&&...>)
    explicit observer(Args&& ...args)
        : details::observer_impl<Type, Strategy, details::local_disposable_strategy>{details::local_disposable_strategy{}, std::forward<Args>(args)...}
    {}

    observer(const observer&)     = delete;
    observer(observer&&) noexcept = default;

    /**
     * @brief Convert current observer to type-erased version. Useful if you need to COPY your observer or to store different observers in same container.
     */
    dynamic_observer<Type> as_dynamic() &&
    {
        return dynamic_observer<Type>{std::move(*this)};
    }
};

template<constraint::decayed_type Type, constraint::observer_strategy<Type> Strategy>
class observer<Type, details::with_disposable<Strategy>> final
    : public details::observer_impl<Type, Strategy, details::external_disposable_strategy>
{
public:
    template<typename ...Args>
        requires (constraint::is_constructible_from<Strategy, Args&&...>)
    explicit observer(disposable_wrapper disposable, Args&& ...args)
        : details::observer_impl<Type, Strategy, details::external_disposable_strategy>{details::external_disposable_strategy{std::move(disposable)}, std::forward<Args>(args)...}
    {}

    observer(const observer&)     = delete;
    observer(observer&&) noexcept = default;

    /**
     * @brief Convert current observer to type-erased version. Useful if you need to COPY your observer or to store different observers in same container.
     */
    dynamic_observer<Type> as_dynamic() &&
    {
        return dynamic_observer<Type>{std::move(*this)};
    }
};

template<constraint::decayed_type Type>
class observer<Type, rpp::details::observers::dynamic_strategy<Type>> final
    : public details::observer_impl<Type, rpp::details::observers::dynamic_strategy<Type>, details::none_disposable_strategy>
{
public:
    template<constraint::observer_strategy<Type> TStrategy>
        requires (!std::same_as<TStrategy, rpp::details::observers::dynamic_strategy<Type>>)
    explicit observer(observer<Type, TStrategy>&& other)
        : details::observer_impl<Type, rpp::details::observers::dynamic_strategy<Type>, details::none_disposable_strategy>{details::none_disposable_strategy{}, std::move(other)}
    {}

    observer(const observer&)     = default;
    observer(observer&&) noexcept = default;

    dynamic_observer<Type> as_dynamic() &&
    {
        return std::move(*this);
    }

    const dynamic_observer<Type>& as_dynamic() &
    {
        return *this;
    }
};


} // namespace rpp