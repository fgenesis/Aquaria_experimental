#ifndef AQ_NETWORK_H
#define AQ_NETWORK_H

namespace Network
{
    typedef void (*callback)(bool, int, int, void*, int, const std::string&);
    bool download(const std::string& query, const std::string& to, bool inMem, bool toQueue = true,
        callback cb = NULL, void *user1 = NULL, int user2 = 0, const char *user3 = NULL);
    void update();
    void shutdown();
};



#endif
