#pragma once

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include <array>
#include <utility>
#include <vector>

ssize_t writeRestarting(int fd, const void *buf, size_t count);
bool writeAllRestarting(int fd, const void *buf, size_t count);
ssize_t readRestarting(int fd, void *buf, size_t count);
void setSocketNoDelay(int s);

struct TermSize {
    uint16_t cols;
    uint16_t rows;

    bool operator==(const TermSize &o) const {
        return cols == o.cols && rows == o.rows;
    }
    bool operator!=(const TermSize &o) const {
        return !(*this == o);
    }
};

struct WindowParams {
    int32_t size;       // Maximum number of bytes in flight.
    int32_t threshold;  // Minimum remaining window to initiate I/O.
};

struct Packet {
    enum class Type { SetSize, IncreaseWindow, ChildExitStatus } type;
    union {
        TermSize termSize;
        int32_t windowAmount;
        int32_t exitStatus;
    } u;
};

class WakeupFd {
public:
    WakeupFd();
    ~WakeupFd() {
        close(fds_[0]);
        close(fds_[1]);
    }

    void set() {
        char dummy = 0;
        writeRestarting(fds_[1], &dummy, 1);
    }

    void wait();

private:
    int readFd() const { return fds_[0]; }

    fd_set fdset_;
    int fds_[2];
};

template <typename T, void packetHandlerFunc(T*, const Packet&), void readFailure()>
void readControlSocketThread(int controlSocketFd, T *userObj) {
    std::array<Packet, 256> buf;
    char *const bufRaw = reinterpret_cast<char*>(&buf);
    size_t accum = 0;
    while (true) {
        const ssize_t amt = readRestarting(controlSocketFd,
                                           &bufRaw[accum],
                                           sizeof(buf) - accum);
        if (amt <= 0) {
            readFailure();
        }
        accum += amt;
        assert(accum <= sizeof(buf));
        const size_t fullPacketCount = accum / sizeof(Packet);
        for (size_t i = 0; i < fullPacketCount; ++i) {
            packetHandlerFunc(userObj, buf[i]);
        }
        const size_t fullPacketBytes = fullPacketCount * sizeof(Packet);
        accum -= fullPacketBytes;
        memmove(&buf[0], &buf[fullPacketCount], accum);
    }
}
