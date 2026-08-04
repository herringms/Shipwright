#pragma once

#include "HyruleCoopProtocol.h"

#include <SDL2/SDL_net.h>
#include <atomic>
#include <mutex>
#include <deque>
#include <map>
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
    void ConfigureRealtime(SessionScope scope, uint64_t participantId, uint64_t tokenHigh, uint64_t tokenLow);
    void DisableRealtime();
    bool Send(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId = 0);
    bool SendReliable(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId = 0);
    bool SendRepeatedRealtime(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId,
                              uint32_t repeatIntervalMs = 60, uint32_t repeatWindowMs = 600);
    void CancelRepeatedRealtime(MessageType type, uint64_t streamId);
    std::vector<Packet> TakeIncomingPackets();

    SessionRole GetRole() const;
    TransportState GetState() const;
    uint32_t GetConnectionGeneration() const;
    bool IsRealtimeReady() const;
    std::string GetLastError() const;

  private:
    struct OutgoingPacket {
        MessageType type;
        uint64_t streamId;
        std::vector<uint8_t> bytes;
    };

    struct RealtimeCredentials {
        SessionScope scope;
        uint64_t participantId = 0;
        uint64_t tokenHigh = 0;
        uint64_t tokenLow = 0;
    };

    struct RepeatedRealtimePacket {
        OutgoingPacket packet;
        uint64_t nextSendMs = 0;
        uint64_t expiresAtMs = 0;
        uint32_t repeatIntervalMs = 0;
    };

    void RunHost(uint16_t port);
    void RunClient(std::string host, uint16_t port);
    void RunConnectedSocket(TCPsocket socket, UDPsocket realtimeSocket, const IPaddress* defaultRealtimePeer);
    bool FlushOutgoing(TCPsocket socket);
    bool FlushRealtime(UDPsocket socket, const IPaddress& peerAddress);
    bool ReceiveAvailable(TCPsocket socket, SDLNet_SocketSet socketSet, std::vector<uint8_t>& receiveBuffer);
    void ReceiveRealtime(UDPsocket socket, IPaddress& peerAddress, bool& hasPeerAddress);
    void MaintainRealtimeBinding(UDPsocket socket, IPaddress& peerAddress, bool& hasPeerAddress);
    bool SendRealtimeControl(UDPsocket socket, const IPaddress& peerAddress, uint16_t kind);
    bool SendRealtimeData(UDPsocket socket, const IPaddress& peerAddress, const std::vector<uint8_t>& data);
    bool SendAll(TCPsocket socket, const std::vector<uint8_t>& data);
    void ConfigureRealtimeTestImpairmentFromEnvironment();
    void QueueReliable(OutgoingPacket packet);
    void PushIncoming(Packet packet);
    void FallBackRealtimePackets();
    void ClearPacketQueues();
    void FailPeer(const std::string& message);
    void Fail(const std::string& message);

    std::atomic<bool> running = false;
    std::atomic<SessionRole> role = SessionRole::None;
    std::atomic<TransportState> state = TransportState::Stopped;
    std::atomic<uint32_t> nextSequence = 1;
    std::atomic<uint32_t> connectionGeneration = 0;
    std::atomic<bool> disconnectPeerRequested = false;
    std::atomic<bool> realtimeConfigured = false;
    std::atomic<bool> realtimeReady = false;
    std::thread worker;

    mutable std::mutex errorMutex;
    std::string lastError;
    std::mutex incomingMutex;
    std::deque<Packet> incoming;
    std::mutex outgoingMutex;
    std::deque<OutgoingPacket> outgoing;
    std::mutex realtimeMutex;
    RealtimeCredentials realtimeCredentials;
    std::deque<OutgoingPacket> realtimeOutgoing;
    std::map<std::pair<MessageType, uint64_t>, RepeatedRealtimePacket> repeatedRealtimeOutgoing;
    std::map<std::pair<MessageType, uint64_t>, uint32_t> latestRealtimeSequences;
    uint64_t lastRealtimeReceiveMs = 0;
    uint64_t lastRealtimeBindAttemptMs = 0;
    std::mutex realtimeTestMutex;
    uint32_t realtimeTestDropEvery = 0;
    uint32_t realtimeTestDatagramCount = 0;
    uint32_t realtimeTestDelayMs = 0;
    bool realtimeTestReorderPairs = false;
    std::vector<uint8_t> realtimeTestHeldDatagram;
};

} // namespace HyruleCoop
