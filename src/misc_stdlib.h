#ifndef MISC_STDLIB_H_INCLUDED
#define MISC_STDLIB_H_INCLUDED

int rm_r( const char* );

int mkdir_p( const char* );

char* get_exe_dir( void );

char* get_base_dir( void );


typedef struct _CurlUserData {
  gchar *response;
  size_t size;
} CurlUserData;

size_t
curl_write_cb( void *data, size_t size, size_t nmemb, void *clientp )
{
  size_t realsize = size * nmemb;
  CurlUserData* mem = (CurlUserData*) clientp;

  char *ptr = realloc(mem->response, mem->size + realsize + 1);
  if(ptr == NULL)
    return 0;  /* out of memory! */

  mem->response = ptr;
  memcpy(&(mem->response[mem->size]), data, realsize);
  mem->size += realsize;
  mem->response[mem->size] = 0;

  return realsize;
}


#endif // MISC_STDLIB_H_INCLUDED

