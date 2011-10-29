#ifndef MINIHTTPSOCKET_H
#define MINIHTTPSOCKET_H

#include <string>
#include <set>
#include <queue>

namespace minihttp
{


enum Code
{
    HTTP_NULL = 0,       // used as a generic "something in the code is wrong" indicator, or initial value
    HTTP_OK = 200,
    HTTP_NOTFOUND = 404,
};


bool InitNetwork();
void StopNetwork();

bool SplitURI(const std::string& uri, std::string& host, std::string& file);

class HttpSocket;

// called back whenever data are received.
// (A) - char* != NULL, unsigned int != 0 --> data were received, readable from char*
// (B) - char* != NULL, unsigned int == 0 --> all data from a request were received
// (C) - char* == NULL, unsigned int == 0 --> the socket was closed, either by remote, or by us.
typedef void (*recv_callback)(HttpSocket*, char*, unsigned int, void*);

// called once when the socket is deleted. used by SocketSet.
typedef void (*del_callback)(HttpSocket*, void*);

// used by the socket to autodelete userdata if not NULL
typedef void (*deletor)(void*);

struct Request
{
    Request() : cb(NULL), user(NULL), dtor(NULL) {}
    Request(const std::string& r, recv_callback c = NULL, void *u = NULL, deletor d = NULL) 
        : resource(r), cb(c), user(u), dtor(d) {}

    std::string header; // set by socket
    std::string resource;
    recv_callback cb;
    void *user;
    deletor dtor;
};

class HttpSocket
{
public:

    HttpSocket();
    ~HttpSocket();

    bool open(const char *addr = NULL, unsigned int port = 0);
    void close(void);
    bool update(void); // returns true if something interesting happened (incoming data, closed connection, etc)
    bool isOpen(void);

    void SetKeepAlive(unsigned int secs) { _keep_alive = secs; }
    void SetUserAgent(const std::string &s) { _user_agent = s; }
    void SetAcceptEncoding(const std::string& s) { _accept_encoding = s; }
    bool SetNonBlocking(bool nonblock);
    void SetBufsizeIn(unsigned int s);
    void SetCallback(recv_callback f, void *user = NULL, deletor dtor = NULL);
    void SetDelSelf(bool del) { _delSelf = del; }
    void SetDelCallback(del_callback f, void *user = NULL);

    bool SendGet(Request& what, bool enqueue);
    bool SendGet(const std::string what, recv_callback cb = NULL, void *user = NULL, deletor dtor = NULL);
    bool QueueGet(const std::string what, recv_callback cb = NULL, void *user = NULL, deletor dtor = NULL);
    bool SendBytes(const char *str, unsigned int len);

    const char *GetHost(void) { return _host.c_str(); }
    unsigned int GetRemaining(void) { return _remaining; }
    unsigned int GetBufSize(void) { return _inbufSize; }
    unsigned int GetRecvSize(void) { return _inbufSize; }
    unsigned int GetStatusCode(void) { return _status; }
    unsigned int GetContentLen(void) { return _contentLen; }
    bool ChunkedTransfer(void) { return _chunkedTransfer; }
    bool ExpectMoreData(void) { return _remaining || _chunkedTransfer; }

protected:

    void _OnRecv(void);
    void _DoCallback(char *buf, unsigned int bytes);
    void _ProcessChunk(void);
    void _ShiftBuffer(void);
    bool _EnqueueOrSend(const Request& req, bool forceQueue = false);
    void _DequeueMore(void);
    void _ParseHeader(void);
    void _CleanupLastRequest(void);

    std::string _host;
    std::string _user_agent;
    std::string _accept_encoding;
    std::string _tmpHdr; // used to save the http header if the incoming buffer was not large enough
    char *_inbuf;
    char *_readptr; // part of inbuf, optionally skipped header
    char *_writeptr; // passed to recv(). usually equal to _inbuf, but may point inside the buffer in case of a partial transfer.

    // generic data received callback
    recv_callback _cb;
    void *_cb_data;
    deletor _cb_dtor;

    // self-deletion callback
    del_callback _del_cb;
    void *_del_cb_data;

    // http request-specific callback, set each time a request is processed
    recv_callback _http_cb;
    void *_http_cb_data;
    deletor _http_cb_dtor;

    unsigned int _inbufSize; // size of internal buffer
    unsigned int _writeSize; // how many bytes can be written to _writeptr;
    unsigned int _keep_alive; // http related
    unsigned int _recvSize; // incoming size, max _inbufSize - 1
    unsigned int _lastport; // port used in last open() call
    unsigned int _remaining; // http "Content-Length: X" - already recvd. 0 if ready for next packet.
                             // For chunked transfer encoding, this holds the remaining size of the current chunk
    unsigned int _contentLen; // as reported by server
    unsigned int _status; // http status code, HTTP_OK if things are good
    bool _nonblocking;
    bool _delSelf; // delete self on close?

    std::queue<Request> _requestQ;

    bool _chunkedTransfer;
    bool _mustClose;
    bool _inProgress;

    void *_s; // socket handle. really an int, but to be sure its 64 bit compatible as it seems required on windows, we use this.
};


class SocketSet
{
public:
    virtual ~SocketSet();
    void deleteAll(void);
    bool update(void);
    void add(HttpSocket *s);
    void remove(HttpSocket *s);
    void _remove(std::set<HttpSocket*>::iterator &it);
    inline size_t size(void) { return _store.size(); }

protected:
    std::set<HttpSocket*> _store;
};


} // namespace minihttp


#endif
