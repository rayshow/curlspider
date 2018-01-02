#pragma once

#include<curl/multi.h>
#include<concurrentqueue/concurrentqueue.h>
#include<functional>
#include<algorithm>
#include<thread>
#include<exception>
#include<mutex>
#include<condition_variable>
#include<list>
#include<memory>
#include<vector>
#include<chrono>
#include<unordered_map>
#include<exception>
#include<stdexcept>
#include<memory>
#include<cstdio>
#include<cassert>
#include<utility>

#pragma warning(disable: 4996 )

class string_pool
{
private:
	std::list<std::string> pool; /* bi-list */
	std::mutex access_mutex;
	enum { default_size = 10000 };
	enum { default_string_size = 128 };

public:
	string_pool()
		:pool(default_size, std::string(default_string_size, '\0')) /* initalize pool with 0-filled string */
	{
	}

	std::size_t capcity()
	{
		return pool.size();
	}

	std::string get_string(const char * content)
	{
		/* read only operation */
		std::size_t len = strlen(content);

		/* read or change list need sync */
		std::lock_guard<std::mutex> scope_lock{ access_mutex };

		/* pool empty */
		if (pool.empty())
		{
			std::string ret(default_string_size, '\0');
			ret.assign(content, len);
			return std::move(ret);
		}

		/* pooled string not len enough */
		std::size_t front_len = pool.front().capacity() - 1;
		if (len > front_len)
		{
			std::string ret(default_string_size, '\0');
			ret.assign(content, len);
			return std::move(ret);
		}

		std::string ret = std::move(pool.front());
		pool.pop_front();
		ret.assign(content, len);
		return std::move(ret);    
	}

	void put_string(std::string&& str)
	{
		std::lock_guard<std::mutex> scope_lock{ access_mutex };
		
		if (pool.size() <= default_size)
			pool.push_back(std::move(str));
		//fprintf(stderr, " *************** size %d \n", pool.size());
	}
};

inline int trace_curl_state(CURL *handle, curl_infotype type, unsigned char *data, size_t size, void *userp)
{
	(void)userp;
	(void)handle; /* prevent compiler warning */

	const char *text;
	switch (type) {
	case CURLINFO_TEXT:
		fprintf(stderr, "== Info: %s", data);

	case CURLINFO_HEADER_OUT:
		text = "=> Send header";
		break;
	case CURLINFO_DATA_OUT:
		text = "=> Send data";
		break;
	case CURLINFO_HEADER_IN:
		text = "<= Recv header";
		break;
	case CURLINFO_DATA_IN:
		text = "<= Recv data";
		break;
	default:
		return 0;
	}

	fprintf(stderr, "%s, %10.10ld bytes (0x%8.8lx)\n", text, size, size);
	return 0;
}


inline int write_fn(char *d, size_t n, size_t l, void *p)
{
	int index = (int)p;

	FILE* file;
	char filename[128];
	snprintf(filename, 128, "download/file_%d.html", index);
	if (!(file = fopen(filename, "a+")))
	{
		fprintf(stderr, "XXXX open file %s fail \n", filename);
		return 0;
	}
	size_t size =  fwrite(d, n, l, file);
	fclose(file);
	return size;
}


class bad_string_length : public std::exception
{
public:
	bad_string_length() {}

	virtual char const* what() const override
	{
		return "bad string length ";
	}
};

/*for easy access*/
struct request
{
	bool              is_post;
	int               retry_times;  /* for time out retry */
	CURL*             curl_handle;
	std::string       request_url;
	std::string       post_data;
	char*             download_data;
	int               download_data_size;
	int               allocated_data_size;

	
	/* default  */
	request()
		: is_post{false}, 
		retry_times{0},
		curl_handle{nullptr}, 
		request_url{},
		post_data{},
		download_data{nullptr},
		download_data_size{0},
		allocated_data_size{0}
	{}

	/* for re-use */
	void reset()
	{
		retry_times = 0;
		curl_handle = nullptr;
		/* allocated memory clear */


		if (download_data)
		{
			memset(download_data, 0, download_data_size);
		}
	}

	void set_request(std::string&& url)
	{
		request_url = std::move(url);
	}

	void set_request_post(std::string&& url, std::string&& data)
	{
		request_url = std::move(url);
		post_data = std::move(data);
	}

	int append_download_data(char *data, int size)
	{
		/*  memory enough, just copy */
		if (download_data &&  download_data_size + size <= allocated_data_size)
		{
			memcpy(download_data + download_data_size, data, size);
			download_data_size += size;
			return;
		}

		int new_allocate_size = 1024 * 1024; /* pre-allocate 1m memory */

		/* allocated but not enough, free*/
		if (download_data && download_data_size + size <= allocated_data_size)
		{
			free(download_data);
			new_allocate_size = (int)((download_data_size + size)*1.5f); /* increasing to 1.5 times of new size */
		}

		download_data = (char*)malloc(new_allocate_size);
		/* alloc failed */
		if (!download_data)
		{
			return false;
		}
		memset(download_data, 0, new_allocate_size);
		memcpy(download_data, data, size);
		allocated_data_size = new_allocate_size;
	}

	bool retry_request(int max_retry_times)
	{
		if (++retry_times > max_retry_times) return false;
		return true;
	}
};


class downloader
{
private:

	typedef std::tuple<bool, std::string, std::string> request_item;

	enum { max_easy_count = 5 };
	CURLM* curlm;
	CURL* curle[max_easy_count];
	std::thread work_thread;
	std::mutex signal_mutex;
	std::mutex que_mutex;
	std::condition_variable condi;
	std::unordered_map<CURL*, request*> requests;
	bool is_running;
	moodycamel::ConcurrentQueue<request_item> send_que;  /* move in and out */
	moodycamel::ConcurrentQueue<CURL*> free_curls;
	string_pool pool;
	int index;
	
	/* setup a curl handle ready to send */
	void curl_set(CURL* curl, const request_item& item)
	{
		//curl_easy_reset(curl);
		curl_easy_setopt(curl, CURLOPT_URL, std::get<1>(item).c_str());
		curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, trace_curl_state);
		curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, index++);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_fn);
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60);                 //30s time out

		/* post request */
		if (std::get<0>(item))
		{
			auto& post_data = std::get<2>(item);
			curl_easy_setopt(curl, CURLOPT_POST, 1L);
			curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, post_data.length());
			curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
		}

		curl_multi_add_handle(curlm, curl);

		//fprintf(stderr, "<=== setup easy curl handle \n");
	}

	/* select curlm, false if error occur */
	bool curl_select(CURLM* curlm, fd_set& r, fd_set& w, fd_set& e, int& comp, int& timeout, CURLMcode& code)
	{
		FD_ZERO(&r);
		FD_ZERO(&w);
		FD_ZERO(&e);
		int max_fd;
		if ( ( code = curl_multi_fdset(curlm, &r, &w, &e, &max_fd)) )
		{
			fprintf(stderr, "curl fdset error %s\n", curl_multi_strerror(code));
			return false;
		}

		/* no file set */
	/*	if (max_fd == -1)
		{
			fprintf(stderr, "curl maxfd error %s\n", curl_multi_strerror(code));
			return false;
		}*/

		long tm;
		if ((code = curl_multi_timeout(curlm, &tm)))
		{
			fprintf(stderr, "curl timeout error %s\n", curl_multi_strerror(code));
			return false;
		}

		//if (tm <= 0) tm = 10;  /* wait 100ms */

		//struct timeval tv;
		//tv.tv_sec = tm / 1000;
		//tv.tv_usec = (tm % 1000) * 1000;
		//if (select(max_fd + 1, &r, &w, &e, &tv) < 0)
		//{
		//	fprintf(stderr, "curl select error read:%d write:%d exception:%d %s\n", r.fd_count, w.fd_count, e.fd_count, curl_multi_strerror(code));
		//	//return false;
		//}

		/* select timeout or have r/w ready */
		CURLMsg* msg_item;
		int msg_in_que;

		static int count = 0;
		while ((msg_item = curl_multi_info_read(curlm, &msg_in_que)))
		{
			if (msg_item && msg_item->msg == CURLMSG_DONE)
			{
				if (msg_item->data.result)
				{
					++timeout;
					++count;
					fprintf(stderr, "count error %d\n", count);
					//fprintf(stderr, "curl msg item error %s\n", curl_easy_strerror(msg_item->data.result));

					CURL* curl = msg_item->easy_handle;
					code = curl_multi_remove_handle(curlm, curl);
					free_curls.enqueue(curl);

					continue;
				}

				/* remove from multi handle and re-put to queue */
				CURL* curl = msg_item->easy_handle;
				code = curl_multi_remove_handle(curlm, curl);
				free_curls.enqueue(curl);
				++comp;
				++count;
				fprintf(stderr, "count normal %d\n", count);
				//fprintf(stderr, "complement %d\n", comp);
			}
		}
		
		return true;
	}

	void running()
	{
		request_item item;
		CURL* free_curl = nullptr;
		int not_complete_count = 0;
		fd_set read_set, write_set, exc_set;
		int complement = 0;
		int timeout = 0;
		int send_count = 0;
		while (is_running)
		{
			//fprintf(stderr, "%s\n", "running...");

			/* some post still pending , need process so can't block */
			if (not_complete_count == 0)
			{
				std::unique_lock<std::mutex> uk{ signal_mutex };
				condi.wait(uk);
			}
		
			/* has free curl and item */
			send_count = 0;
			static int dequeue = 0;
			while ( send_que.try_dequeue(item))
			{
				++dequeue;
				fprintf(stderr, " dequeue %d\n", dequeue);

				/* until a curl handle is free  */
				while (!free_curls.try_dequeue(free_curl))
				{
					//printf("===> enter busy wait curl handle ready \n");
					CURLMcode code = curl_multi_perform(curlm, &not_complete_count);
					if (!curl_select(curlm, read_set, write_set, exc_set, complement, timeout, code))
					{
						fprintf(stderr, " xxx select failed\n");
						--dequeue;
						send_que.enqueue(std::move(item));
						break;
					}
					//fprintf(stderr, " xxx curl state %s\n", curl_multi_strerror(code));
					std::this_thread::yield();
					continue;
				}

				/* count for send at same time */
				send_count++;
				curl_set(free_curl, item);
				//fprintf(stderr, " <=== pendding url: %s\n", item.c_str());

				/*recycle string*/
				pool.put_string(std::move(std::get<1>(item)));
				pool.put_string(std::move(std::get<2>(item)));
			}
			 
			/* there some post need process or some request need process  */
			if (send_count > 0 || not_complete_count >0 )
			{
				//printf("===> enter batch curl perform\n");
				not_complete_count = 0;
				/* multi-send request */
				CURLMcode code =  curl_multi_perform(curlm, &not_complete_count);
				if (!curl_select(curlm, read_set, write_set, exc_set, complement, timeout, code))
				{
					fprintf(stderr, " xxx select failed\n");
				}
				//fprintf(stderr, " <=== perform batch  of %d items pending %d items complements %d\n", send_count, not_complete_count, complement);
			}
			else 
			{
				/* no enough curl handle or no posted item pendding, yield cpu*/
				std::this_thread::yield();
				continue;
			}
		}

		fprintf(stderr, " <=== perform batch  of %d items pending %d items complements %d timeout %d \n", send_count, not_complete_count, complement, timeout);
		fprintf(stderr, "%s\n", "download thread stop");
	}

	void clean_curl()
	{
		//clean up resource
		std::for_each(curle, curle + max_easy_count, [](CURL* curl) {
			if (curl)  curl_easy_cleanup(curl);
			curl = nullptr;
		});
		curl_multi_cleanup(curlm);
		curlm = nullptr;
	}

public:
	downloader()
	{
		curlm = curl_multi_init();
		if (!curlm)
		{
			fprintf(stderr, "%s\n", "initalize multi curl handle failed!");
			throw std::bad_alloc{};
		}
		try
		{
			memset(curle, 0, max_easy_count * sizeof(CURL*));
			std::for_each(curle, curle + max_easy_count, [&](CURL* curl) {
				curl = curl_easy_init();
				if (!curl)
				{
					fprintf(stderr, "%s\n", "initalize multi curl handle failed!");
					throw std::bad_alloc{};
				}
				free_curls.enqueue(curl);
			});
		}
		catch (...)
		{
			clean_curl();
			throw; 
		}

		index = 0;
		is_running = true;
		work_thread = std::thread{ std::bind(&downloader::running, this) };
	}


	void add_get_request(const char* url)
	{
		
		
		send_que.enqueue( std::make_tuple(false, std::move(pool.get_string(url)), std::move(pool.get_string(url)))  );        /* is thread safe */
		condi.notify_one();                         /* is thread safe */
	}

	void add_post_request(const char* url, const char* data)
	{

	}


	~downloader()
	{
		/* stop thread */
		is_running = false;
		condi.notify_one();
		if (work_thread.joinable())
		{
			work_thread.join();
		}
		fprintf(stderr, "%s\n", "downloader destruct");

		/*clear request*/
		for(auto& item : requests)
		{
			if (item.second)
			{
				delete item.second;
			}
		}
		/*clear curl */
		clean_curl();
	}
};


const char* url_format = "http://www.baidu.com/s?ie=utf-8&f=8&rsv_bp=1&ch=&tn=baiduerr&bar=&wd=%d";


int main()
{
	{
		std::unique_ptr<downloader> dl;
		try
		{
			dl.reset(new downloader{});
		}
		catch (std::bad_alloc& ba)
		{
			fprintf(stderr, "%s\n", "out of memory");
			getchar();
			return 0;
		}

		char url_buffer[128];
		for (int i = 0; i < 100; ++i)
		{
			memset(url_buffer, 0, 128);
			sprintf(url_buffer, url_format, i);
			dl->add_get_request(url_buffer);
			//sleep 5ms 
			//std::this_thread::sleep_for(std::chrono::duration<int, std::ratio<1, 1000> >{ 100 });
		}

		getchar();
	}
	

	getchar();
	
	return 0;
}