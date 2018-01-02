#include<upload.h>
#include<cstdio>

#include<mutex>
#include<string>

#include<url_uploader.h>

#include<thread>
#include<rapidjson/rapidjson.h>
#include<rapidjson/document.h>
#include<rapidjson/stringbuffer.h>
#include<rapidjson/writer.h>


bool stop = false;
std::mutex mtx;
int index = 0;

void post_index(int thread_index)
{
	while (!stop /*&& index<10*/ )
	{ 
		url_upload_post("account=1157988949@qq.com&passwd=1234567");
		mtx.lock();
		//std::string url{ std::string{ "thread:" }+std::to_string(thread_index) + " post: " + std::to_string(index++) };
		//printf(" ==> create : %s \n", url.c_str());
		printf(" ==> thread %d create %d \n", thread_index, index++);

		mtx.unlock();

		
		std::this_thread::sleep_for(std::chrono::duration<int, std::ratio<1,1000> >{ 10 });
	}
}


int main()
{
	printf("press any key to start, and again press any key to exits");
	getchar();

	if (!url_upload_init("47.89.246.81:8080/DatingHelper/user/login.do" ))
	{
		printf("init failed");
		return 0;
	}

	///*mutl-thread post test, always failed*/
	getchar();
	std::thread threads[4];
	for (int i = 0; i < 4; ++i)
	{
		threads[i] = std::thread{ post_index, i };
	}

	/*clean up and exist*/
	getchar();
	stop = true;
	for (int i = 0; i < 4; ++i)
	{
		if (threads[i].joinable())
		{
			threads[i].join();
		}
	}

	/*post success test*/
	url_upload_post( "account=1157988949@qq.com&passwd=1234567");

	url_upload_cleanup();
	getchar();
}

//
//void run()
//{
//	rapidjson::Document doc;
//	const char* hl = "hello,world";
//	int len = strlen(hl);
//
//	int times = 0;
//	while (true)
//	{
//		{
//
//			rapidjson::Value val{ rapidjson::kStringType };
//			val.SetString(hl, len, doc.GetAllocator());
//			//printf("construct %d times \n", times++);
//		}
//
//		//std::this_thread::sleep_for(std::chrono::duration<int, std::ratio<1, 1000> >{ 1 });
//	}
//}
//
//int main()
//{
//	std::thread th{ run };
//
//	getchar();
//
//	th.join();
//}


//#include<rapidjson/rapidjson.h>
//#include<rapidjson/document.h>
//#include<rapidjson/stringbuffer.h>
//#include<rapidjson/writer.h>
//
//using namespace rapidjson;
//
//int main()
//{
//
//	Document doc;
//	Value& v = doc.SetArray();
//	std::string name1{ "hello" };
//	std::string name2{ "world" };
//	std::string name3{ "are" };
//	std::string name4{ "you" };
//
//	Value v1,v2,v3,v4;
//	v1.SetString(name1.c_str(), name1.length(), doc.GetAllocator());
//	v2.SetString(name1.c_str(), name1.length(), doc.GetAllocator());
//	v3.SetString(name1.c_str(), name1.length(), doc.GetAllocator());
//	v4.SetString(name1.c_str(), name1.length(), doc.GetAllocator());
//	printf("%d %s %d", v1.GetType() == kStringType, v1.GetString(), v1.GetStringLength());
//
//	v.PushBack(v1, doc.GetAllocator());
//	printf("%d", v1.GetType() == kNullType);
//	v1.SetString(name1.c_str(), name1.length(), doc.GetAllocator());
//
//	assert(v.GetType() == kArrayType);
//
//	v.Clear();
//	v.PushBack(v1, doc.GetAllocator());
//	
//
//	v.PushBack(v2, doc.GetAllocator());
//	v.PushBack(v3, doc.GetAllocator());
//	v.PushBack(v4, doc.GetAllocator());
//	StringBuffer buffer;
//	Writer<StringBuffer> writer;
//	writer.Reset(buffer);
//
//	doc.Accept(writer);
//	printf("%s \n", buffer.GetString());
//	buffer.Clear();
//	writer.Reset(buffer);
//	
//	doc.Accept(writer);
//	printf("%s \n", buffer.GetString());
//
//	//name1 = "xxxxx";
//	//name2 = "tttttttttttt";
//	//name3 = "fffffffffff";
//	//v1.SetString(name1.c_str(), name1.length(), doc.GetAllocator());
//	//v2.SetString(name2.c_str(), name2.length(), doc.GetAllocator());
//	//v3.SetString(name3.c_str(), name3.length(), doc.GetAllocator());
//	//v.Clear();
//	//v.PushBack(v1, doc.GetAllocator());
//	//v.PushBack(v2, doc.GetAllocator());
//	//v.PushBack(v3, doc.GetAllocator());
//
//
//
//	getchar();
//
//}