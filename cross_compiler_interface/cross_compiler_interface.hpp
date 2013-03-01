//          Copyright John R. Bandela 2012.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef cross_compiler_interface_
#define cross_compiler_interface_


// Disable some MSVC warnings
#pragma warning(push)
#pragma warning(disable:4996)
#pragma  warning(disable: 4099)
#pragma  warning(disable: 4351)

// Include Platform Specific
#ifdef _WIN32
#include "platform/Windows/platform_specific.hpp"
#endif // _WIN32
#ifdef __linux__
#include "platform/Linux/platform_specific.hpp"
#endif // __linux__

#include <functional>
#include <assert.h>
#include <cstddef>
#include <stdexcept>
#include <string>


#include "cross_compiler_error_handling.hpp"

namespace cross_compiler_interface{

	// Template for converting to/from regular types to cross-compiler compatible types 
	template<class T>
	struct cross_conversion{		
	};


	// Allocator - uses shared_malloc from our platform specific header
	template<class T>
	T* allocate_array(std::size_t sz){
		auto ret = static_cast<T*>(shared_malloc(sizeof(T)*sz));
		if(!ret)throw std::bad_alloc();
		return ret;
	}

	namespace detail{
		// Calling convention defined in platform specific header

		typedef  void(CROSS_CALL_CALLING_CONVENTION *ptr_fun_void_t)();
		extern "C"{
			struct  portable_base{
				ptr_fun_void_t* vfptr;
			};

		}
	}
	typedef detail::portable_base portable_base;

	// Make sure no padding
	static_assert(sizeof(portable_base)==sizeof(detail::ptr_fun_void_t*),"Padding in portable_base");



	namespace detail{

		// Helper functions to cast a vtable function to the correct type and call it
		template<class R, class... Parms>
		R call(const ptr_fun_void_t pFun,Parms... p){
			typedef R( CROSS_CALL_CALLING_CONVENTION *fun_t)(Parms...);
			auto f = reinterpret_cast<fun_t>(pFun);
			return f(p...);
		}

		template<class T>
		T& dummy_conversion(T& t){
			return t;
		}

	}



	// base class for vtable_n
	struct vtable_n_base:public portable_base{
		void** pdata;
		portable_base* runtime_parent_;
		vtable_n_base(void** p):pdata(p),runtime_parent_(0){}
		template<int n,class T>
		T* get_data()const{
			return static_cast<T*>(pdata[n]);
		}

		template<int n>
		void set_data(void* d){
			pdata[n] = d;
		}

		template<int n,class R, class... Parms>
		void update(R(CROSS_CALL_CALLING_CONVENTION *pfun)(Parms...)){
			vfptr[n] = reinterpret_cast<detail::ptr_fun_void_t>(pfun);
		}

		template<int n,class R, class... Parms>
		void add(R(CROSS_CALL_CALLING_CONVENTION *pfun)(Parms...)){
			// If you have an assertion here, you have a duplicated number in you interface
			assert(vfptr[n] == nullptr);
			update<n>(pfun);
		}
	};

	// Our "vtable" definition
	template<int N>
	struct vtable_n:public vtable_n_base 
	{
	protected:
		detail::ptr_fun_void_t table_n[N];
		void* data[N];
		enum {sz = N};
		vtable_n():vtable_n_base(data),table_n(),data(){
			vfptr = &table_n[0];
		}

	public:
		portable_base* get_portable_base(){return this;}
		portable_base* get_portable_base()const{return this;}

	};

	namespace detail{
		template<int N,class F>
		F& get_function(const portable_base* v){
			const vtable_n_base* vt = static_cast<const vtable_n_base*>(v);
			return *vt->template get_data<N,F>();
		}

		template<int N,class T>
		T* get_data(const portable_base* v){
			const vtable_n_base* vt = static_cast<const vtable_n_base*>(v);
			return vt->template get_data<N,T>();
		}
	}

	template<bool bImp,template<bool> class Iface, int N,class F>
	struct jrb_function{};

	struct conversion_helper{ // Used to Help MSVC++ avoid Internal Compiler Error
		template<class Parm>
		static typename cross_conversion<Parm>::converted_type to_converted(Parm p){
			typedef cross_conversion<Parm> cc;
			return cc::to_converted_type(p);
		}
		template<class Parm>
		static typename cross_conversion<Parm>::original_type to_original(typename cross_conversion<Parm>::converted_type p){
			typedef cross_conversion<Parm> cc;
			return cc::to_original_type(p);
		}

	};

	template<template<bool> class Iface, int N>
	struct call_adaptor{

		template<class R,class... Parms>
		struct vtable_caller{
			static R call_vtable_func(const detail::ptr_fun_void_t pFun,const portable_base* v,Parms... p){
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details
				typedef cross_conversion<R> cc;
				typedef typename cc::converted_type cret_t;
				typename cc::converted_type cret;
				auto ret =  detail::call<error_code,const portable_base*, cret_t*, typename cross_conversion<Parms>::converted_type...>(pFun,
					v,&cret,conversion_helper::to_converted<Parms>(p)...);
				if(ret){
					error_mapper<Iface>::mapper::exception_from_error_code(ret);
				}
				return conversion_helper::to_original<R>(cret);
			}

		};

		template<class... Parms>
		struct vtable_caller<void,Parms...>{

			static void call_vtable_func(const detail::ptr_fun_void_t pFun,const portable_base* v,Parms... p){
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details
				auto ret =  detail::call<error_code,const portable_base*,typename cross_conversion<Parms>::converted_type...>(pFun,
					v,conversion_helper::to_converted<Parms>(p)...);
				if(ret){
					error_mapper<Iface>::mapper::exception_from_error_code(ret);
				}
				return;
			}

		};
		template<class R,class... Parms>
		struct vtable_entry{
			typedef std::function<R(Parms...)> fun_t;
			typedef error_code (CROSS_CALL_CALLING_CONVENTION * vt_entry_func)(const portable_base*,
				typename cross_conversion<R>::converted_type*,typename cross_conversion<Parms>::converted_type...);

			static error_code CROSS_CALL_CALLING_CONVENTION func(const portable_base* v, typename cross_conversion<R>::converted_type* r,typename cross_conversion<Parms>::converted_type... p){
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details
				try{
					auto& f = detail::get_function<N,fun_t>(v);
					if(!f){
						// See if runtime inheritance present with parent
						const vtable_n_base* vt = static_cast<const vtable_n_base*>(v);
						if(vt->runtime_parent_){
							// call the parent
							// Use dummy conversion because MSVC does not like just p...
							return reinterpret_cast<vt_entry_func>(vt->runtime_parent_->vfptr[N])(vt->runtime_parent_,r,detail::dummy_conversion<typename cross_conversion<Parms>::converted_type>(p)...);
						}
						else{
							return error_not_implemented::ec;
						}
					}
					*r = conversion_helper::to_converted<R>(f(conversion_helper::to_original<Parms>(p)...));
					return 0;
				} catch(std::exception& e){
					return error_mapper<Iface>::mapper::error_code_from_exception(e);
				}
			}
		};

		template<class... Parms>
		struct vtable_entry<void,Parms...>{
			typedef std::function<void(Parms...)> fun_t;
			typedef error_code (CROSS_CALL_CALLING_CONVENTION * vt_entry_func)(const portable_base*,
				typename cross_conversion<Parms>::converted_type...);

			static error_code CROSS_CALL_CALLING_CONVENTION func(const portable_base* v, typename cross_conversion<Parms>::converted_type... p){
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details
				// See also http://connect.microsoft.com/VisualStudio/feedback/details/769988/codename-milan-total-mess-up-with-variadic-templates-and-namespaces
				try{
					auto& f = detail::get_function<N,fun_t>(v);
					if(!f){
						// See if runtime inheritance present with parent
						const vtable_n_base* vt = static_cast<const vtable_n_base*>(v);
						if(vt->runtime_parent_){
							// call the parent
							return reinterpret_cast<vt_entry_func>(vt->runtime_parent_->vfptr[N])(vt->runtime_parent_,detail::dummy_conversion<typename cross_conversion<Parms>::converted_type>(p)...);
						}
						else{
							return error_not_implemented::ec;
						}
					}

					f(conversion_helper::to_original<Parms>(p)...);
					return 0;
				} catch(std::exception& e){
					return error_mapper<Iface>::mapper::error_code_from_exception(e);
				}
			}
		};
	
		template<class ... Parms>
		struct vtable_entry_fast{

			template<class C, class MF, MF mf, class R>
			static error_code CROSS_CALL_CALLING_CONVENTION func(const portable_base* v, typename cross_conversion<R>::converted_type* r,typename cross_conversion<Parms>::converted_type... p){
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details


				try{
					C* f = detail::get_data<N,C>(v);
					*r = conversion_helper::to_converted<R>((f->*mf)(conversion_helper::to_original<Parms>(p)...));
					return 0;
				} catch(std::exception& e){
					return error_mapper<Iface>::mapper::error_code_from_exception(e);
				}
			}
		};

				template<class ... Parms>
		struct vtable_entry_fast_void{

			template<class C, class MF, MF mf, class R>
			static error_code CROSS_CALL_CALLING_CONVENTION func(const portable_base* v, typename cross_conversion<Parms>::converted_type... p){
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details


				try{
					C* f = detail::get_data<N,C>(v);
					(f->*mf)(conversion_helper::to_original<Parms>(p)...);
					return 0;
				} catch(std::exception& e){
					return error_mapper<Iface>::mapper::error_code_from_exception(e);
				}
			}
		};

	};


	template<bool bImp, template<bool> class Iface, int N,class R, class... Parms>
	struct jrb_function_base{
		portable_base* p_;
		template<class... P>
		R operator()(P&&... p)const{
				using namespace std; // Workaround for MSVC bug http://connect.microsoft.com/VisualStudio/feedback/details/772001/codename-milan-c-11-compilation-issue#details
				// See also http://connect.microsoft.com/VisualStudio/feedback/details/769988/codename-milan-total-mess-up-with-variadic-templates-and-namespaces
			typedef typename call_adaptor<Iface,N>::template vtable_caller<R,Parms...> adapter;
			return adapter::call_vtable_func(p_->vfptr[N],p_,std::forward<P>(p)...);
		}
		jrb_function_base(portable_base* v):p_(v){}
	};

	template<template<bool> class Iface, int N,class R, class... Parms>
	struct jrb_function<true, Iface,N,R(Parms...)>
		:public jrb_function_base<true,Iface,N,R,Parms...>,
		public call_adaptor<Iface,N>::template vtable_entry<R,Parms...>
	{ //Implementation
		typedef R return_type;
		typedef std::function<R(Parms...)> fun_t;
		fun_t func_;

		jrb_function(portable_base* p):jrb_function_base<true,Iface,N,R,Parms...>(p){
			auto vn = static_cast<vtable_n_base*>(p);
			vn->template set_data<N>(&func_);
			vn->template add<N>(jrb_function::func);
		}

		template<class F>
		static void set_function(jrb_function& jf,F f){
			jf.func_ = f;
		}

		typedef jrb_function jf_t;
	};




	template<template<bool> class Iface, int N,class R, class... Parms>
	struct jrb_function<false, Iface,N,R(Parms...)>
		:public jrb_function_base<false,Iface,N,R,Parms...>
	{ //Usage
		typedef R return_type;
		typedef std::function<R(Parms...)> fun_t;

		jrb_function(portable_base* p):
			jrb_function_base<false,Iface,N,R,Parms...>(p){}

	};

	template<class Iface, int Id,class F>
	struct cross_function{};

	template<template<bool>class Iface,int Id,class F>
	struct cross_function<Iface<false>,Id,F>:public jrb_function<false,Iface,Id + Iface<false>::base_sz,F>{
		enum{N = Id + Iface<false>::base_sz};
		enum{interface_sz = Iface<false>::sz - Iface<false>::base_sz};
		static_assert(Id < interface_sz,"Increase the sz of your interface");
		cross_function(Iface<false>* pi):jrb_function<false,Iface,N,F>(pi->get_portable_base()){}


	};	


	namespace detail{

	// MSVC Milan has trouble with variadic templates
	// and mem_fn. We use this template to help us with mem_fn

	template<class F>
	struct mem_fn_helper{};

	template<class R,class... Parms>
	struct mem_fn_helper<R(Parms...)>
	{
		template<class C,template<bool>class Iface, int N>
		struct inner{

		typedef R (C::*MFT)(Parms...);

		typedef R ret_t;
		typedef typename call_adaptor<Iface,N>:: template vtable_entry_fast<Parms...> vte_t;

		};
	};


	template<class... Parms>
	struct mem_fn_helper<void(Parms...)>
	{
		template<class C,template<bool>class Iface, int N>
		struct inner{

		typedef void (C::*MFT)(Parms...);

		typedef void ret_t;
		typedef typename call_adaptor<Iface,N>:: template vtable_entry_fast_void<Parms...> vte_t;

		};
	};
	}


	template<template<bool>class Iface, int Id,class F>
	struct cross_function<Iface<true>,Id,F>:public jrb_function<true,Iface,Id + Iface<true>::base_sz,F>{
		enum{N = Id + Iface<true>::base_sz};
		typedef jrb_function<true,Iface,Id + Iface<true>::base_sz,F> jf_t;
		enum{interface_sz = Iface<true>::sz - Iface<true>::base_sz};
		static_assert(Id < interface_sz,"Increase the sz of your interface");
		cross_function(Iface<true>* pi):jf_t(pi->get_portable_base()){}

		template<class Func>
		void operator=(Func f){
			jf_t::set_function(*this,f);
		}
		typedef detail::mem_fn_helper<F> tm;
		template<class C, typename tm:: template inner<C,Iface,N>::MFT mf>
		void set_mem_fn (C* c){
			typedef typename tm:: template inner<C,Iface,N>::MFT MF;
			typedef typename tm:: template inner<C,Iface,N>::ret_t R;
			typedef typename tm:: template inner<C,Iface,N>::vte_t vte_t;


			typedef vtable_n_base vn_t;
			vn_t* vn = static_cast<vn_t*>(jf_t::p_);
			vn->template set_data<N>(c);
			vn->template update<N>(&vte_t:: template func<C,MF,mf,R>);

		}
	};

	template<template <bool> class Iface>
	struct use_interface:public Iface<false>{ // Usage
		use_interface(portable_base* v):Iface<false>(v){}

		explicit operator bool(){
			return this->get_portable_base();
		}
	};


	template<template <bool> class Iface>
	use_interface<Iface> create(std::string module,std::string func){
		typedef portable_base* (CROSS_CALL_CALLING_CONVENTION *CFun)();
		auto f = load_module_function<CFun>(module,func);
		return f();


	}

	template<template<bool> class Iface>
	struct implement_interface:vtable_n<Iface<true>::sz>,public Iface<true>{ // Implementation


		implement_interface():Iface<true>(vtable_n<Iface<true>::sz>::get_portable_base()){}

		void set_runtime_parent(use_interface<Iface> parent){
			vtable_n_base* vnb = this;
			vnb->runtime_parent_ = parent.get_portable_base();
		}

		using  Iface<true>::get_portable_base;
		operator use_interface<Iface>(){return Iface<true>::get_portable_base();}
	};
	

	template<bool b>
	struct InterfaceBase{
	private:
		portable_base* p_;
	public:
		enum{sz = 0};
		
		InterfaceBase(portable_base* p):p_(p){} 

		portable_base* get_portable_base()const{
			return p_;
		}
	};

	template<bool b,int num_functions, template<bool> class Base = InterfaceBase >
	struct define_interface:public Base<b>{
		enum{base_sz = Base<b>::sz};

		enum{sz = num_functions + base_sz};
		typedef define_interface base_t;

		define_interface(portable_base* p):Base<b>(p){}
	};

}

#include "cross_compiler_conversions.hpp"

#pragma warning(pop)
#endif