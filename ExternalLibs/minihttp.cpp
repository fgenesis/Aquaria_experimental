#ifdef _MSC_VER
#  ifndef _CRT_SECURE_NO_WARNINGS
#    define _CRT_SECURE_NO_WARNINGS
#  endif
#  ifndef _CRT_SECURE_NO_DEPRECATE
#    define _CRT_SECURE_NO_DEPRECATE
#  endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <cctype>
#include <cerrno>
#include <algorithm>

#ifdef _WIN32
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0501
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define EWOULDBLOCK WSAEWOULDBLOCK
#  define ETIMEDOUT WSAETIMEDOUT
#  define ECONNRESET WSAECONNRESET
#  define ENOTCONN WSAENOTCONN
#  define MAKESOCKET(s) ((SOCKET)(s))
#  define CASTSOCKET(s) ((void*)(s))
#else
#  include <sys/types.h>
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/socket.h>
#  include <netdb.h>
#  define SOCKET_ERROR -1
#  define INVALID_SOCKET -1
typedef int SOCKET;
#  define MAKESOCKET(s) ((SOCKET)(size_t)(s))
#  define CASTSOCKET(s) ((void*)(s))
#endif

#define SOCKETVALID(s) (MAKESOCKET(s) != INVALID_SOCKET)

#ifdef _MSC_VER
#  define STRNICMP _strnicmp
#else
#  define STRNICMP strncasecmp
#endif

#include "minihttp.h"

#ifdef _DEBUG
#  define traceprint(...) {printf(__VA_ARGS__);}
#else
#  define traceprint(...) {}
#endif

namespace minihttp {

#define DEFAULT_BUFSIZE 4096

inline int _GetError()
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

inline std::string _GetErrorStr(int e)
{
#ifdef _WIN32
    LPTSTR s;
    ::FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM, NULL, e, 0, (LPTSTR)&s, 0, NULL);
    std::string ret = s;
    ::LocalFree(s);
    return ret;
#endif
    return strerror(e);
}

bool InitNetwork()
{
#ifdef _WIN32
    WSADATA wsadata;
    if(WSAStartup(MAKEWORD(2,2), &wsadata))
    {
        traceprint("WSAStartup ERROR: %s", _GetErrorStr(_GetError()).c_str());
        return false;
    }
#endif
    return true;
}

void StopNetwork()
{
#ifdef _WIN32
    WSACleanup();
#endif
}

static bool _Resolve(const char *host, unsigned int port, struct sockaddr_in *addr)
{
    char port_str[15];
    sprintf(port_str, "%u", port);

    struct addrinfo hnt, *res = 0;
    memset(&hnt, 0, sizeof(hnt));
    hnt.ai_family = AF_INET;
    hnt.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port_str, &hnt, &res))
    {
        traceprint("RESOLVE ERROR: %s", _GetErrorStr(_GetError()).c_str());
        return false;
    }
    if (res)
    {
        if (res->ai_family != AF_INET)
        {
            traceprint("RESOLVE WTF: %s", _GetErrorStr(_GetError()).c_str());
            freeaddrinfo(res);
            return false;
        }
        memcpy(addr, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
        return true;
    }
    return false;
}

bool SplitURI(const std::string& uri, std::string& host, std::string& file)
{
    const char *p = uri.c_str();
    const char *sl = strstr(p, "//");
    unsigned int offs = 0;
    if(sl)
    {
        offs = 7;
        if(strncmp(p, "http://", offs))
            return false;
        p = sl + 2;
    }
    sl = strchr(p, '/');
    if(!sl)
        return false;
    host = uri.substr(offs, sl - p);
    file = sl;
    return true;
}

static bool _SetNonBlocking(SOCKET s, bool nonblock)
{
    if(!SOCKETVALID(s))
        return false;
#ifdef _WIN32
    ULONG tmp = !!nonblock;
    if(::ioctlsocket(MAKESOCKET(s), FIONBIO, &tmp) == SOCKET_ERROR)
        return false;
#else
    int tmp = ::fcntl(MAKESOCKET(s), F_GETFL);
    if(tmp < 0)
        return false;
    if(::fcntl(MAKESOCKET(s), F_SETFL, nonblock ? (tmp|O_NONBLOCK) : (tmp|=~O_NONBLOCK)) < 0)
        return false;
#endif
    return true;
}



HttpSocket::HttpSocket()
: _s(CASTSOCKET(INVALID_SOCKET)), _keep_alive(0), _inbuf(NULL), _inbufSize(0), _recvSize(0), _inProgress(false),
  _cb(NULL), _cb_data(NULL), _readptr(NULL), _lastport(0), _remaining(0), _delSelf(false),
  _del_cb(NULL), _del_cb_data(NULL), _writeptr(NULL), _chunkedTransfer(false), _mustClose(true),
  _http_cb(NULL), _http_cb_data(NULL), _contentLen(0), _cb_dtor(NULL), _http_cb_dtor(NULL)
{
}

HttpSocket::~HttpSocket()
{
    close();
    SetCallback(NULL, NULL, NULL); // trigger deletion of callback data, if present
    if(_inbuf)
        free(_inbuf);
}

bool HttpSocket::isOpen(void)
{
    return SOCKETVALID(_s);
}

void HttpSocket::close(void)
{
    if(!SOCKETVALID(_s))
        return;

    if(!_remaining && _inProgress)
        _DoCallback(_readptr, 0); // notify about finished request

    _inProgress = false;

    _DoCallback(NULL, 0);

#ifdef _WIN32
    ::closesocket((SOCKET)_s);
#else
    ::close(MAKESOCKET(_s));
#endif
    _s = CASTSOCKET(INVALID_SOCKET);

    _CleanupLastRequest();
    
    if(_delSelf && _requestQ.empty()) // if there are further requests, socket will reopen on next update() call
    {
        if(_del_cb)
            _del_cb(this, _del_cb_data);
        delete this;
    }
}

bool HttpSocket::SetNonBlocking(bool nonblock)
{
    _nonblocking = nonblock;
    return _SetNonBlocking(MAKESOCKET(_s), nonblock);
}

void HttpSocket::SetBufsizeIn(unsigned int s)
{
    if(s < 512)
        s = 512;
    if(s != _inbufSize)
        _inbuf = (char*)realloc(_inbuf, s);
    _inbufSize = s;
    _writeSize = s - 1;
    _readptr = _writeptr = _inbuf;
}

void HttpSocket::SetCallback(recv_callback f, void *user /* = NULL */, deletor dtor /* = NULL */ )
{
    if(_cb_dtor)
    {
        _cb_dtor(_cb_data); // call deletor for this socket's userdata
        _cb_dtor = NULL; // never delete twice, just in case
    }

    _cb = f;
    _cb_data = user;
    _cb_dtor = dtor;
}

void HttpSocket::SetDelCallback(del_callback f, void *user /* = NULL */)
{
    _del_cb = f;
    _del_cb_data = user;
}

bool HttpSocket::SendGet(const std::string& what, recv_callback cb /* = NULL */,
                         void *user /* = NULL */, deletor dtor /* = NULL */)
{
    Request req(what, cb, user, dtor);
    return SendGet(req, false);
}

bool HttpSocket::QueueGet(const std::string& what, recv_callback cb /* = NULL */,
                          void *user /* = NULL */, deletor dtor /* = NULL */)
{
    Request req(what, cb, user, dtor);
    return SendGet(req, true);
}

bool HttpSocket::SendGet(Request& req, bool enqueue)
{
    std::stringstream r;
    const char *crlf = "\r\n";
    r << "GET " << req.resource << " HTTP/1.1" << crlf;
    r << "Host: " << _host << crlf;
    if(_keep_alive)
    {
        r << "Connection: Keep-Alive" << crlf;
        r << "Keep-Alive: " << _keep_alive << crlf;
    }
    else
        r << "Connection: close" << crlf;

    if(_user_agent.length())
        r << "User-Agent: " << _user_agent << crlf;

    if(_accept_encoding.length())
        r << "Accept-Encoding: " << _accept_encoding << crlf;

    r << crlf; // header terminator

    req.header = r.str();

    return _EnqueueOrSend(req, enqueue);
}

bool HttpSocket::_EnqueueOrSend(const Request& req, bool forceQueue /* = false */)
{
    if(_inProgress || forceQueue) // do not send while receiving other data
    {
        traceprint("HTTP: Transfer pending; putting into queue. Now %u waiting.\n", (unsigned int)_requestQ.size()); // DEBUG
        _requestQ.push(req);
        return true;
    }
    // ok, we can send directly
    _CleanupLastRequest();
    _http_cb = req.cb;
    _http_cb_data = req.user;
    _http_cb_dtor = req.dtor;
    _inProgress = true;
    return SendBytes(req.header.c_str(), req.header.length());
}

// called whenever a request is finished completely and the socket checks for more things to send
void HttpSocket::_DequeueMore(void)
{
    if(_inProgress)
        _DoCallback(_readptr, 0); // notify about finished request

    if(_requestQ.size()) // still have other requests queued?
    {
        _CleanupLastRequest();
        const Request& req = _requestQ.front();
        _http_cb = req.cb;
        _http_cb_data = req.user;
        _http_cb_dtor = req.dtor;
        if(SendBytes(req.header.c_str(), req.header.size())) // could we send?
        {
            _requestQ.pop(); // if so, we are done with this request
            if(!_inProgress) // this should never happen
                puts("_DequeueMore(): something was in queue, but _inProgress == false, BAD!");
        }
    }
    else
        _inProgress = false; // we are done for now. socket is kept alive for future sends.
}

bool HttpSocket::SendBytes(const char *str, unsigned int len)
{
    if(!SOCKETVALID(_s))
        return false;
    //traceprint("SEND: '%s'\n", str);
    return ::send(MAKESOCKET(_s), str, len, 0) >= 0;
    // TODO: check _GetError()
}

bool HttpSocket::open(const char *host /* = NULL */, unsigned int port /* = 0 */)
{
    if(isOpen())
    {
        return (!host || host == _host) && (!port || port == _lastport); // ok if same settings, fail if other settings
    }

    sockaddr_in addr;

    if(host)
        _host = host;
    else
        host = _host.c_str();

    if(port)
        _lastport = port;
    else
    {
        port = _lastport;
        if(!port)
            return false;
    }

    if(!_Resolve(host, port, &addr))
    {
        traceprint("RESOLV ERROR: %s\n", _GetErrorStr(_GetError()).c_str());
        return false;
    }

    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    
    if(!SOCKETVALID(s))
    {
        traceprint("SOCKET ERROR: %s\n", _GetErrorStr(_GetError()).c_str());
        return false;
    }

    if (::connect(MAKESOCKET(s), (sockaddr*)&addr, sizeof(sockaddr)))
    {
        traceprint("CONNECT ERROR: %s\n", _GetErrorStr(_GetError()).c_str());
        return false;
    }

    _SetNonBlocking(s, _nonblocking); // restore setting if it was set in invalid state. static call because _s is intentionally still invalid here.
    _chunkedTransfer = false;
    _s = CASTSOCKET(s); // set the socket handle when we are really sure we are connected, and things are set up

    return true;
}

bool HttpSocket::update(void)
{
    // initiate transfer if queue is not empty, but the socket somehow forgot to proceed
    if(_requestQ.size() && !_inProgress)
    {
        if(!(isOpen() || open())) // if the socket is closed, try to re-open
            return false;
        _DequeueMore();
    }

    if(!SOCKETVALID(_s))
        return false;

    if(!_inbuf)
        SetBufsizeIn(DEFAULT_BUFSIZE);

    int bytes = recv(MAKESOCKET(_s), _writeptr, _writeSize, 0); // last char is used as string terminator

    if(bytes > 0) // we received something
    {
        _inbuf[bytes] = 0;
        _recvSize = bytes;
        _OnRecv();
        return true;
    }
    else if(bytes == 0) // remote has closed the connection
    {
        _recvSize = 0;
        close();
        return true;
    }
    else // whoops, error?
    {
        int e = _GetError();
        switch(e)
        {
            case ECONNRESET:
            case ENOTCONN:
            case ETIMEDOUT:
#ifdef _WIN32
            case WSAECONNABORTED:
            case WSAESHUTDOWN:
#endif
                close();
                break;

            case EWOULDBLOCK:
#if !defined(_WIN32) && (EWOULDBLOCK != EAGAIN)
            case EAGAIN: // linux man pages say this can also happen instead of EWOULDBLOCK
#endif
                return false;
        }
        traceprint("SOCKET UPDATE: %s\n", _GetErrorStr(e).c_str());
    }
    return true;
}

void HttpSocket::_CleanupLastRequest(void)
{
    if(_http_cb_dtor)
        _http_cb_dtor(_http_cb_data); // call deletor for this request's userdata

    _http_cb_dtor = NULL; // never try to delete twice, just in case
    _http_cb = NULL;
    _http_cb_data = NULL;
}

void HttpSocket::_ShiftBuffer(void)
{
    size_t by = _readptr - _inbuf;
    memmove(_inbuf, _readptr, by);
    _readptr = _inbuf;
    _writeptr = _inbuf + by;
    _writeSize = _inbufSize - by - 1;
}

void HttpSocket::_DoCallback(char *buf, unsigned int bytes)
{
    if(_cb)
        _cb(this, buf, bytes, _cb_data);

    if(_http_cb)
        _http_cb(this, buf, bytes, _http_cb_data);
}

void HttpSocket::_ProcessChunk(void)
{
    if(!_chunkedTransfer)
        return;

    unsigned int chunksize = -1;

    while(true)
    {
        // less data required until chunk end than received, means the new chunk starts somewhere in the middle
        // of the received data block. finish this chunk first.
        if(_remaining)
        {
            if(_remaining <= _recvSize) // it contains the rest of the chunk, including CRLF
            {
                _DoCallback(_readptr, _remaining - 2); // implicitly skip CRLF
                _readptr += _remaining;
                _recvSize -= _remaining;
                _remaining = 0; // done with this one.
                if(!chunksize) // and if chunksize was 0, we are done with all chunks.
                    break;
            }
            else // buffer did not yet arrive completely
            {
                _DoCallback(_readptr, _recvSize);
                _remaining -= _recvSize;
                _recvSize = 0; // done with the whole buffer, but not with the chunk
                return; // nothing else to do here
            }
        }

        // each chunk identifier ends with CRLF.
        // if we don't find that, we hit the corner case that the chunk identifier was not fully received.
        // in that case, adjust the buffer and wait for the rest of the data to be appended
        char *term = strstr(_readptr, "\r\n");
        if(!term)
        {
            if(_recvSize) // if there is still something queued, move it to the left of the buffer and append on next read
                _ShiftBuffer();
            return;
        }
        term += 2; // skip CRLF
        
        // when we are here, the (next) chunk header was completely received.
        chunksize = strtoul(_readptr, NULL, 16);
        _remaining = chunksize + 2; // the http protocol specifies that each chunk has a trailing CRLF
        _recvSize -= (term - _readptr);
        _readptr = term;
    }

    if(!chunksize) // this was the last chunk, no further data expected unless requested
    {
        _chunkedTransfer = false;
        _DequeueMore();
        if(_recvSize)
            traceprint("_ProcessChunk: There are %u bytes left in the buffer, huh?\n", _recvSize);
        if(_mustClose)
            close();
    }
}

void HttpSocket::_ParseHeader(void)
{
    // if we are here, we expect a header
    // TODO: this can be more optimized
    _tmpHdr += _inbuf;
    const char *hptr = _tmpHdr.c_str();

    if((_recvSize >= 4 || _tmpHdr.size() >= 4) && memcmp("HTTP", hptr, 4))
    {
        traceprint("_ParseHeader: not HTTP stream\n");
        return;
    }

    const char *hdrend = strstr(hptr, "\r\n\r\n");
    if(!hdrend)
    {
        traceprint("_ParseHeader: could not find end-of-header marker, or incomplete buf; delaying.\n");
        return;
    }
    std::transform(_tmpHdr.begin(), _tmpHdr.end(), _tmpHdr.begin(), tolower);

    const char *statuscode = strchr(hptr, ' ');
    if(!statuscode)
        return; // WTF?

    ++statuscode;
    _status = atoi(statuscode);

    _chunkedTransfer = false;
    _contentLen = 0; // yet unknown

    const char *content_length = "content-length: ";
    const char *lenptr = strstr(hptr, content_length);
    if(lenptr)
    {
        lenptr += strlen(content_length);
        _remaining = _contentLen = atoi(lenptr);
    }
    else
    {
        const char *transfer_encoding = "transfer-encoding: ";
        lenptr = strstr(hptr, transfer_encoding);
        if(lenptr)
        {
            lenptr += strlen(transfer_encoding);
            if(STRNICMP(lenptr, "chunked", 7))
            {
                traceprint("_ParseHeader: Unknown content length and transfer encoding!\n");
                return;
            }
            _chunkedTransfer = true;
        }
    }

    const char *connection = "connection: ";
    const char *conptr = strstr(hptr, connection);
    _mustClose = true;
    if(conptr)
    {
        conptr += strlen(connection);
        _mustClose = STRNICMP(conptr, "keep-alive", 10); // if its not keep-alive, server will close it, so we can too
    }

    if(!(_chunkedTransfer || _contentLen))
        traceprint("_ParseHeader: Not chunked transfer and content-length==0, this will go fail");

    // get ready
    _readptr = strstr(_inbuf, "\r\n\r\n") + 4; // skip double newline. must have been found in hptr earlier.
    _recvSize -= (_readptr - _inbuf); // skip the header part
    _tmpHdr.clear();
}

// generic http header parsing
void HttpSocket::_OnRecv(void)
{
    // reset pointers for next read
    _writeSize = _inbufSize - 1;
    _readptr = _writeptr = _inbuf;


    if(!(_chunkedTransfer || (_remaining && _recvSize)))
        _ParseHeader();

    if(_chunkedTransfer)
    {
        _ProcessChunk(); // first, try to finish one or more chunks
    }
    else if(_remaining && _recvSize) // something remaining? if so, we got a header earlier, but not all data
    {
        _remaining -= _recvSize;
        _DoCallback(_readptr, _recvSize);

        if(int(_remaining) < 0)
        {
            traceprint("_OnRecv: _remaining wrap-around, huh??\n");
            _remaining = 0;
        }
        if(!_remaining) // received last block?
        {
            if(_mustClose)
                close();
            else
                _DequeueMore();
        }

        // nothing else to do here.
    }

    // otherwise, the server sent just the header, with the data following in the next packet
}

SocketSet::~SocketSet()
{
    deleteAll();
}

void SocketSet::deleteAll(void)
{
    for(std::set<HttpSocket*>::iterator it = _store.begin(); it != _store.end(); ++it)
        delete *it;
    _store.clear();
}

struct SocketSetEraseHelper
{
    std::set<HttpSocket*>::iterator *it;
    SocketSet *ss;
    bool deleted;
};

static void _SocketSet_del_callback(HttpSocket *s, void *user)
{
    SocketSetEraseHelper *h = (SocketSetEraseHelper*)user;
    h->ss->_remove(*h->it);
    h->deleted = true;
}

bool SocketSet::update(void)
{
    bool interesting = false;
    std::set<HttpSocket*>::iterator it = _store.begin();
    SocketSetEraseHelper h;
    h.it = &it;
    h.ss = this;
    for( ; it != _store.end(); )
    {
        (*it)->SetDelCallback(_SocketSet_del_callback, &h);
        h.deleted = false;
        interesting = (*it)->update() || interesting;
        if(!h.deleted)
            ++it;
    }
    return interesting;
}

void SocketSet::remove(HttpSocket *s)
{
    s->SetDelCallback(NULL, NULL);
    _store.erase(s);
}

void SocketSet::_remove(std::set<HttpSocket*>::iterator& it)
{
    (*it)->SetDelCallback(NULL, NULL);
    _store.erase(it++);
}

void SocketSet::add(HttpSocket *s)
{
    s->SetNonBlocking(true);
    _store.insert(s);
}


} // namespace minihttp
