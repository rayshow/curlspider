#ifndef __URL_UPLOADER_DLLMAIN_H
#define __URL_UPLOADER_DLLMAIN_H


#if defined(WIN32) || defined(_WIN32) || defined(__SYMBIAN32__)
#  if defined(UPLOAD_LIB) //
#    define URL_UPLOAD_EXTERN  __declspec(dllexport)
#  else
#    define URL_UPLOAD_EXTERN  __declspec(dllimport)
#  endif
#endif

URL_UPLOAD_EXTERN bool url_upload_init(const char* url);

URL_UPLOAD_EXTERN void url_upload_post(const char* post_field);

URL_UPLOAD_EXTERN void url_upload_cleanup();

#endif
