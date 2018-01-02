#ifndef __URL_UPLOADER_SIGNAL_H
#define __URL_UPLOADER_SIGNAL_H

#include<process.h>
#include<Windows.h>

namespace url_upload
{
	
	class noncopyable
	{
	public:
		noncopyable() = default;
		noncopyable(const noncopyable&) = delete;
		noncopyable& operator=(noncopyable const&) = delete;
	};

	class signal: public noncopyable
	{
	private:
		HANDLE handle;
	public:

		signal() 
		{
			handle = CreateSemaphore(nullptr, 0, 1, TEXT("url_uploader_signal"));
		}

		
		void wait()
		{
			WaitForSingleObject(handle, INFINITE);
		}

		
		void notify_one()
		{
			ReleaseSemaphore(handle,1,nullptr);
		}

		
		~signal()
		{
			CloseHandle(handle);
		}
	};

}
#endif