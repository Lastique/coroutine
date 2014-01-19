
//          Copyright Oliver Kowalke 2009.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef BOOST_COROUTINES_DETAIL_PUSH_COROUTINE_OBJECT_H
#define BOOST_COROUTINES_DETAIL_PUSH_COROUTINE_OBJECT_H

#include <cstddef>

#include <boost/assert.hpp>
#include <boost/config.hpp>
#include <boost/cstdint.hpp>
#include <boost/exception_ptr.hpp>
#include <boost/move/move.hpp>
#include <boost/ref.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/type_traits/function_traits.hpp>
#include <boost/utility.hpp>

#include <boost/coroutine/attributes.hpp>
#include <boost/coroutine/detail/config.hpp>
#include <boost/coroutine/exceptions.hpp>
#include <boost/coroutine/detail/flags.hpp>
#include <boost/coroutine/detail/holder.hpp>
#include <boost/coroutine/detail/param.hpp>
#include <boost/coroutine/detail/stack_tuple.hpp>
#include <boost/coroutine/detail/trampoline.hpp>
#include <boost/coroutine/flags.hpp>
#include <boost/coroutine/stack_context.hpp>
#include <boost/coroutine/detail/push_coroutine_base.hpp>

#ifdef BOOST_MSVC
 #pragma warning (push)
 #pragma warning (disable: 4355) // using 'this' in initializer list
#endif

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_PREFIX
#endif

namespace boost {
namespace coroutines {
namespace detail {

template<
    typename Arg, typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object : private stack_tuple< StackAllocator >,
                              public push_coroutine_base< Arg >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            Arg, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< Arg >                  base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< Arg > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
#ifdef BOOST_NO_CXX11_RVALUE_REFERENCES
    push_coroutine_object( Fn fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }
#endif
    push_coroutine_object( BOOST_RV_REF( Fn) fn, attributes const& attr,
                      StackAllocator const& stack_alloc,
                      allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
#ifdef BOOST_NO_CXX11_RVALUE_REFERENCES
        fn_( fn),
#else
        fn_( forward< Fn >( fn) ),
#endif
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< Arg > * hldr_from(
                reinterpret_cast< holder< Arg > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);
            BOOST_ASSERT( hldr_from->data);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_, hldr_from->data);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< Arg > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Arg, typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< Arg, reference_wrapper< Fn >, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< Arg >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            Arg, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< Arg >                  base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< Arg > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
    push_coroutine_object( reference_wrapper< Fn > fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< Arg > * hldr_from(
                reinterpret_cast< holder< Arg > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);
            BOOST_ASSERT( hldr_from->data);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_, hldr_from->data);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< Arg > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Arg, typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< Arg, const reference_wrapper< Fn >, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< Arg >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            Arg, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< Arg >                  base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< Arg > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
    push_coroutine_object( const reference_wrapper< Fn > fn, attributes const& attr,
                      StackAllocator const& stack_alloc,
                      allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< Arg > * hldr_from(
                reinterpret_cast< holder< Arg > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);
            BOOST_ASSERT( hldr_from->data);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_, hldr_from->data);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< Arg > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Arg, typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< Arg &, Fn, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< Arg & >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            Arg &, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< Arg & >                base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< Arg * > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
#ifndef BOOST_NO_CXX11_RVALUE_REFERENCES
    push_coroutine_object( BOOST_RV_REF( Fn) fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( forward< Fn >( fn) ),
        alloc_( alloc)
    { enter_(); }
#else
    push_coroutine_object( Fn fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    push_coroutine_object( BOOST_RV_REF( Fn) fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }
#endif

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< Arg * > * hldr_from(
                reinterpret_cast< holder< Arg * > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);
            BOOST_ASSERT( hldr_from->data);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_, hldr_from->data);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< Arg * > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Arg, typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< Arg &, reference_wrapper< Fn >, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< Arg & >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            Arg &, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< Arg & >                 base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< Arg * > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
    push_coroutine_object( reference_wrapper< Fn > fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< Arg * > * hldr_from(
                reinterpret_cast< holder< Arg * > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);
            BOOST_ASSERT( hldr_from->data);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_, hldr_from->data);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< Arg * > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Arg, typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< Arg &, const reference_wrapper< Fn >, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< Arg & >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            Arg, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< Arg & >                base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< Arg * > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
    push_coroutine_object( const reference_wrapper< Fn > fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< Arg * > * hldr_from(
                reinterpret_cast< holder< Arg * > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);
            BOOST_ASSERT( hldr_from->data);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_, hldr_from->data);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< Arg * > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< void, Fn, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< void >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            void, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< void >                 base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< void > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
#ifndef BOOST_NO_CXX11_RVALUE_REFERENCES
    push_coroutine_object( BOOST_RV_REF( Fn) fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( forward< Fn >( fn) ),
        alloc_( alloc)
    { enter_(); }
#else
    push_coroutine_object( Fn fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    push_coroutine_object( BOOST_RV_REF( Fn) fn, attributes const& attr,
                      StackAllocator const& stack_alloc,
                      allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }
#endif

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< void > * hldr_from(
                reinterpret_cast< holder< void > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< void > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< void, reference_wrapper< Fn >, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< void >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            void, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< void >                 base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< void > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
    push_coroutine_object( reference_wrapper< Fn > fn, attributes const& attr,
                           StackAllocator const& stack_alloc,
                           allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< void > * hldr_from(
                reinterpret_cast< holder< void > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< void > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

template<
    typename Fn,
    typename StackAllocator, typename Allocator,
    typename Caller
>
class push_coroutine_object< void, const reference_wrapper< Fn >, StackAllocator, Allocator, Caller > :
    private stack_tuple< StackAllocator >,
    public push_coroutine_base< void >
{
public:
    typedef typename Allocator::template rebind<
        push_coroutine_object<
            void, Fn, StackAllocator, Allocator, Caller
        >
    >::other                                            allocator_t;

private:
    typedef stack_tuple< StackAllocator >               pbase_type;
    typedef push_coroutine_base< void >                 base_type;

    Fn                      fn_;
    allocator_t             alloc_;

    static void destroy_( allocator_t & alloc, push_coroutine_object * p)
    {
        alloc.destroy( p);
        alloc.deallocate( p, 1);
    }

    push_coroutine_object( push_coroutine_object &);
    push_coroutine_object & operator=( push_coroutine_object const&);

    void enter_()
    {
        holder< void > * hldr_from(
            reinterpret_cast< holder< void > * >(
                this->caller_.jump(
                    this->callee_,
                    reinterpret_cast< intptr_t >( this),
                    this->preserve_fpu() ) ) );
        this->callee_ = * hldr_from->ctx;
        if ( this->except_) rethrow_exception( this->except_);
    }

    void unwind_stack_() BOOST_NOEXCEPT
    {
        BOOST_ASSERT( ! this->is_complete() );

        this->flags_ |= flag_unwind_stack;
        holder< void > hldr_to( & this->caller_, true);
        this->caller_.jump(
            this->callee_,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        this->flags_ &= ~flag_unwind_stack;

        BOOST_ASSERT( this->is_complete() );
    }

public:
    push_coroutine_object( const reference_wrapper< Fn > fn, attributes const& attr,
                      StackAllocator const& stack_alloc,
                      allocator_t const& alloc) :
        pbase_type( stack_alloc, attr.size),
        base_type(
            trampoline1< push_coroutine_object >,
            & this->stack_ctx,
            stack_unwind == attr.do_unwind,
            fpu_preserved == attr.preserve_fpu),
        fn_( fn),
        alloc_( alloc)
    { enter_(); }

    ~push_coroutine_object()
    {
        if ( ! this->is_complete() && this->force_unwind() )
            unwind_stack_();
    }

    void run()
    {
        coroutine_context callee;
        coroutine_context caller;

        {
            holder< void > hldr_to( & caller);
            holder< void > * hldr_from(
                reinterpret_cast< holder< void > * >(
                    caller.jump(
                        this->caller_,
                        reinterpret_cast< intptr_t >( & hldr_to),
                        this->preserve_fpu() ) ) );
            BOOST_ASSERT( hldr_from->ctx);

            // create pull_coroutine
            Caller c( * hldr_from->ctx, false, this->preserve_fpu(), alloc_);
            try
            { fn_( c); }
            catch ( forced_unwind const&)
            {}
            catch (...)
            { this->except_ = current_exception(); }
            callee = c.impl_->callee_;
        }

        this->flags_ |= flag_complete;
        holder< void > hldr_to( & caller);
        caller.jump(
            callee,
            reinterpret_cast< intptr_t >( & hldr_to),
            this->preserve_fpu() );
        BOOST_ASSERT_MSG( false, "push_coroutine is complete");
    }

    void deallocate_object()
    { destroy_( alloc_, this); }
};

}}}

#ifdef BOOST_HAS_ABI_HEADERS
#  include BOOST_ABI_SUFFIX
#endif

#ifdef BOOST_MSVC
 #pragma warning (pop)
#endif

#endif // BOOST_COROUTINES_DETAIL_PUSH_COROUTINE_OBJECT_H
