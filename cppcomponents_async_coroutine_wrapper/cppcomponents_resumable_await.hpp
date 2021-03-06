
// Copyright (c) 2013 John R. Bandela
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#pragma once
#ifndef INCLUDE_GUARD_CPPCOMPONENTS_RESUMABLE_AWAIT_HPP_
#define INCLUDE_GUARD_CPPCOMPONENTS_RESUMABLE_AWAIT_HPP_

#include <cppcomponents/future.hpp>
#include "cppcomponents_async_coroutine_wrapper.hpp"

#include <memory>
#include <exception>
#include <utility>
#include <assert.h>
#include <type_traits>
#include <functional>

#ifdef PPL_HELPER_OUTPUT_ENTER_EXIT
#include <cstdio>
#include <atomic>
#define PPL_HELPER_ENTER_EXIT ::cppcomponents::detail::EnterExit ppl_helper_enter_exit_var;
#else
#define PPL_HELPER_ENTER_EXIT
#endif

namespace cppcomponents{
	namespace detail{
#ifdef PPL_HELPER_OUTPUT_ENTER_EXIT
		// Used for debugging to make sure all functions are exiting correctly
		struct EnterExit{
			EnterExit() : n_(++number()){ increment(); std::printf("==%d Entering\n", n_); }
			int n_;
			std::string s_;
			static int& number(){ static int number = 0; return number; }
			static std::atomic<int>& counter(){ static std::atomic<int> counter_ = 0; return counter_; }
			static void increment(){ ++counter(); }
			static void decrement(){ --counter(); }
			static void check_all_destroyed(){ assert(counter() == 0); if (counter())throw std::exception("Not all EnterExit destroyed"); };
			~EnterExit(){ decrement(); std::printf("==%d Exiting\n", n_); }

		};
#endif
#undef PPL_HELPER_OUTPUT_ENTER_EXIT


		struct coroutine_holder : std::enable_shared_from_this<coroutine_holder>{
			PPL_HELPER_ENTER_EXIT;
			typedef cppcomponents_async_coroutine_wrapper::CoroutineVoidPtr co_type;
			std::unique_ptr<co_type> coroutine_;
			co_type::CallerType* coroutine_caller_;
			use<InterfaceUnknown> future_;
			coroutine_holder() : coroutine_(), coroutine_caller_(nullptr){}

		};

		struct ret_type{
			std::exception_ptr eptr_;
			void* pv_;

			template<class T>
			T& get(){
				if (eptr_){
					std::rethrow_exception(eptr_);
				}
				return *static_cast<T*>(pv_);
			}
		};


		typedef std::function < void ()> awaiter_func_type;

		void execute_awaiter_func(void* v){
			if (!v) return;
			auto f = *static_cast<awaiter_func_type*>(v);
			f();
		}


	}



	class awaiter{
		typedef detail::coroutine_holder* co_ptr;
		typedef detail::awaiter_func_type func_type;
		co_ptr co_;

		template<class R>
		func_type get_function(use < IFuture < R >> t){
			auto co = co_;
			func_type retfunc([co, t]()mutable{
				auto sptr = co->shared_from_this();
				t.Then([sptr](use < IFuture < R >> et)mutable{
					detail::ret_type ret;
					ret.eptr_ = nullptr;
					ret.pv_ = nullptr;
					ret.pv_ = &et;
					(*sptr->coroutine_)(&ret);
					try{
						detail::execute_awaiter_func(sptr->coroutine_->Get());
					}
					catch (std::exception& e){
						(void)e;
						ret.eptr_ = std::current_exception();
						ret.pv_ = nullptr;
						(*sptr->coroutine_)(&ret);
						throw;
					}
				});
			});
			return retfunc;
		}
		template<class R>
		func_type get_function(use<IExecutor> executor, use < IFuture < R >> t){
			auto co = co_;
			func_type retfunc([co, t,executor]()mutable{
				auto sptr = co->shared_from_this();
				t.Then(executor,[sptr](use < IFuture < R >> et)mutable{
					detail::ret_type ret;
					ret.eptr_ = nullptr;
					ret.pv_ = nullptr;
					ret.pv_ = &et;
					(*sptr->coroutine_)(&ret);
					try{
						detail::execute_awaiter_func(sptr->coroutine_->Get());
					}
					catch (std::exception& e){
						(void)e;
						ret.eptr_ = std::current_exception();
						ret.pv_ = nullptr;
						(*sptr->coroutine_)(&ret);
						throw;
					}
				});
			});

			return retfunc;

		}

	public:
		awaiter(co_ptr c)
			: co_(c)
		{

		}

		template<class R>
		use<IFuture<R>> as_future(use < IFuture < R >> t){
			if (t.Ready()){
				return t;
			}
			auto retfunc = get_function(t);
			PPL_HELPER_ENTER_EXIT;
			assert(co_->coroutine_caller_);
			(*co_->coroutine_caller_)(&retfunc);
			return static_cast<detail::ret_type*>(co_->coroutine_caller_->Get())->get < use < IFuture<R >> >();
		}
		template<class R>
			use<IFuture<R>> as_future(use<IExecutor> executor, use < IFuture < R >> t){
				if (t.Ready()){
					return t;
				}
				auto retfunc = get_function(executor,t);
				PPL_HELPER_ENTER_EXIT;
				assert(co_->coroutine_caller_);
				(*co_->coroutine_caller_)(&retfunc);
				return static_cast<detail::ret_type*>(co_->coroutine_caller_->Get())->get < use < IFuture<R >> >();
		}

			template<class R>
			R operator()(use < IFuture < R >> t){
				return as_future(t).Get();
			}

			template<class R>
			R operator()(use<IExecutor> executor,use < IFuture < R >> t){
				return as_future(executor,t).Get();
			}

	};
	namespace detail{

		template<class T>
		struct ret_holder{
			T value_;
			template<class FT>
			ret_holder(FT& f, awaiter h) : value_(f(h)){}
			const T& get()const{ return value_; }

			use<IFuture<T>> get_ready_future()const{ return cppcomponents::make_ready_future(value_); }
		};
		template<>
		struct ret_holder<void>{
			template<class FT>
			ret_holder(FT& f, awaiter h){
				f(h);
			}
			void get()const{}
			use<IFuture<void>> get_ready_future()const{ return cppcomponents::make_ready_future(); }

		};

		template<class F>
		class simple_async_function_holder : public coroutine_holder{


			F f_;
			typedef typename std::result_of<F(awaiter)>::type return_type;
			typedef use<IFuture<return_type>> task_t;
			typedef std::function<void()> func_type;


			static void coroutine_function(cppcomponents::use<cppcomponents_async_coroutine_wrapper::ICoroutineVoidPtr> ca){
				PPL_HELPER_ENTER_EXIT;;

				auto p = ca.Get();
				auto pthis = reinterpret_cast<simple_async_function_holder*>(p);
				pthis->coroutine_caller_ = &ca;
				auto promise = make_promise<return_type>();
				pthis->future_ = promise.template QueryInterface < InterfaceUnknown>();
				try{
					PPL_HELPER_ENTER_EXIT;
					awaiter helper(pthis);
					promise.SetResultOf(std::bind(pthis->f_, helper));
					ca(nullptr);
				}
				catch (std::exception& e){
					auto ec = cppcomponents::error_mapper::error_code_from_exception(e);
					promise.SetError(ec);
					ca(nullptr);
				}
			}
		public:
			simple_async_function_holder(F f) : f_(f){}

			task_t run(){
				coroutine_.reset(new coroutine_holder::co_type(cppcomponents::make_delegate<cppcomponents_async_coroutine_wrapper::CoroutineHandler>(coroutine_function), this));
				detail::execute_awaiter_func(coroutine_->Get());

				return this->future_.template QueryInterface<IFuture<return_type>>();

			}
		};

		template<class F>
		struct return_helper{};

		template<class R>
		struct return_helper<R(awaiter)>{
			typedef R type;
		};


		template<class F>
		use<IFuture<typename std::result_of<F(awaiter)>::type>> do_async(F f){
			auto ret = std::make_shared<detail::simple_async_function_holder<F>>(f);
			return ret->run();
		}


		template<class R, class F>
		struct do_async_functor{
			F f_;
			template<class... T>
			use<IFuture<R>> operator()(T && ... t){
				using namespace std::placeholders;
				return do_async(std::bind(f_, std::forward<T>(t)..., _1));
			}

			do_async_functor(F f) : f_{ f }{}


		};

		namespace return_type_calculator{

			// Calculate return type of callable
			// Adapted from http://stackoverflow.com/questions/11893141/inferring-the-call-signature-of-a-lambda-or-arbitrary-callable-for-make-functio
			template<class F>
			using typer = F;

			template<typename T> struct remove_class { };
			template<typename C, typename R, typename... A>
			struct remove_class<R(C::*)(A...)> { using type = typer<R(A...)>; using return_type = R; };
			template<typename C, typename R, typename... A>
			struct remove_class<R(C::*)(A...) const> { using type = typer<R(A...)>; using return_type = R; };
			template<typename C, typename R, typename... A>
			struct remove_class<R(C::*)(A...) volatile> { using type = typer<R(A...)>; using return_type = R; };
			template<typename C, typename R, typename... A>
			struct remove_class<R(C::*)(A...) const volatile> { using type = typer<R(A...)>; using return_type = R; };

			template<class T>
			using remove_reference_t = typename std::remove_reference<T>::type;
			template<typename T>
			struct get_signature_impl {
				using type = typename remove_class<
					decltype(&remove_reference_t<T>::operator())>::type;
				using return_type = typename remove_class<
					decltype(&remove_reference_t<T>::operator())>::return_type;
			};
			template<typename R, typename... A>
			struct get_signature_impl<R(A...)> { using type = typer<R(A...)>; using return_type = R; };
			template<typename R, typename... A>
			struct get_signature_impl<R(&)(A...)> { using type = typer<R(A...)>; using return_type = R; };
			template<typename R, typename... A>
			struct get_signature_impl<R(*)(A...)> { using type = typer<R(A...)>; using return_type = R; };
			template<typename T> using get_signature = typename get_signature_impl<T>::type;
			template<typename T> using return_type = typename get_signature_impl<T>::return_type;

		}
	}
	template<class F>
	auto  resumable(F f) -> detail::do_async_functor<detail::return_type_calculator::return_type<decltype(f)>, F>{
		return detail::do_async_functor<detail::return_type_calculator::return_type<F>, F>{f};
	}

}


#endif