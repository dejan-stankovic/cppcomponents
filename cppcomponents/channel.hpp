//          Copyright John R. Bandela 2013.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#pragma once
#ifndef INCLUDE_GUARD_CPPCOMPONENTS_CHANNEL_HPP_05_29_2013_
#define INCLUDE_GUARD_CPPCOMPONENTS_CHANNEL_HPP_05_29_2013_

#include "future.hpp"
#include "implementation/queue.hpp"

namespace cppcomponents{

	typedef cppcomponents::uuid<0x4d327f1c, 0x7515, 0x4dc5, 0xb1e4, 0x4ff5051d2beb> channel_base_uuid_t;
	template<class T>
	struct IChannel : define_interface<combine_uuid<channel_base_uuid_t, typename uuid_of<T>::uuid_type>>{
		use<IFuture<void>> Write(T);
		use<IFuture<void>> WriteError(cppcomponents::error_code);
		use<IFuture<T>> Read();
		void Close();

		CPPCOMPONENTS_CONSTRUCT_TEMPLATE(IChannel, Write, WriteError, Read, Close);
	};


	inline std::string ChannelDummyId(){ return "cppcomponents::uuid<0x158d0d7c, 0xcaf2, 0x4301, 0xa192, 0x0e2509204e93>"; }

	template<class T>
	struct implement_channel : implement_runtime_class <
		implement_channel<T>, runtime_class < ChannelDummyId, object_interfaces < IChannel < T >>
		>>
	{
		typedef IPromise<T> ipromise_t;
		typedef IPromise<void> ipromise_void_t;
		low_lock_queue<use<ipromise_t>> reader_promise_queue_;
		low_lock_queue<use<ipromise_void_t>> writer_promise_queue_;

		std::atomic<unsigned int> read_write_count_;
		std::atomic<bool> closed_;

		implement_channel()
			: read_write_count_{ 0 }, closed_{ false }
		{}

		struct read_write_counter{
			implement_channel* imp_;
			read_write_counter(implement_channel* i) : imp_{ i }{
				imp_->read_write_count_.fetch_add(1);
			}

			~read_write_counter(){
				imp_->read_write_count_.fetch_sub(1);
			}
		};

		use<IFuture<void>> Write(T t){
			if (closed_.load()) return make_error_future<void>(cppcomponents::error_abort::ec);


			use<ipromise_t> rp;
			if ((reader_promise_queue_.consume(rp))){
				assert(rp);
				rp.Set(t);
				return make_ready_future();
			}
			else{

				auto p = implement_future_promise<void>::create().QueryInterface<ipromise_void_t>();
				{
					read_write_counter counter{ this };
					writer_promise_queue_.produce(p);
				}
				auto f = p.QueryInterface<IFuture<void>>();
				auto rpq = &reader_promise_queue_;
				return f.Then([t, rpq](use < IFuture < void >> f){
					f.Get();
					use<ipromise_t> p;
					assert(rpq->consume(p));
					assert(p);
					p.Set(t);
				});

			}
		}
		use<IFuture<void>> WriteError(cppcomponents::error_code e){
			if (closed_.load()) return make_error_future<void>(cppcomponents::error_abort::ec);


			use<ipromise_t> rp;
			if ((reader_promise_queue_.consume(rp))){
				assert(rp);
				rp.SetError(e);
				return make_ready_future();
			}
			else{

				auto p = implement_future_promise<void>::create().QueryInterface<ipromise_void_t>();
				{
					read_write_counter counter{ this };
					writer_promise_queue_.produce(p);
				}
				auto f = p.QueryInterface<IFuture<void>>();
				auto rpq = &reader_promise_queue_;
				return f.Then([e, rpq](use < IFuture < void >> f){
					f.Get();
					use<ipromise_t> p;
					assert(rpq->consume(p));
					assert(p);
					p.SetError(e);
				});

			}
		}

		use<IFuture<T>> Read(){
			if (closed_.load()) return make_error_future<T>(cppcomponents::error_abort::ec);

			auto pr = implement_future_promise<T>::create().template QueryInterface<ipromise_t>();
			{
				read_write_counter counter{ this };
				reader_promise_queue_.produce(pr);
			}
			use<ipromise_void_t> p;
			if (writer_promise_queue_.consume(p)){
				if (p){
					p.Set();
				}
			}
			auto f = pr. template QueryInterface<IFuture<T>>();
			return f.Then([](use < IFuture < T >> f){
				return f.Get();
			});
		}

		void Close(){
			closed_.store(true);

			// Busy wait for all read/writes to complete
			while (read_write_count_.load() > 0);
			// Clear out all the writes
			use<ipromise_void_t> p;
			while (writer_promise_queue_.consume(p)){
				if (p){
					p.SetError(cppcomponents::error_abort::ec);
				}
			}

			// Clear out all the reads
			use<ipromise_t> rp;
			while (reader_promise_queue_.consume(rp)){
				if (rp){
					rp.SetError(cppcomponents::error_abort::ec);
				}
			}

		}
		~implement_channel(){
			Close();
		}

	};


	template<class T>
	struct unique_channel : use<IChannel<T>>{
		typedef use<IChannel<T>> base_t;
		unique_channel(use < IChannel < T >> chan) : base_t{ chan }
		{}

		~unique_channel()
		{
			if (*this){
				this->Close();
			}
		}

		unique_channel(unique_channel && other) : base_t{ std::move(static_cast<base_t>(other)) }
		{
			other.release();
		}

		unique_channel& operator=(unique_channel && other){
			static_cast<base_t&>(*this) = std::move(static_cast<base_t>(other));
			other.release();
			return *this;
		}

		void release(){
			static_cast<base_t&>(*this) = nullptr;
		}

	private:
		unique_channel(const unique_channel&);
		unique_channel& operator=(const unique_channel&) ;
	};

	template<class T>
	unique_channel<T> make_channel(){
		return implement_channel<T>::create().template QueryInterface<IChannel<T>>();
	}



}



#endif