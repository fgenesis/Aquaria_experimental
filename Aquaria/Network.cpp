#include "minihttp.h"
#include "DSQ.h"
#include "Network.h"
#include "lvpa/ByteBuffer.h"
#include "VFSTools.h"
#include <map>
#include <set>
#include <cassert>
#include "SDL.h"

using namespace minihttp;

namespace Network {

struct FileDownloadInfos;

static bool netUp = false;
static minihttp::SocketSet sockets;
static std::map<std::string, HttpSocket*> hostmap;
static SDL_mutex *mtx = NULL;
static SDL_Thread *worker = NULL;
static volatile bool canUpdate = true; // for crude thread sync

template <typename T> class BlockingQueue
{
public:
    BlockingQueue()
    {
        _mtx = SDL_CreateMutex();
        _cond = SDL_CreateCond();
    }
    ~BlockingQueue()
    {
        SDL_DestroyMutex(_mtx);
        SDL_DestroyCond(_cond);
    }
    void push(const T& e)
    {
        SDL_LockMutex(_mtx);
        _q.push(e);
        SDL_UnlockMutex(_mtx);
        SDL_CondBroadcast(_cond);
    }
    T pop()
    {
        SDL_LockMutex(_mtx);
        while(_q.empty())
            SDL_CondWait(_cond, _mtx);
        T& ret = _q.front();
        _q.pop();
        SDL_UnlockMutex(_mtx);
        return ret;
    }
private:
    SDL_mutex *_mtx;
    SDL_cond *_cond;
    std::queue<T> _q;
};

struct SocketInitialData
{
    std::string file, host;
    minihttp::HttpSocket *sock;
    FileDownloadInfos *info;
};

BlockingQueue<SocketInitialData*> NQ;


struct FileDownloadInfos
{
    FileDownloadInfos() : inMem(false), cb(NULL), user1(NULL), user2(0) {}
    std::string name;
    lvpa::ByteBuffer buf;
    bool inMem;
    callback cb;
    void *user1;
    int user2;
    std::string user3;
};

static int _NetworkPushThread(void *); // pre-decl

bool init()
{
    if(netUp)
        return true;

    debugLog("NETWORK: Init");

    if(!(netUp = InitNetwork()))
        return false;

    if(!mtx)
        mtx = SDL_CreateMutex();

    if(!worker)
        worker = SDL_CreateThread(_NetworkPushThread, NULL);

    return true;
}

void update()
{
    if(netUp)
    {
        if(canUpdate) // can not update if a socket creation is in progress right now
        {
            SDL_LockMutex(mtx);
            if(canUpdate)
                while(sockets.update()); // as long as something interesting happens, keep updating
            SDL_UnlockMutex(mtx);
        }
    }
}

void shutdown()
{
    if(netUp)
    {
        SDL_LockMutex(mtx);

        debugLog("NETWORK: Shutdown");
        netUp = false;
        NQ.push(NULL); // wake up thread a last time
        sockets.deleteAll();
        hostmap.clear();

        SDL_UnlockMutex(mtx);

        SDL_WaitThread(worker, NULL);
        worker = NULL;

        SDL_DestroyMutex(mtx);
        mtx = NULL;
    }
}

static bool file_mount_buffer(FileDownloadInfos *info)
{
    debugLog("NETWORK: Save HTTP file to disk and mount: " + info->name);

    std::string vdir = "./";
    size_t pos = info->name.rfind('/');
    if(pos != std::string::npos)
        vdir = info->name.substr(0, pos);

    ttvfs::VFSDir *vd = dsq->vfs.GetDir(vdir.c_str(), false);
    if(!vd)
    {
        debugLog("NETWORK: Failed to find real disk dir");
        return false;
    }

    std::string real = vd->fullname();
    debugLog("VFS at " + vdir + " points to " + real);

    real += '/';
    real += ttvfs::PathToFileName(info->name.c_str());

    int wrote = -1;
    ttvfs::VFSFileReal *vf = new ttvfs::VFSFileReal(real.c_str());
    if(vf->open(NULL, "wb"))
    {
        vd->add(vf); // drop out an older one before writing, in case the file is downloaded twice
        wrote = vf->write(info->buf.contents(), info->buf.wpos()); // ... it gets closed when dropped, so we can write
        vf->flush();
        vf->close();
    }
    vf->ref--;

    std::ostringstream os;
    os << "NETWORK: Wrote " << wrote << " bytes to " << real;
    debugLog(os.str());

    info->buf.clear();
    info->buf._setPtr(NULL); // indicate the memory was given away and everything successful
    return true;
}

static bool mem_mount_buffer(FileDownloadInfos *info)
{
    debugLog("NETWORK: Memory mount HTTP file: " + info->name);

    ttvfs::VFSFileMem *vf = new ttvfs::VFSFileMem(
        info->name.c_str(),
        info->buf.contents(),
        info->buf.wpos(), // using wpos() and note size() here is intentional because of the extra terminating '\0' byte
        ttvfs::VFSFileMem::TAKE_OVER,
        free
        );

    info->buf._setPtr(NULL); // indicate the memory was given away and everything successful

    const char *str = info->name.c_str();
    const char *slashpos = (char *)strrchr(str, '/');
    if(slashpos)
    {
        std::string path(str, slashpos - str);
        ttvfs::VFSDir *vd = dsq->vfs.GetDir(path.c_str(), true);
        vd->add(vf); // just add into already merged tree
    }
    else
    {
        dsq->vfs.GetDirRoot()->add(vf);
    }
    vf->ref--;
    return true;
}

static void wget_callback(HttpSocket *sock, char *data, unsigned int size, void *user)
{
    FileDownloadInfos *info = (FileDownloadInfos*)user;
    if(!info)
        return; // EH?

    if(sock->GetStatusCode() != HTTP_OK)
    {
        if(!(data || size)) // just print the last bit
        {
            std::ostringstream os;
            os << "NETWORK: HTTP ERROR " << sock->GetStatusCode();
            debugLog(os.str());

            if(info->cb)
                info->cb(true, 0, 0, info->user1, info->user2, info->user3);
        }
        return;
    }

    if(!data) // connection closed?
    {
        if(info->buf.contents()) // this should be NULL if the download was complete & processed
        {
            std::string t = "NETWORK: Download FAILED: ";
            t += info->name;
            debugLog(t);

            if(info->cb)
                info->cb(true, 0, 0, info->user1, info->user2, info->user3);
        }
        std::string t = "HTTP connection to host closed: ";
        t += sock->GetHost();
        debugLog(t);
        return;
    }

    if(size) // is it a packet that has data?
    {
        /*std::ostringstream os;
        os << "HTTP Recv " << size << " bytes, total " << info->buf.size();
        debugLog(os.str());*/

        info->buf.append(data, size);

        if(info->cb)
            info->cb(false, info->buf.wpos(), sock->GetContentLen(), info->user1, info->user2, info->user3);
    }
    else // packet is 0 bytes
    {
        std::ostringstream os;
        os << "NETWORK: Download complete: " << info->name << " (" << info->buf.size() << " bytes)";
        debugLog(os.str());

        info->buf << (char)0; // terminate strings, for text files
        info->buf.wpos(info->buf.wpos() - 1); 

        int wpos = info->buf.wpos();

        bool ok;
        if(info->inMem)
            ok = mem_mount_buffer(info);
        else
            ok = file_mount_buffer(info);

        if(info->cb)
            info->cb(true, ok ? wpos : 0, ok ? wpos : 0, info->user1, info->user2, info->user3);
    }
}

template <typename T> static void delete_wrapper(void *ptr)
{
    delete (T*)ptr;
}

// must only be run by _NetworkPushThread
static void th_DoSocketOpenAndSend(const SocketInitialData *si)
{
    if(!si->sock->isOpen() && !si->sock->open(si->host.c_str(), 80))  // FIXME: SplitURI should also be able to detect port!!
    {
        std::ostringstream os;
        os << "NETWORK: Failed to open socket to host: " << si->host; //<< "(port " << port << ")";
        debugLog(os.str());
        delete si->info;
        return;
    }

    // worst case:
    // As HttpSocket::SendGet() may block even after it is connected
    // (a user with a very stupid firewall waiting for confirmation for first packet send)
    // we must do the sending in this thread, while preventing main from updating the sockets.
    // Care has to be taken as SendGet() may as well enqueue get requests internally in case
    // a socket is already receiving data. We can't keep the send operation locked, but it has to be thread safe somehow.
    // This method is everything else but nice, but does the job.
    SDL_LockMutex(mtx);
    canUpdate = false;
    SDL_UnlockMutex(mtx);

    si->sock->SendGet(si->file, wget_callback, si->info, delete_wrapper<FileDownloadInfos>);
    
    SDL_LockMutex(mtx);
    canUpdate = true;
    SDL_UnlockMutex(mtx);
}

static int _NetworkPushThread(void *)
{
    SocketInitialData *si;
    while(netUp)
    {
        si = NQ.pop();
        if(si)
        {
            th_DoSocketOpenAndSend(si);
            delete si;
        }
    }
    debugLog("Network worker thread exiting");
    return 0;
}


bool download(const std::string& query, const std::string& to, bool inMem, bool toQueue /* = true */,
              callback cb /* = NULL */, void *user1 /* = NULL */, int user2 /* = 0 */, const char *user3 /* = NULL */)
{
    if(!netUp)
    {
        init();
    }

    std::string host, file;
    if(!minihttp::SplitURI(query, host, file))
    {
        std::ostringstream os;
        os << "NETWORK: Bad download URL: " << query;
        debugLog(os.str());
        return false;
    }

    debugLog("NETWORK: Downloading: " + query);

    // recycle old socket or create new
    minihttp::HttpSocket *ht;
    std::map<std::string, HttpSocket*>::iterator it = hostmap.find(host);
    if(toQueue && it != hostmap.end())
        ht = it->second;
    else
    {
        ht = new minihttp::HttpSocket;
        ht->SetUserAgent(dsq->versionLabel->getText());
        if(toQueue)
        {
            hostmap[host] = ht;
            ht->SetKeepAlive(10);
        }
        else
            ht->SetDelSelf(true);

        sockets.add(ht);
    }

    FileDownloadInfos *info = new FileDownloadInfos;
    info->name = to;
    info->inMem = inMem;
    info->cb = cb;
    info->user1 = user1;
    info->user2 = user2;
    if(user3)
        info->user3 = user3;

    SocketInitialData *ind = new SocketInitialData;
    ind->file = file;
    ind->host = host;
    ind->info = info;
    ind->sock = ht;

    NQ.push(ind); // let worker thread handle connecting and sending the initial packet

    return true;
}


} // namespace Network
