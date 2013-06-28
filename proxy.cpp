#include <event.h>
#include <evhttp.h>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <curl/curl.h>
#include <pthread.h>
#include <iomanip>

using namespace std;

struct arg
{
    int start;
    int end;
    string url;
    evbuffer* data;
};

string int_to_str(int i)
{
    ostringstream str;
    str << i;
    return str.str();
}

size_t write_data(void* bufptr, size_t size, size_t nmemb, void* stream)
{
    evbuffer* buffer = (evbuffer*)stream;
    int length = size * nmemb;
    evbuffer_add(buffer, (char*)bufptr, length);
    return length;
}

void* get_chunk(void* ptr)
{
    arg* args = (arg*)ptr;
    CURL* curl = curl_easy_init();
    curl_slist* headers = NULL;
    ostringstream str;
    str << "Range: bytes=" << args->start << "-" << args->end;
    headers = curl_slist_append(headers, str.str().c_str());
    curl_easy_setopt(curl, CURLOPT_URL, args->url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, args->data);
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

void get_multi_threading(string url, evhttp_request* req)
{
    evbuffer* buffer = evbuffer_new();
    const int THREAD_NUMBER = 5;
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    curl_easy_perform(curl);
    int position[THREAD_NUMBER] = {0};
    double content_length;
    const char* content_type;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
    int mod_length = (int)content_length % THREAD_NUMBER;
    int per_chunk = (int)content_length / THREAD_NUMBER;
    for (int i = 1; i <= THREAD_NUMBER; i++)
    {
        if (i == THREAD_NUMBER && position[i - 1] + per_chunk < content_length)
        {
            position[i] = content_length;
        }
        else
        {
            position[i] = position[i - 1] + per_chunk;
        }
    }
    typedef evbuffer* bufptr;
    bufptr chunks[THREAD_NUMBER];
    pthread_t pids[THREAD_NUMBER];
    arg args[THREAD_NUMBER];
    for (int i = 0; i < THREAD_NUMBER; i++)
    {
        chunks[i] = evbuffer_new();
        args[i].start = position[i];
        args[i].end = position[i + 1] - 1;
        args[i].data = chunks[i];
        args[i].url = url;
        pthread_create(pids + i, NULL, get_chunk, args + i);
    }
    for (int i = 0; i < THREAD_NUMBER; i++)
    {
        pthread_join(pids[i], NULL);
        evbuffer_add_buffer(buffer, chunks[i]);
        evbuffer_free(chunks[i]);
    }
    evhttp_add_header(req->output_headers, "Content-Length", int_to_str(int(content_length)).c_str());
    if (content_type)
    {
        evhttp_add_header(req->output_headers, "Content-Type", content_type);
    }
    evhttp_send_reply(req, HTTP_OK, "OK", buffer);
    evbuffer_free(buffer);
    return;
}

void cb_handler(evhttp_request* req, void* arg)
{
    string uri(evhttp_request_uri(req));
    if (true)
    {
        get_multi_threading(uri, req);
        return;
    }
    evbuffer* buffer = evbuffer_new();
    CURL* curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)buffer);
    curl_easy_perform(curl);
    const char* content_type = NULL;
    double content_length;
    curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type);
    curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_length);
    if (content_type)
    {
        evhttp_add_header(req->output_headers, "Content-Type", content_type);
    }
    evhttp_add_header(req->output_headers, "Content-Length", int_to_str(int(content_length)).c_str());
    curl_easy_cleanup(curl);
    evhttp_send_reply(req, HTTP_OK, "OK", buffer);
    evbuffer_free(buffer);
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        cout << "Usage: ./proxy port\n";
        return 1;
    }
    int port = atoi(argv[1]);
    event_init();
    evhttp* httpd = evhttp_start("0.0.0.0", port);
    if (!httpd)
    {
        cout << "Unable to listen on 0.0.0.0:" << port;
        return 1;
    }
    curl_global_init(CURL_GLOBAL_ALL);
    //evhttp_set_timeout(httpd, 5);
    evhttp_set_gencb(httpd, cb_handler, NULL);
    event_dispatch();
    evhttp_free(httpd);
    return 0;
}
