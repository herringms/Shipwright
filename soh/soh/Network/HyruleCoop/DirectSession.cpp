#include "DirectSession.h"

#include <SDL2/SDL.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <spdlog/spdlog.h>

namespace HyruleCoop {
namespace {

bool IsReplaceableSnapshot(MessageType type) {
    return type == MessageType::ClockSnapshot || type == MessageType::PlayerSnapshot ||
           type == MessageType::SceneFlagsSnapshot || type == MessageType::ActorSnapshot ||
           type == MessageType::CycleSnapshot || type == MessageType::ProgressionSnapshot;
}

bool IsRealtimeSnapshot(MessageType type) {
    return type == MessageType::ClockSnapshot || type == MessageType::PlayerSnapshot ||
           type == MessageType::ActorSnapshot;
}

bool IsRealtimeDatagram(MessageType type) {
    return IsRealtimeSnapshot(type) || type == MessageType::AttackIntent;
}

constexpr uint32_t kRealtimeMagic = 0x48435544; // HCUD
constexpr uint16_t kRealtimeVersion = 1;
constexpr uint16_t kRealtimeBind = 1;
constexpr uint16_t kRealtimeBindAck = 2;
constexpr uint16_t kRealtimeData = 3;
constexpr uint16_t kRealtimeAcknowledgedData = 4;
constexpr uint16_t kRealtimeAcknowledgement = 5;
constexpr uint16_t kRealtimePing = 6;
constexpr uint16_t kRealtimePong = 7;
constexpr size_t kRealtimeHeaderSize = 44;
constexpr size_t kMaximumRealtimeDatagramSize = 1200;
constexpr uint64_t kRealtimeBindIntervalMs = 500;
constexpr uint64_t kRealtimeTimeoutMs = 5000;
constexpr uint64_t kRealtimePingIntervalMs = 1000;
constexpr size_t kAcknowledgedReceiveWindow = 512;

void AppendU16(std::vector<uint8_t>& data, uint16_t value) {
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendU32(std::vector<uint8_t>& data, uint32_t value) {
    data.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>(value & 0xFF));
}

void AppendU64(std::vector<uint8_t>& data, uint64_t value) {
    AppendU32(data, static_cast<uint32_t>(value >> 32));
    AppendU32(data, static_cast<uint32_t>(value));
}

uint16_t ReadU16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

uint32_t ReadU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
           (static_cast<uint32_t>(data[2]) << 8) | static_cast<uint32_t>(data[3]);
}

uint64_t ReadU64(const uint8_t* data) {
    return (static_cast<uint64_t>(ReadU32(data)) << 32) | ReadU32(data + 4);
}

std::vector<uint8_t> EncodeRealtimeDatagram(uint16_t kind, SessionScope scope, uint64_t participantId,
                                            uint64_t tokenHigh, uint64_t tokenLow,
                                            const std::vector<uint8_t>& payload = {}) {
    std::vector<uint8_t> data;
    data.reserve(kRealtimeHeaderSize + payload.size());
    AppendU32(data, kRealtimeMagic);
    AppendU16(data, kRealtimeVersion);
    AppendU16(data, kind);
    AppendU64(data, scope.sessionEpoch);
    AppendU32(data, scope.worldGeneration);
    AppendU64(data, participantId);
    AppendU64(data, tokenHigh);
    AppendU64(data, tokenLow);
    data.insert(data.end(), payload.begin(), payload.end());
    return data;
}

bool SameCredentials(const uint8_t* data, size_t size, SessionScope scope, uint64_t participantId,
                     uint64_t tokenHigh, uint64_t tokenLow, uint16_t& kind) {
    if (size < kRealtimeHeaderSize || ReadU32(data) != kRealtimeMagic || ReadU16(data + 4) != kRealtimeVersion) {
        return false;
    }
    kind = ReadU16(data + 6);
    return ReadU64(data + 8) == scope.sessionEpoch && ReadU32(data + 16) == scope.worldGeneration &&
           ReadU64(data + 20) == participantId && ReadU64(data + 28) == tokenHigh &&
           ReadU64(data + 36) == tokenLow;
}

uint64_t FreshnessStream(MessageType type, uint64_t streamId) {
    return type == MessageType::PlayerSnapshot || type == MessageType::ClockSnapshot ? 0 : streamId;
}

bool IsNewerSequence(uint32_t sequence, uint32_t previous) {
    return static_cast<int32_t>(sequence - previous) > 0;
}

uint32_t ReadTestEnvironmentU32(const char* name, uint32_t maximum) {
    const char* text = SDL_getenv(name);
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        return 0;
    }
    return static_cast<uint32_t>(std::min<unsigned long>(value, maximum));
}

void AtomicMaximum(std::atomic<uint32_t>& target, uint32_t value) {
    uint32_t previous = target.load();
    while (previous < value && !target.compare_exchange_weak(previous, value)) {
    }
}

uint32_t Smooth(uint32_t previous, uint32_t sample, uint32_t previousWeight = 7) {
    return previous == 0 ? sample : (previous * previousWeight + sample) / (previousWeight + 1);
}

} // namespace

DirectSession::~DirectSession() {
    Stop();
}

bool DirectSession::StartHost(uint16_t port) {
    if (port == 0) {
        return false;
    }
    Stop();
    ResetTelemetry();
    role = SessionRole::Host;
    state = TransportState::Starting;
    running = true;
    worker = std::thread(&DirectSession::RunHost, this, port);
    return true;
}

bool DirectSession::StartClient(const std::string& host, uint16_t port) {
    if (host.empty() || port == 0) {
        return false;
    }
    Stop();
    ResetTelemetry();
    role = SessionRole::Client;
    state = TransportState::Starting;
    running = true;
    worker = std::thread(&DirectSession::RunClient, this, host, port);
    return true;
}

void DirectSession::Stop() {
    running = false;
    if (worker.joinable()) {
        worker.join();
    }

    {
        std::lock_guard<std::mutex> lock(incomingMutex);
        incoming.clear();
    }
    {
        std::lock_guard<std::mutex> lock(outgoingMutex);
        outgoing.clear();
    }
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        realtimeCredentials = {};
        realtimeOutgoing.clear();
        repeatedRealtimeOutgoing.clear();
        acknowledgedRealtimeOutgoing.clear();
        latestRealtimeSequences.clear();
        receivedAcknowledgedRealtime.clear();
        receivedAcknowledgedRealtimeOrder.clear();
        lastRealtimeReceiveMs = 0;
        lastRealtimeBindAttemptMs = 0;
        lastRealtimePingMs = 0;
    }
    {
        std::lock_guard<std::mutex> lock(realtimeTestMutex);
        realtimeTestDatagramCount = 0;
        realtimeTestHeldDatagram.clear();
    }

    role = SessionRole::None;
    state = TransportState::Stopped;
    nextSequence = 1;
    connectionGeneration = 0;
    disconnectPeerRequested = false;
    realtimeConfigured = false;
    realtimeReady = false;
}

void DirectSession::DisconnectPeer() {
    if (role == SessionRole::Host && state == TransportState::Connected) {
        disconnectPeerRequested = true;
    }
}

void DirectSession::ConfigureRealtime(SessionScope scope, uint64_t participantId, uint64_t tokenHigh,
                                      uint64_t tokenLow) {
    realtimeConfigured = false;
    realtimeReady = false;
    FallBackRealtimePackets();
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        realtimeCredentials = { scope, participantId, tokenHigh, tokenLow };
        latestRealtimeSequences.clear();
        acknowledgedRealtimeOutgoing.clear();
        receivedAcknowledgedRealtime.clear();
        receivedAcknowledgedRealtimeOrder.clear();
        lastRealtimeReceiveMs = 0;
        lastRealtimeBindAttemptMs = 0;
        lastRealtimePingMs = 0;
    }
    realtimeConfigured = scope.sessionEpoch != 0 && participantId != 0 && (tokenHigh != 0 || tokenLow != 0);
}

void DirectSession::DisableRealtime() {
    realtimeConfigured = false;
    realtimeReady = false;
    FallBackRealtimePackets();
    std::lock_guard<std::mutex> lock(realtimeMutex);
    realtimeCredentials = {};
    latestRealtimeSequences.clear();
    acknowledgedRealtimeOutgoing.clear();
    receivedAcknowledgedRealtime.clear();
    receivedAcknowledgedRealtimeOrder.clear();
    lastRealtimeReceiveMs = 0;
    lastRealtimeBindAttemptMs = 0;
    lastRealtimePingMs = 0;
}

bool DirectSession::Send(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId) {
    if (!running || state != TransportState::Connected || payload.size() > kMaximumPayloadSize) {
        return false;
    }
    Packet packet{ type, nextSequence.fetch_add(1), payload, streamId };
    std::vector<uint8_t> bytes = EncodePacket(packet);
    if (IsRealtimeSnapshot(type) && realtimeReady && bytes.size() + kRealtimeHeaderSize <= kMaximumRealtimeDatagramSize) {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        if (realtimeReady) {
            const uint64_t sequenceStream = streamId;
            for (auto iterator = realtimeOutgoing.begin(); iterator != realtimeOutgoing.end(); ++iterator) {
                if (iterator->type == type && iterator->streamId == sequenceStream) {
                    realtimeOutgoing.erase(iterator);
                    break;
                }
            }
            realtimeOutgoing.push_back({ type, streamId, std::move(bytes), packet.sequence, SDL_GetTicks64() });
            return true;
        }
    }

    QueueReliable({ type, streamId, std::move(bytes), packet.sequence, SDL_GetTicks64() });
    return true;
}

bool DirectSession::SendReliable(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId) {
    if (!running || state != TransportState::Connected || payload.size() > kMaximumPayloadSize) {
        return false;
    }
    Packet packet{ type, nextSequence.fetch_add(1), payload, streamId };
    QueueReliable({ type, streamId, EncodePacket(packet), packet.sequence, SDL_GetTicks64() });
    return true;
}

bool DirectSession::SendRepeatedRealtime(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId,
                                         uint32_t repeatIntervalMs, uint32_t repeatWindowMs) {
    if (!running || state != TransportState::Connected || payload.size() > kMaximumPayloadSize ||
        !IsRealtimeDatagram(type) || repeatIntervalMs == 0 || repeatWindowMs < repeatIntervalMs) {
        return false;
    }
    Packet packet{ type, nextSequence.fetch_add(1), payload, streamId };
    OutgoingPacket outgoingPacket{ type, streamId, EncodePacket(packet), packet.sequence, SDL_GetTicks64() };
    if (realtimeReady && outgoingPacket.bytes.size() + kRealtimeHeaderSize <= kMaximumRealtimeDatagramSize) {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        if (realtimeReady) {
            const uint64_t now = SDL_GetTicks64();
            repeatedRealtimeOutgoing[std::make_pair(type, streamId)] = {
                std::move(outgoingPacket), now, now + repeatWindowMs, repeatIntervalMs
            };
            return true;
        }
    }

    QueueReliable(std::move(outgoingPacket));
    return true;
}

bool DirectSession::SendAcknowledgedRealtime(MessageType type, const std::vector<uint8_t>& payload,
                                              uint64_t streamId, uint32_t retryIntervalMs,
                                              uint32_t fallbackAfterMs) {
    if (!running || state != TransportState::Connected || payload.size() > kMaximumPayloadSize ||
        !IsRealtimeDatagram(type) || retryIntervalMs == 0 || fallbackAfterMs < retryIntervalMs) {
        return false;
    }

    Packet packet{ type, nextSequence.fetch_add(1), payload, streamId };
    OutgoingPacket outgoingPacket{ type, streamId, EncodePacket(packet), packet.sequence, SDL_GetTicks64(), true };
    if (realtimeReady && outgoingPacket.bytes.size() + kRealtimeHeaderSize <= kMaximumRealtimeDatagramSize) {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        if (realtimeReady) {
            const uint64_t now = SDL_GetTicks64();
            acknowledgedRealtimeOutgoing[packet.sequence] = {
                std::move(outgoingPacket), 0, now, now + fallbackAfterMs, retryIntervalMs, 0
            };
            telemetryPendingAcknowledgements =
                static_cast<uint32_t>(acknowledgedRealtimeOutgoing.size());
            telemetryAcknowledgedEventsSent.fetch_add(1);
            return true;
        }
    }

    outgoingPacket.acknowledgedRealtime = false;
    QueueReliable(std::move(outgoingPacket));
    return true;
}

void DirectSession::CancelRepeatedRealtime(MessageType type, uint64_t streamId) {
    std::lock_guard<std::mutex> lock(realtimeMutex);
    repeatedRealtimeOutgoing.erase(std::make_pair(type, streamId));
}

void DirectSession::QueueReliable(OutgoingPacket packet) {
    std::lock_guard<std::mutex> lock(outgoingMutex);
    if (packet.queuedAtMs == 0) {
        packet.queuedAtMs = SDL_GetTicks64();
    }
    const MessageType type = packet.type;
    const uint64_t streamId = packet.streamId;
    if (IsReplaceableSnapshot(type)) {
        for (auto iterator = outgoing.begin(); iterator != outgoing.end(); ++iterator) {
            if (iterator->type == type && iterator->streamId == streamId) {
                // This packet has a newer sequence number, so preserve wire ordering by moving it to the tail.
                outgoing.erase(iterator);
                break;
            }
        }
    }
    outgoing.push_back(std::move(packet));
    telemetryReliableQueueDepth = static_cast<uint32_t>(outgoing.size());
    AtomicMaximum(telemetryReliableQueueHighWater, static_cast<uint32_t>(outgoing.size()));
}

std::vector<Packet> DirectSession::TakeIncomingPackets() {
    std::vector<Packet> packets;
    std::lock_guard<std::mutex> lock(incomingMutex);
    packets.reserve(incoming.size());
    while (!incoming.empty()) {
        if (incoming.front().receivedAtMs != 0) {
            const uint32_t delay = static_cast<uint32_t>(
                std::min<uint64_t>(SDL_GetTicks64() - incoming.front().receivedAtMs, UINT32_MAX));
            telemetryLastApplicationDelayMs = delay;
            AtomicMaximum(telemetryMaximumApplicationDelayMs, delay);
        }
        packets.push_back(std::move(incoming.front()));
        incoming.pop_front();
    }
    telemetryIncomingQueueDepth = 0;
    return packets;
}

SessionRole DirectSession::GetRole() const {
    return role;
}

TransportState DirectSession::GetState() const {
    return state;
}

uint32_t DirectSession::GetConnectionGeneration() const {
    return connectionGeneration;
}

bool DirectSession::IsRealtimeReady() const {
    return realtimeReady;
}

TransportTelemetry DirectSession::GetTelemetry() const {
    TransportTelemetry result;
    result.realtimeReady = realtimeReady;
    result.roundTripMs = telemetryRoundTripMs;
    result.roundTripJitterMs = telemetryRoundTripJitterMs;
    result.snapshotIntervalMs = telemetrySnapshotIntervalMs;
    result.snapshotJitterMs = telemetrySnapshotJitterMs;
    result.reliableQueueDepth = telemetryReliableQueueDepth;
    result.reliableQueueHighWater = telemetryReliableQueueHighWater;
    result.incomingQueueDepth = telemetryIncomingQueueDepth;
    result.incomingQueueHighWater = telemetryIncomingQueueHighWater;
    result.pendingAcknowledgements = telemetryPendingAcknowledgements;
    result.lastTcpQueueDelayMs = telemetryLastTcpQueueDelayMs;
    result.maximumTcpQueueDelayMs = telemetryMaximumTcpQueueDelayMs;
    result.lastApplicationDelayMs = telemetryLastApplicationDelayMs;
    result.maximumApplicationDelayMs = telemetryMaximumApplicationDelayMs;
    result.realtimeDatagramsSent = telemetryRealtimeDatagramsSent;
    result.realtimeDatagramsReceived = telemetryRealtimeDatagramsReceived;
    result.realtimeBytesSent = telemetryRealtimeBytesSent;
    result.realtimeBytesReceived = telemetryRealtimeBytesReceived;
    result.realtimeDuplicates = telemetryRealtimeDuplicates;
    result.realtimeStaleSnapshots = telemetryRealtimeStaleSnapshots;
    result.acknowledgedEventsSent = telemetryAcknowledgedEventsSent;
    result.acknowledgedEventRetries = telemetryAcknowledgedEventRetries;
    result.acknowledgedEventsReceived = telemetryAcknowledgedEventsReceived;
    result.acknowledgedEventFallbacks = telemetryAcknowledgedEventFallbacks;
    result.tcpBytesSent = telemetryTcpBytesSent;
    result.tcpBytesReceived = telemetryTcpBytesReceived;
    std::lock_guard<std::mutex> lock(realtimeMutex);
    if (lastRealtimeReceiveMs != 0) {
        result.lastRealtimeReceiveAgeMs = SDL_GetTicks64() - lastRealtimeReceiveMs;
    }
    return result;
}

std::string DirectSession::GetLastError() const {
    std::lock_guard<std::mutex> lock(errorMutex);
    return lastError;
}

void DirectSession::RunHost(uint16_t port) {
    IPaddress address;
    if (SDLNet_ResolveHost(&address, nullptr, port) < 0) {
        Fail(SDLNet_GetError());
        return;
    }

    TCPsocket listener = SDLNet_TCP_Open(&address);
    if (listener == nullptr) {
        Fail(SDLNet_GetError());
        return;
    }

    UDPsocket realtimeSocket = SDLNet_UDP_Open(port);
    if (realtimeSocket == nullptr) {
        SPDLOG_WARN("[HyruleCoop] UDP port {} is unavailable; realtime snapshots will use TCP: {}", port,
                    SDLNet_GetError());
    }

    SPDLOG_INFO("[HyruleCoop] Listening on TCP{} port {}", realtimeSocket == nullptr ? "" : "+UDP", port);
    while (running) {
        state = TransportState::Listening;
        TCPsocket peer = nullptr;
        while (running && peer == nullptr) {
            peer = SDLNet_TCP_Accept(listener);
            if (peer == nullptr) {
                SDL_Delay(10);
            }
        }
        if (peer == nullptr) {
            break;
        }
        RunConnectedSocket(peer, realtimeSocket, nullptr);
        SDLNet_TCP_Close(peer);
        ClearPacketQueues();
        if (state == TransportState::Error) {
            break;
        }
    }
    if (realtimeSocket != nullptr) {
        SDLNet_UDP_Close(realtimeSocket);
    }
    SDLNet_TCP_Close(listener);
}

void DirectSession::RunClient(std::string host, uint16_t port) {
    state = TransportState::Connecting;
    IPaddress address;
    if (SDLNet_ResolveHost(&address, host.c_str(), port) < 0) {
        Fail(SDLNet_GetError());
        return;
    }

    TCPsocket peer = SDLNet_TCP_Open(&address);
    if (peer == nullptr) {
        Fail(SDLNet_GetError());
        return;
    }

    UDPsocket realtimeSocket = SDLNet_UDP_Open(0);
    if (realtimeSocket == nullptr) {
        SPDLOG_WARN("[HyruleCoop] UDP socket is unavailable; realtime snapshots will use TCP: {}", SDLNet_GetError());
    }

    RunConnectedSocket(peer, realtimeSocket, &address);
    if (realtimeSocket != nullptr) {
        SDLNet_UDP_Close(realtimeSocket);
    }
    SDLNet_TCP_Close(peer);
}

void DirectSession::RunConnectedSocket(TCPsocket socket, UDPsocket realtimeSocket,
                                       const IPaddress* defaultRealtimePeer) {
    SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(1);
    if (socketSet == nullptr || SDLNet_TCP_AddSocket(socketSet, socket) < 0) {
        if (socketSet != nullptr) {
            SDLNet_FreeSocketSet(socketSet);
        }
        Fail(SDLNet_GetError());
        return;
    }

    disconnectPeerRequested = false;
    DisableRealtime();
    ConfigureRealtimeTestImpairmentFromEnvironment();
    connectionGeneration.fetch_add(1);
    state = TransportState::Connected;
    std::vector<uint8_t> receiveBuffer;
    IPaddress realtimePeer{};
    bool hasRealtimePeer = defaultRealtimePeer != nullptr;
    if (defaultRealtimePeer != nullptr) {
        realtimePeer = *defaultRealtimePeer;
    }
    while (running) {
        if (realtimeSocket != nullptr) {
            MaintainRealtimeBinding(realtimeSocket, realtimePeer, hasRealtimePeer);
            ReceiveRealtime(realtimeSocket, realtimePeer, hasRealtimePeer);
            if (realtimeReady && hasRealtimePeer && !FlushRealtime(realtimeSocket, realtimePeer)) {
                realtimeReady = false;
                FallBackRealtimePackets();
            }
        }
        if (!FlushOutgoing(socket)) {
            break;
        }
        if (disconnectPeerRequested.exchange(false)) {
            break;
        }
        if (!ReceiveAvailable(socket, socketSet, receiveBuffer)) {
            break;
        }
        SDL_Delay(1);
    }

    DisableRealtime();
    SDLNet_FreeSocketSet(socketSet);
    if (running && state != TransportState::Error) {
        state = TransportState::Disconnected;
    }
}

bool DirectSession::FlushOutgoing(TCPsocket socket) {
    std::deque<OutgoingPacket> packets;
    {
        std::lock_guard<std::mutex> lock(outgoingMutex);
        packets.swap(outgoing);
        telemetryReliableQueueDepth = 0;
    }
    while (!packets.empty()) {
        const uint32_t queueDelay = static_cast<uint32_t>(
            std::min<uint64_t>(SDL_GetTicks64() - packets.front().queuedAtMs, UINT32_MAX));
        telemetryLastTcpQueueDelayMs = queueDelay;
        AtomicMaximum(telemetryMaximumTcpQueueDelayMs, queueDelay);
        if (!SendAll(socket, packets.front().bytes)) {
            FailPeer(SDLNet_GetError());
            return false;
        }
        packets.pop_front();
    }
    return true;
}

bool DirectSession::FlushRealtime(UDPsocket socket, const IPaddress& peerAddress) {
    std::deque<OutgoingPacket> packets;
    std::deque<OutgoingPacket> reliableFallbacks;
    RealtimeCredentials credentials;
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        packets.swap(realtimeOutgoing);
        const uint64_t now = SDL_GetTicks64();
        for (auto iterator = repeatedRealtimeOutgoing.begin(); iterator != repeatedRealtimeOutgoing.end();) {
            RepeatedRealtimePacket& repeated = iterator->second;
            if (now >= repeated.expiresAtMs) {
                reliableFallbacks.push_back(std::move(repeated.packet));
                iterator = repeatedRealtimeOutgoing.erase(iterator);
                continue;
            }
            if (now >= repeated.nextSendMs) {
                packets.push_back(repeated.packet);
                repeated.nextSendMs = now + repeated.repeatIntervalMs;
            }
            ++iterator;
        }
        for (auto iterator = acknowledgedRealtimeOutgoing.begin();
             iterator != acknowledgedRealtimeOutgoing.end();) {
            AcknowledgedRealtimePacket& acknowledged = iterator->second;
            if (now >= acknowledged.fallbackAtMs) {
                acknowledged.packet.acknowledgedRealtime = false;
                acknowledged.packet.queuedAtMs = now;
                reliableFallbacks.push_back(std::move(acknowledged.packet));
                iterator = acknowledgedRealtimeOutgoing.erase(iterator);
                telemetryAcknowledgedEventFallbacks.fetch_add(1);
                continue;
            }
            if (now >= acknowledged.nextSendMs) {
                packets.push_back(acknowledged.packet);
                if (acknowledged.firstSentMs == 0) {
                    acknowledged.firstSentMs = now;
                } else {
                    telemetryAcknowledgedEventRetries.fetch_add(1);
                }
                ++acknowledged.transmissionCount;
                acknowledged.nextSendMs = now + acknowledged.retryIntervalMs;
            }
            ++iterator;
        }
        telemetryPendingAcknowledgements =
            static_cast<uint32_t>(acknowledgedRealtimeOutgoing.size());
        credentials = realtimeCredentials;
    }
    while (!reliableFallbacks.empty()) {
        QueueReliable(std::move(reliableFallbacks.front()));
        reliableFallbacks.pop_front();
    }

    while (!packets.empty()) {
        std::vector<uint8_t> data =
            EncodeRealtimeDatagram(packets.front().acknowledgedRealtime ? kRealtimeAcknowledgedData
                                                                         : kRealtimeData,
                                   credentials.scope, credentials.participantId,
                                   credentials.tokenHigh, credentials.tokenLow, packets.front().bytes);
        if (!SendRealtimeData(socket, peerAddress, data)) {
            std::lock_guard<std::mutex> lock(outgoingMutex);
            while (!packets.empty()) {
                if (!packets.front().acknowledgedRealtime &&
                    packets.front().type != MessageType::AttackIntent) {
                    outgoing.push_back(std::move(packets.front()));
                }
                packets.pop_front();
            }
            SPDLOG_WARN("[HyruleCoop] UDP realtime send failed; returning to TCP: {}", SDLNet_GetError());
            return false;
        }
        packets.pop_front();
    }
    return true;
}

bool DirectSession::SendRealtimeData(UDPsocket socket, const IPaddress& peerAddress,
                                     const std::vector<uint8_t>& data) {
    telemetryRealtimeDatagramsSent.fetch_add(1);
    telemetryRealtimeBytesSent.fetch_add(data.size());
    uint32_t delayMs = 0;
    std::vector<uint8_t> heldDatagram;
    {
        std::lock_guard<std::mutex> lock(realtimeTestMutex);
        ++realtimeTestDatagramCount;
        if (realtimeTestDropEvery != 0 && realtimeTestDatagramCount % realtimeTestDropEvery == 0) {
            return true;
        }
        delayMs = realtimeTestDelayMs;
        if (realtimeTestReorderPairs) {
            if (realtimeTestHeldDatagram.empty()) {
                realtimeTestHeldDatagram = data;
                return true;
            }
            heldDatagram.swap(realtimeTestHeldDatagram);
        }
    }

    if (delayMs != 0) {
        SDL_Delay(delayMs);
    }
    const auto send = [&](const std::vector<uint8_t>& bytes) {
        UDPpacket packet{};
        packet.data = const_cast<uint8_t*>(bytes.data());
        packet.len = static_cast<int>(bytes.size());
        packet.maxlen = packet.len;
        packet.address = peerAddress;
        return SDLNet_UDP_Send(socket, -1, &packet) != 0;
    };
    return send(data) && (heldDatagram.empty() || send(heldDatagram));
}

void DirectSession::ConfigureRealtimeTestImpairmentFromEnvironment() {
    const uint32_t dropEvery = ReadTestEnvironmentU32("HYRULE_COOP_TEST_UDP_DROP_EVERY", 1000000);
    const uint32_t delayMs = ReadTestEnvironmentU32("HYRULE_COOP_TEST_UDP_DELAY_MS", 1000);
    const bool reorderPairs = ReadTestEnvironmentU32("HYRULE_COOP_TEST_UDP_REORDER_PAIRS", 1) != 0;
    {
        std::lock_guard<std::mutex> lock(realtimeTestMutex);
        realtimeTestDropEvery = dropEvery;
        realtimeTestDatagramCount = 0;
        realtimeTestDelayMs = delayMs;
        realtimeTestReorderPairs = reorderPairs;
        realtimeTestHeldDatagram.clear();
    }
    if (dropEvery != 0 || delayMs != 0 || reorderPairs) {
        SPDLOG_WARN("[HyruleCoop] Test-only UDP impairment enabled: dropEvery={}, delayMs={}, reorderPairs={}",
                    dropEvery, delayMs, reorderPairs);
    }
}

void DirectSession::MaintainRealtimeBinding(UDPsocket socket, IPaddress& peerAddress, bool& hasPeerAddress) {
    if (!realtimeConfigured) {
        return;
    }

    const uint64_t now = SDL_GetTicks64();
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        if (realtimeReady && lastRealtimeReceiveMs != 0 && now - lastRealtimeReceiveMs > kRealtimeTimeoutMs) {
            realtimeReady = false;
        }
    }
    if (!realtimeReady) {
        FallBackRealtimePackets();
    }

    bool sendPing = false;
    if (realtimeReady && hasPeerAddress) {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        if (lastRealtimePingMs == 0 || now - lastRealtimePingMs >= kRealtimePingIntervalMs) {
            lastRealtimePingMs = now;
            sendPing = true;
        }
    }
    if (sendPing) {
        std::vector<uint8_t> payload;
        AppendU64(payload, now);
        SendRealtimeControl(socket, peerAddress, kRealtimePing, payload);
    }

    if (role != SessionRole::Client || !hasPeerAddress) {
        return;
    }

    bool sendBind = false;
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        if (lastRealtimeBindAttemptMs == 0 || now - lastRealtimeBindAttemptMs >= kRealtimeBindIntervalMs) {
            lastRealtimeBindAttemptMs = now;
            sendBind = true;
        }
    }
    if (sendBind) {
        SendRealtimeControl(socket, peerAddress, kRealtimeBind);
    }
}

bool DirectSession::SendRealtimeControl(UDPsocket socket, const IPaddress& peerAddress, uint16_t kind,
                                        const std::vector<uint8_t>& payload) {
    RealtimeCredentials credentials;
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        credentials = realtimeCredentials;
    }
    std::vector<uint8_t> data = EncodeRealtimeDatagram(kind, credentials.scope, credentials.participantId,
                                                         credentials.tokenHigh, credentials.tokenLow, payload);
    return SendRealtimeData(socket, peerAddress, data);
}

void DirectSession::ReceiveRealtime(UDPsocket socket, IPaddress& peerAddress, bool& hasPeerAddress) {
    std::array<uint8_t, kMaximumRealtimeDatagramSize> buffer{};
    UDPpacket datagram{};
    datagram.data = buffer.data();
    datagram.maxlen = static_cast<int>(buffer.size());

    while (SDLNet_UDP_Recv(socket, &datagram) != 0) {
        telemetryRealtimeDatagramsReceived.fetch_add(1);
        telemetryRealtimeBytesReceived.fetch_add(static_cast<uint64_t>(datagram.len));
        RealtimeCredentials credentials;
        {
            std::lock_guard<std::mutex> lock(realtimeMutex);
            credentials = realtimeCredentials;
        }
        uint16_t kind = 0;
        if (!realtimeConfigured ||
            !SameCredentials(datagram.data, static_cast<size_t>(datagram.len), credentials.scope,
                             credentials.participantId, credentials.tokenHigh, credentials.tokenLow, kind)) {
            continue;
        }

        const bool fromExpectedPeer = hasPeerAddress && datagram.address.host == peerAddress.host &&
                                      datagram.address.port == peerAddress.port;
        if (role == SessionRole::Host && kind == kRealtimeBind) {
            peerAddress = datagram.address;
            hasPeerAddress = true;
            {
                std::lock_guard<std::mutex> lock(realtimeMutex);
                lastRealtimeReceiveMs = SDL_GetTicks64();
            }
            realtimeReady = true;
            SendRealtimeControl(socket, peerAddress, kRealtimeBindAck);
            continue;
        }
        if (!fromExpectedPeer) {
            continue;
        }
        const uint64_t receivedAtMs = SDL_GetTicks64();
        {
            std::lock_guard<std::mutex> lock(realtimeMutex);
            lastRealtimeReceiveMs = receivedAtMs;
        }
        if (role == SessionRole::Client && kind == kRealtimeBindAck) {
            realtimeReady = true;
            continue;
        }
        if (!realtimeReady) {
            continue;
        }

        if (kind == kRealtimePing && datagram.len == static_cast<int>(kRealtimeHeaderSize + 8)) {
            std::vector<uint8_t> payload(datagram.data + kRealtimeHeaderSize, datagram.data + datagram.len);
            SendRealtimeControl(socket, peerAddress, kRealtimePong, payload);
            continue;
        }
        if (kind == kRealtimePong && datagram.len == static_cast<int>(kRealtimeHeaderSize + 8)) {
            const uint64_t sentAtMs = ReadU64(datagram.data + kRealtimeHeaderSize);
            if (sentAtMs <= receivedAtMs) {
                ObserveRoundTrip(static_cast<uint32_t>(
                    std::min<uint64_t>(receivedAtMs - sentAtMs, UINT32_MAX)));
            }
            continue;
        }
        if (kind == kRealtimeAcknowledgement &&
            datagram.len == static_cast<int>(kRealtimeHeaderSize + 4)) {
            const uint32_t sequence = ReadU32(datagram.data + kRealtimeHeaderSize);
            uint64_t firstSentMs = 0;
            {
                std::lock_guard<std::mutex> lock(realtimeMutex);
                const auto pending = acknowledgedRealtimeOutgoing.find(sequence);
                if (pending != acknowledgedRealtimeOutgoing.end()) {
                    firstSentMs = pending->second.firstSentMs;
                    acknowledgedRealtimeOutgoing.erase(pending);
                    telemetryPendingAcknowledgements =
                        static_cast<uint32_t>(acknowledgedRealtimeOutgoing.size());
                }
            }
            if (firstSentMs != 0 && firstSentMs <= receivedAtMs) {
                ObserveRoundTrip(static_cast<uint32_t>(
                    std::min<uint64_t>(receivedAtMs - firstSentMs, UINT32_MAX)));
                telemetryAcknowledgedEventsReceived.fetch_add(1);
            }
            continue;
        }
        if ((kind != kRealtimeData && kind != kRealtimeAcknowledgedData) ||
            datagram.len <= static_cast<int>(kRealtimeHeaderSize)) {
            continue;
        }

        std::vector<uint8_t> encoded(datagram.data + kRealtimeHeaderSize, datagram.data + datagram.len);
        Packet packet;
        size_t consumed = 0;
        std::string error;
        if (TryDecodePacket(encoded, packet, consumed, error) != DecodeResult::Decoded || consumed != encoded.size() ||
            !IsRealtimeDatagram(packet.type)) {
            continue;
        }
        packet.receivedAtMs = receivedAtMs;
        packet.acknowledgedRealtime = kind == kRealtimeAcknowledgedData;
        if (packet.type == MessageType::PlayerSnapshot) {
            ObservePlayerSnapshotArrival(receivedAtMs);
        }
        if (packet.acknowledgedRealtime) {
            std::vector<uint8_t> acknowledgement;
            AppendU32(acknowledgement, packet.sequence);
            SendRealtimeControl(socket, peerAddress, kRealtimeAcknowledgement, acknowledgement);

            bool duplicate = false;
            {
                std::lock_guard<std::mutex> lock(realtimeMutex);
                duplicate = receivedAcknowledgedRealtime.contains(packet.sequence);
                if (!duplicate) {
                    receivedAcknowledgedRealtime.insert(packet.sequence);
                    receivedAcknowledgedRealtimeOrder.push_back(packet.sequence);
                    while (receivedAcknowledgedRealtimeOrder.size() > kAcknowledgedReceiveWindow) {
                        receivedAcknowledgedRealtime.erase(receivedAcknowledgedRealtimeOrder.front());
                        receivedAcknowledgedRealtimeOrder.pop_front();
                    }
                }
            }
            if (duplicate) {
                telemetryRealtimeDuplicates.fetch_add(1);
                continue;
            }
        }
        PushIncoming(std::move(packet));
    }
}

bool DirectSession::ReceiveAvailable(TCPsocket socket, SDLNet_SocketSet socketSet,
                                     std::vector<uint8_t>& receiveBuffer) {
    const int ready = SDLNet_CheckSockets(socketSet, 5);
    if (ready < 0) {
        FailPeer(SDLNet_GetError());
        return false;
    }
    if (ready == 0 || !SDLNet_SocketReady(socket)) {
        return true;
    }

    std::array<uint8_t, 4096> chunk;
    const int received = SDLNet_TCP_Recv(socket, chunk.data(), static_cast<int>(chunk.size()));
    if (received <= 0) {
        return false;
    }
    telemetryTcpBytesReceived.fetch_add(static_cast<uint64_t>(received));
    receiveBuffer.insert(receiveBuffer.end(), chunk.begin(), chunk.begin() + received);

    while (!receiveBuffer.empty()) {
        Packet packet;
        size_t consumed = 0;
        std::string error;
        const DecodeResult result = TryDecodePacket(receiveBuffer, packet, consumed, error);
        if (result == DecodeResult::NeedMoreData) {
            break;
        }
        if (result == DecodeResult::Invalid) {
            FailPeer(error);
            return false;
        }
        packet.receivedAtMs = SDL_GetTicks64();
        PushIncoming(std::move(packet));
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + consumed);
    }
    return true;
}

void DirectSession::PushIncoming(Packet packet) {
    if (IsRealtimeSnapshot(packet.type) && !packet.acknowledgedRealtime) {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        const auto key = std::make_pair(packet.type, FreshnessStream(packet.type, packet.streamId));
        const auto previous = latestRealtimeSequences.find(key);
        if (previous != latestRealtimeSequences.end() && !IsNewerSequence(packet.sequence, previous->second)) {
            telemetryRealtimeStaleSnapshots.fetch_add(1);
            return;
        }
        latestRealtimeSequences[key] = packet.sequence;
    }

    std::lock_guard<std::mutex> lock(incomingMutex);
    if (IsReplaceableSnapshot(packet.type) && !packet.acknowledgedRealtime) {
        const uint64_t packetStream = packet.streamId;
        for (auto iterator = incoming.begin(); iterator != incoming.end();) {
            if (!iterator->acknowledgedRealtime && iterator->type == packet.type &&
                iterator->streamId == packetStream) {
                iterator = incoming.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }
    if (packet.receivedAtMs == 0) {
        packet.receivedAtMs = SDL_GetTicks64();
    }
    incoming.push_back(std::move(packet));
    telemetryIncomingQueueDepth = static_cast<uint32_t>(incoming.size());
    AtomicMaximum(telemetryIncomingQueueHighWater, static_cast<uint32_t>(incoming.size()));
}

void DirectSession::FallBackRealtimePackets() {
    std::deque<OutgoingPacket> packets;
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        packets.swap(realtimeOutgoing);
        for (auto& [key, repeated] : repeatedRealtimeOutgoing) {
            packets.push_back(std::move(repeated.packet));
        }
        repeatedRealtimeOutgoing.clear();
        for (auto& [sequence, acknowledged] : acknowledgedRealtimeOutgoing) {
            acknowledged.packet.acknowledgedRealtime = false;
            acknowledged.packet.queuedAtMs = SDL_GetTicks64();
            packets.push_back(std::move(acknowledged.packet));
            telemetryAcknowledgedEventFallbacks.fetch_add(1);
        }
        acknowledgedRealtimeOutgoing.clear();
        telemetryPendingAcknowledgements = 0;
    }
    if (packets.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(outgoingMutex);
    while (!packets.empty()) {
        OutgoingPacket packet = std::move(packets.front());
        packets.pop_front();
        const uint64_t packetStream = packet.streamId;
        for (auto iterator = outgoing.begin(); iterator != outgoing.end();) {
            if (iterator->type == packet.type && iterator->streamId == packetStream) {
                iterator = outgoing.erase(iterator);
            } else {
                ++iterator;
            }
        }
        outgoing.push_back(std::move(packet));
    }
    telemetryReliableQueueDepth = static_cast<uint32_t>(outgoing.size());
    AtomicMaximum(telemetryReliableQueueHighWater, static_cast<uint32_t>(outgoing.size()));
}

bool DirectSession::SendAll(TCPsocket socket, const std::vector<uint8_t>& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int result = SDLNet_TCP_Send(socket, data.data() + sent, static_cast<int>(data.size() - sent));
        if (result <= 0) {
            return false;
        }
        telemetryTcpBytesSent.fetch_add(static_cast<uint64_t>(result));
        sent += static_cast<size_t>(result);
    }
    return true;
}

void DirectSession::Fail(const std::string& message) {
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        lastError = message.empty() ? "unknown network error" : message;
    }
    state = TransportState::Error;
    running = false;
    SPDLOG_ERROR("[HyruleCoop] {}", GetLastError());
}

void DirectSession::FailPeer(const std::string& message) {
    if (role == SessionRole::Host) {
        state = TransportState::Disconnected;
        SPDLOG_WARN("[HyruleCoop] Dropping peer: {}", message.empty() ? "connection lost" : message);
        return;
    }
    Fail(message);
}

void DirectSession::ClearPacketQueues() {
    {
        std::lock_guard<std::mutex> lock(incomingMutex);
        incoming.clear();
        telemetryIncomingQueueDepth = 0;
    }
    {
        std::lock_guard<std::mutex> lock(outgoingMutex);
        outgoing.clear();
        telemetryReliableQueueDepth = 0;
    }
    {
        std::lock_guard<std::mutex> lock(realtimeMutex);
        realtimeOutgoing.clear();
        repeatedRealtimeOutgoing.clear();
        acknowledgedRealtimeOutgoing.clear();
        latestRealtimeSequences.clear();
        receivedAcknowledgedRealtime.clear();
        receivedAcknowledgedRealtimeOrder.clear();
        telemetryPendingAcknowledgements = 0;
    }
    {
        std::lock_guard<std::mutex> lock(realtimeTestMutex);
        realtimeTestDatagramCount = 0;
        realtimeTestHeldDatagram.clear();
    }
}

void DirectSession::ResetTelemetry() {
    telemetryRoundTripMs = 0;
    telemetryRoundTripJitterMs = 0;
    telemetrySnapshotIntervalMs = 0;
    telemetrySnapshotJitterMs = 0;
    telemetryLastSnapshotArrivalMs = 0;
    telemetryReliableQueueDepth = 0;
    telemetryReliableQueueHighWater = 0;
    telemetryIncomingQueueDepth = 0;
    telemetryIncomingQueueHighWater = 0;
    telemetryPendingAcknowledgements = 0;
    telemetryLastTcpQueueDelayMs = 0;
    telemetryMaximumTcpQueueDelayMs = 0;
    telemetryLastApplicationDelayMs = 0;
    telemetryMaximumApplicationDelayMs = 0;
    telemetryRealtimeDatagramsSent = 0;
    telemetryRealtimeDatagramsReceived = 0;
    telemetryRealtimeBytesSent = 0;
    telemetryRealtimeBytesReceived = 0;
    telemetryRealtimeDuplicates = 0;
    telemetryRealtimeStaleSnapshots = 0;
    telemetryAcknowledgedEventsSent = 0;
    telemetryAcknowledgedEventRetries = 0;
    telemetryAcknowledgedEventsReceived = 0;
    telemetryAcknowledgedEventFallbacks = 0;
    telemetryTcpBytesSent = 0;
    telemetryTcpBytesReceived = 0;
}

void DirectSession::ObserveRoundTrip(uint32_t milliseconds) {
    const uint32_t previous = telemetryRoundTripMs.load();
    const uint32_t deviation = previous > milliseconds ? previous - milliseconds : milliseconds - previous;
    telemetryRoundTripJitterMs = Smooth(telemetryRoundTripJitterMs.load(), deviation);
    telemetryRoundTripMs = Smooth(previous, milliseconds);
}

void DirectSession::ObservePlayerSnapshotArrival(uint64_t nowMs) {
    const uint64_t previousArrival = telemetryLastSnapshotArrivalMs.exchange(nowMs);
    if (previousArrival == 0 || nowMs <= previousArrival) {
        return;
    }
    const uint32_t interval = static_cast<uint32_t>(std::min<uint64_t>(nowMs - previousArrival, UINT32_MAX));
    const uint32_t previousInterval = telemetrySnapshotIntervalMs.load();
    const uint32_t deviation =
        previousInterval > interval ? previousInterval - interval : interval - previousInterval;
    telemetrySnapshotJitterMs = Smooth(telemetrySnapshotJitterMs.load(), deviation);
    telemetrySnapshotIntervalMs = Smooth(previousInterval, interval);
}

} // namespace HyruleCoop
