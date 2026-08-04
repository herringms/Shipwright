#pragma once

#include "HyruleCoopProtocol.h"

#include <SDL2/SDL_net.h>
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
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

struct TransportTelemetry {
    bool realtimeReady = false;
    uint32_t roundTripMs = 0;
    uint32_t roundTripJitterMs = 0;
    uint32_t snapshotIntervalMs = 0;
    uint32_t snapshotJitterMs = 0;
    uint32_t reliableQueueDepth = 0;
    uint32_t reliableQueueHighWater = 0;
    uint32_t incomingQueueDepth = 0;
    uint32_t incomingQueueHighWater = 0;
    uint32_t pendingAcknowledgements = 0;
    uint32_t lastTcpQueueDelayMs = 0;
    uint32_t maximumTcpQueueDelayMs = 0;
    uint32_t lastApplicationDelayMs = 0;
    uint32_t maximumApplicationDelayMs = 0;
    uint64_t lastRealtimeReceiveAgeMs = 0;
    uint64_t realtimeDatagramsSent = 0;
    uint64_t realtimeDatagramsReceived = 0;
    uint64_t realtimeBytesSent = 0;
    uint64_t realtimeBytesReceived = 0;
    uint64_t realtimeDuplicates = 0;
    uint64_t realtimeStaleSnapshots = 0;
    uint64_t acknowledgedEventsSent = 0;
    uint64_t acknowledgedEventRetries = 0;
    uint64_t acknowledgedEventsReceived = 0;
    uint64_t acknowledgedEventFallbacks = 0;
    uint64_t tcpBytesSent = 0;
    uint64_t tcpBytesReceived = 0;
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
    bool SendAcknowledgedRealtime(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId,
                                  uint32_t retryIntervalMs = 70, uint32_t fallbackAfterMs = 1000);
    void CancelRepeatedRealtime(MessageType type, uint64_t streamId);
    std::vector<Packet> TakeIncomingPackets();

    SessionRole GetRole() const;
    TransportState GetState() const;
    uint32_t GetConnectionGeneration() const;
    bool IsRealtimeReady() const;
    TransportTelemetry GetTelemetry() const;
    std::string GetLastError() const;

  private:
    struct OutgoingPacket {
        MessageType type;
        uint64_t streamId;
        std::vector<uint8_t> bytes;
        uint32_t sequence = 0;
        uint64_t queuedAtMs = 0;
        bool acknowledgedRealtime = false;
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

    struct AcknowledgedRealtimePacket {
        OutgoingPacket packet;
        uint64_t firstSentMs = 0;
        uint64_t nextSendMs = 0;
        uint64_t fallbackAtMs = 0;
        uint32_t retryIntervalMs = 0;
        uint32_t transmissionCount = 0;
    };

    void RunHost(uint16_t port);
    void RunClient(std::string host, uint16_t port);
    void RunConnectedSocket(TCPsocket socket, UDPsocket realtimeSocket, const IPaddress* defaultRealtimePeer);
    bool FlushOutgoing(TCPsocket socket);
    bool FlushRealtime(UDPsocket socket, const IPaddress& peerAddress);
    bool ReceiveAvailable(TCPsocket socket, SDLNet_SocketSet socketSet, std::vector<uint8_t>& receiveBuffer);
    void ReceiveRealtime(UDPsocket socket, IPaddress& peerAddress, bool& hasPeerAddress);
    void MaintainRealtimeBinding(UDPsocket socket, IPaddress& peerAddress, bool& hasPeerAddress);
    bool SendRealtimeControl(UDPsocket socket, const IPaddress& peerAddress, uint16_t kind,
                             const std::vector<uint8_t>& payload = {});
    bool SendRealtimeData(UDPsocket socket, const IPaddress& peerAddress, const std::vector<uint8_t>& data);
    bool SendAll(TCPsocket socket, const std::vector<uint8_t>& data);
    void ConfigureRealtimeTestImpairmentFromEnvironment();
    void QueueReliable(OutgoingPacket packet);
    void PushIncoming(Packet packet);
    void FallBackRealtimePackets();
    void ClearPacketQueues();
    void ResetTelemetry();
    void ObserveRoundTrip(uint32_t milliseconds);
    void ObservePlayerSnapshotArrival(uint64_t nowMs);
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
    mutable std::mutex realtimeMutex;
    RealtimeCredentials realtimeCredentials;
    std::deque<OutgoingPacket> realtimeOutgoing;
    std::map<std::pair<MessageType, uint64_t>, RepeatedRealtimePacket> repeatedRealtimeOutgoing;
    std::map<uint32_t, AcknowledgedRealtimePacket> acknowledgedRealtimeOutgoing;
    std::map<std::pair<MessageType, uint64_t>, uint32_t> latestRealtimeSequences;
    std::deque<uint32_t> receivedAcknowledgedRealtimeOrder;
    std::unordered_set<uint32_t> receivedAcknowledgedRealtime;
    uint64_t lastRealtimeReceiveMs = 0;
    uint64_t lastRealtimeBindAttemptMs = 0;
    uint64_t lastRealtimePingMs = 0;
    std::mutex realtimeTestMutex;
    uint32_t realtimeTestDropEvery = 0;
    uint32_t realtimeTestDatagramCount = 0;
    uint32_t realtimeTestDelayMs = 0;
    bool realtimeTestReorderPairs = false;
    std::vector<uint8_t> realtimeTestHeldDatagram;

    std::atomic<uint32_t> telemetryRoundTripMs = 0;
    std::atomic<uint32_t> telemetryRoundTripJitterMs = 0;
    std::atomic<uint32_t> telemetrySnapshotIntervalMs = 0;
    std::atomic<uint32_t> telemetrySnapshotJitterMs = 0;
    std::atomic<uint64_t> telemetryLastSnapshotArrivalMs = 0;
    std::atomic<uint32_t> telemetryReliableQueueDepth = 0;
    std::atomic<uint32_t> telemetryReliableQueueHighWater = 0;
    std::atomic<uint32_t> telemetryIncomingQueueDepth = 0;
    std::atomic<uint32_t> telemetryIncomingQueueHighWater = 0;
    std::atomic<uint32_t> telemetryPendingAcknowledgements = 0;
    std::atomic<uint32_t> telemetryLastTcpQueueDelayMs = 0;
    std::atomic<uint32_t> telemetryMaximumTcpQueueDelayMs = 0;
    std::atomic<uint32_t> telemetryLastApplicationDelayMs = 0;
    std::atomic<uint32_t> telemetryMaximumApplicationDelayMs = 0;
    std::atomic<uint64_t> telemetryRealtimeDatagramsSent = 0;
    std::atomic<uint64_t> telemetryRealtimeDatagramsReceived = 0;
    std::atomic<uint64_t> telemetryRealtimeBytesSent = 0;
    std::atomic<uint64_t> telemetryRealtimeBytesReceived = 0;
    std::atomic<uint64_t> telemetryRealtimeDuplicates = 0;
    std::atomic<uint64_t> telemetryRealtimeStaleSnapshots = 0;
    std::atomic<uint64_t> telemetryAcknowledgedEventsSent = 0;
    std::atomic<uint64_t> telemetryAcknowledgedEventRetries = 0;
    std::atomic<uint64_t> telemetryAcknowledgedEventsReceived = 0;
    std::atomic<uint64_t> telemetryAcknowledgedEventFallbacks = 0;
    std::atomic<uint64_t> telemetryTcpBytesSent = 0;
    std::atomic<uint64_t> telemetryTcpBytesReceived = 0;
};

} // namespace HyruleCoop
