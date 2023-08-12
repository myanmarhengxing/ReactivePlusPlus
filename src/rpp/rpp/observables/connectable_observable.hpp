//                   ReactivePlusPlus library
//
//           Copyright Aleksey Loginov 2023 - present.
//  Distributed under the Boost Software License, Version 1.0.
//     (See accompanying file LICENSE_1_0.txt or copy at
//           https://www.boost.org/LICENSE_1_0.txt)
//
//  Project home: https://github.com/victimsnino/ReactivePlusPlus

#pragma once

#include <rpp/observables/observable.hpp>
#include <rpp/subjects/fwd.hpp>
#include <rpp/disposables/refcount_disposable.hpp>

#include <mutex>

namespace rpp::details
{
template<typename T>
struct ref_count_on_subscribe_t;

template<rpp::constraint::observable OriginalObservable, rpp::constraint::subject Subject>
struct ref_count_on_subscribe_t<rpp::connectable_observable<OriginalObservable, Subject>>
{
    rpp::connectable_observable<OriginalObservable, Subject> original_observable{};
    struct state_t
    {
        std::mutex                                 mutex{};
        std::shared_ptr<rpp::refcount_disposable>  disposable{};
    };

    std::shared_ptr<state_t> m_state = std::make_shared<state_t>();

    using ValueType = rpp::utils::extract_observable_type_t<OriginalObservable>;
    template<constraint::observer_strategy<ValueType> Strategy>
    void subscribe(observer<ValueType, Strategy>&& obs) const
    {
        const auto [should_connect, disposable] = on_subscribe();

        obs.set_upstream(disposable);
        original_observable.subscribe(std::move(obs));
        if (should_connect)
            original_observable.connect(disposable);
    }

private:
    struct on_subscribe_res
    {
        bool                              should_connect{};
        rpp::composite_disposable_wrapper disposable{};
    };

    on_subscribe_res on_subscribe()  const
    {
        std::unique_lock lock(m_state->mutex);
        if (m_state->disposable && !m_state->disposable->is_disposed())
            return {.should_connect=false, .disposable=m_state->disposable->add_ref()};
        m_state->disposable = std::make_shared<rpp::refcount_disposable>();
        return {.should_connect=true, .disposable=m_state->disposable};
    }
};
}

namespace rpp
{
template<rpp::constraint::observable OriginalObservable, rpp::constraint::subject Subject>
class connectable_observable final : public decltype(std::declval<Subject>().get_observable())
{
    using base = decltype(std::declval<Subject>().get_observable());
public:
    connectable_observable(const OriginalObservable& original_observable, const Subject& subject = Subject{})
        : base{subject.get_observable()}
        , m_original_observable{original_observable}
        , m_subject{subject} {}

    connectable_observable(OriginalObservable&& original_observable, const Subject& subject = Subject{})
        : base{subject.get_observable()}
        , m_original_observable{std::move(original_observable)}
        , m_subject{subject} {}


    rpp::disposable_wrapper connect(rpp::composite_disposable_wrapper wrapper = {}) const
    {
        std::unique_lock lock(m_state->mutex);

        if (m_subject.get_disposable().is_disposed())
            return {};

        if (!m_state->disposable.is_disposed())
            return m_state->disposable;
        
        if (!wrapper.has_underlying())
            wrapper = rpp::composite_disposable_wrapper{std::make_shared<rpp::composite_disposable>()};

        m_state->disposable = wrapper;
        lock.unlock();

        m_original_observable.subscribe(wrapper, m_subject.get_observer());
        return wrapper;
    }

    /**
    * @brief Forces rpp::connectable_observable to behave like common observable
    * @details Connects rpp::connectable_observable on the first subscription and unsubscribes on last unsubscription
    *	
    * @par Example
    * @snippet ref_count.cpp ref_count
    * 
    * @ingroup connectable_operators
    * @see https://reactivex.io/documentation/operators/refcount.html
    */
    auto ref_count() const
    {
        return rpp::observable < rpp::utils::extract_observable_type_t<OriginalObservable>,
               details::ref_count_on_subscribe_t<connectable_observable<OriginalObservable, Subject>>>{*this};
    }

private:
    OriginalObservable                                        m_original_observable;
    Subject                                                   m_subject;
    struct state_t
    {
        std::mutex                        mutex{};
        rpp::composite_disposable_wrapper disposable{};
    };
    std::shared_ptr<state_t> m_state = std::make_shared<state_t>();
};
}