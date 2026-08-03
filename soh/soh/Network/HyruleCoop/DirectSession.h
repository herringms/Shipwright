#pragma once

#include "HyruleCoopProtocol.h"

#include <SDL2/SDL_net.h>
#include <atomic>
#include <mutex>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace HyruleCoop {

enum class SessionRole {
    None,
    Host,
    Client,
};

enum class TransportState {
    Stopped,
    Starting,
    Listening,
    Connecting,
    Connected,
    Disconnected,
    Error,
};

class DirectSession {
  public:
    DirectSession() = default;
    ~DirectSession();

    DirectSession(const DirectSession&) = delete;
    DirectSession& operator=(const DirectSession&) = delete;

    bool StartHost(uint16_t port);
    bool StartClient(const std::string& host, uint16_t port);
    void Stop();
    void DisconnectPeer();
    bool Send(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId = 0);
    std::vector<Packet> TakeIncomingPackets();

    SessionRole GetRole() const;
    TransportState GetState() const;
    uint32_t GetConnectionGeneration() const;
    std::string GetLastError() const;

  private:
    struct OutgoingPacket {
        MessageType type;
        uint64_t streamId;
        std::vector<uint8_t> bytes;
    };

    void RunHost(uint16_t port);
    void RunClient(std::string host, uint16_t port);
    void RunConnectedSocket(TCPsocket socket);
    bool FlushOutgoing(TCPsocket socket);
    bool ReceiveAvailable(TCPsocket socket, SDLNet_SocketSet socketSet, std::vector<uint8_t>& receiveBuffer);
    bool SendAll(TCPsocket socket, const std::vector<uint8_t>& data);
    void ClearPacketQueues();
    void FailPeer(const std::string& message);
    void Fail(const std::string& message);

    std::atomic<bool> running = false;
    std::atomic<SessionRole> role = SessionRole::None;
    std::atomic<TransportState> state = TransportState::Stopped;
    std::atomic<uint32_t> nextSequence = 1;
    std::atomic<uint32_t> connectionGeneration = 0;
    std::atomic<bool> disconnectPeerRequested = false;
    std::thread worker;

    mutable std::mutex errorMutex;
    std::string lastError;
    std::mutex incomingMutex;
    std::deque<Packet> incoming;
    std::mutex outgoingMutex;
    std::deque<OutgoingPacket> outgoing;
};

} // namespace HyruleCoop
