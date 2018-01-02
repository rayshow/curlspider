#ifndef __URL_UPLOADER_H
#define __URL_UPLOADER_H

//std headerSs
#include<thread>
#include<functional>
#include<exception>
#include<mutex>
#include<list>


//3rd lockfree queue
#include<concurrentqueue/concurrentqueue.h>
//3rd libcurl
#include<curl/multi.h>
//3rd rapidjson
#include<rapidjson/rapidjson.h>
#include<rapidjson/document.h>
#include<rapidjson/stringbuffer.h>
#include<rapidjson/writer.h>


#include"signal.h"

namespace url_upload
{
#ifdef _DEBUG
	#define DebugInfo(s, ...) printf(s, __VA_ARGS__);
#elif 
	#define DebugInfo(s, ...) 
#endif


	typedef std::pair<std::string, std::string> post_item;

	static size_t cb(char *d, size_t n, size_t l, void *p)
	{
		/* take care of the data here, ignored in this example */
		(void)d;
		(void)p;
		return n*l;
	}

	class string_pool
	{
	private:
		std::list<std::string> pool;
		std::mutex access_mutex;
		enum { default_size = 10000 };
		enum { default_string_size = 128 };
		
	public:
		string_pool()
			:pool( default_size, std::string(default_string_size, '\0') ) //first 
		{
		}

		std::size_t capcity()
		{
			return pool.size();
		}

		std::string get_string(const char * content)
		{
			//read only operation
			std::size_t len = strlen(content);
		
			//read or change list need sync
			std::lock_guard<std::mutex> scope_lock{ access_mutex };

			//pool empty
			if (pool.empty())
			{
				std::string ret( default_string_size, '\0' );
				ret.assign(content, len);
				return std::move(ret);
			}

			//pooled string not len enough
			std::size_t front_len = pool.front().capacity()-1;
			if (len > front_len)
			{
				std::string ret(default_string_size, '\0');
				ret.assign(content, len);
				return std::move(ret);
			}

			std::string ret = std::move(pool.front());
			pool.pop_front();
			ret.assign(content, len);
			return std::move( ret );    //move
		}
		
		void put_string(std::string&& str)
		{
			std::lock_guard<std::mutex> scope_lock{ access_mutex };
			//DebugInfo(" *** string len : %d ", str.capacity());
			if(pool.size() <= default_size)
				pool.push_back( std::move(str) );
			DebugInfo(" *************** size %d \n", pool.size());
		}

	};

	class url_uploader
	{
	private:
		signal signal;
		CURL*  curl_handle;
		std::thread upload_thread;
		bool is_running; 
		moodycamel::ConcurrentQueue< std::string > post_queue;
		string_pool pool;
		std::string url;
		enum{ default_send_size = 500 };

		bool do_post( const char* post_field, int len)
		{
			//DebugInfo(" <== posting: url %s  post_field: %s\n", url.c_str(), post_field );
			int ret = CURLE_OK;
			ret |= curl_easy_setopt(curl_handle, CURLOPT_URL, url.c_str() );
			ret |= curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
			ret |= curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDSIZE, len );
			ret |= curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, post_field );
			ret |= curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, cb);  //back data of post, ingore
			ret |= curl_easy_perform(curl_handle);
			if (ret != CURLE_OK) return false;
			return true;
		}

		void running()
		{
			DebugInfo("%s\n", "thread running");
			std::string item;
			int item_count = 0;

			rapidjson::Document doc{ rapidjson::kArrayType };
			rapidjson::StringBuffer buffer;
			rapidjson::Writer<rapidjson::StringBuffer> writer{ buffer };

			while (is_running)
			{
				DebugInfo(" <== consume running... \n");
				signal.wait();
				while (post_queue.try_dequeue(item) && item_count<default_send_size )
				{
					rapidjson::Value join_item{ rapidjson::kStringType };
					join_item.SetString(item.c_str(), item.length(), doc.GetAllocator());
					doc.PushBack(join_item, doc.GetAllocator());
					++item_count;
					pool.put_string(std::move(item));
				}

				//packed 
				if (item_count >= default_send_size)
				{
					//DebugInfo(" <======================================= post_count: %d posted state: %d %s \n", item_count,  state, buffer.GetString() );
					doc.Accept(writer);
					int state = do_post(buffer.GetString(), buffer.GetLength());
					item_count = 0;
					doc.Clear();
					doc.SetArray();
					buffer.Clear();
					writer.Reset(buffer);
					doc.GetAllocator().Clear();
				}
			}
		}

	public:
		//throw bad_alloc if curl init failed
		url_uploader(const char* inURL):
			signal{},
			curl_handle{nullptr},
			upload_thread{},
			url{ inURL },
			pool{}
		{
			DebugInfo("%s\n", "uploader initalizing");
			curl_global_init(CURL_GLOBAL_ALL);
			curl_handle = curl_easy_init();
			if (!curl_handle)
			{
				DebugInfo("%s\n", "initalize failed");
				throw std::bad_alloc{};
			}
			is_running = true;
			upload_thread = std::thread{ std::bind(&url_uploader::running, this) };
			DebugInfo("%s\n", "uploader initalized");
		}

		void post( const char* post_field )
		{
			post_queue.enqueue( std::move(pool.get_string(post_field)) );
			signal.notify_one();
		}

		~url_uploader()
		{
			//wait thread to exit
			is_running = false;
			signal.notify_one();
			if(upload_thread.joinable())
				upload_thread.join();
			DebugInfo("%s\n", " uploader thread end");
			curl_easy_cleanup(curl_handle);
			curl_handle = nullptr;
			curl_global_cleanup();
			DebugInfo("%s\n", " uploader curl end");
		}
	};
}





#endif