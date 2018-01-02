
#include"upload.h"
#include"url_uploader.h"

url_upload::url_uploader* uploader = nullptr;

bool url_upload_init(const char* url)
{
	try
	{
		uploader = new url_upload::url_uploader{ url };
	}
	catch (...)
	{
		if (uploader)
			delete uploader;
		return false;
	}
	return true;
}

void url_upload_post( const char* post_data)
{
	if(uploader)
		uploader->post(post_data );
}

void url_upload_cleanup()
{
	if (uploader)
	{
		delete uploader;
		uploader = nullptr;
	}
}