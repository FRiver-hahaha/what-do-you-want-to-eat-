#include "server/tcp/ConnContext.h"

namespace eat::tcp {

const char* ConnStateName(ConnState state) {
    switch (state) {
        case ConnState::Connected: return "connected";
        case ConnState::Handshaking: return "handshaking";
        case ConnState::Authenticated: return "authenticated";
        case ConnState::Replaced: return "replaced";
        case ConnState::Draining: return "draining";
        case ConnState::Closing: return "closing";
        case ConnState::Closed: return "closed";
    }
    return "closed";
}

void ConnContext::Touch(std::chrono::steady_clock::time_point now) {
    last_recv_mono = now;
}

bool ConnContext::IsAuthenticated() const {
    return state == ConnState::Authenticated || state == ConnState::Draining;
}

bool ConnContext::Close(protocol::ErrorCode) {
    if (state == ConnState::Closed) {
        return false;
    }
    state = ConnState::Closed;
    read_buf_used = 0;
    send_queued_bytes = 0;
    inflight_business = 0;
    return true;
}

}  // namespace eat::tcp