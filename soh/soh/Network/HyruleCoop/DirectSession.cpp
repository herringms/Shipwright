#include "DirectSession.h"

#include <SDL2/SDL.h>
#include <array>
#include <chrono>
#include <spdlog/spdlog.h>

namespace HyruleCoop {
namespace {

bool IsReplaceableSnapshot(MessageType type) {
    return type == MessageType::ClockSnapshot || type == MessageType::PlayerSnapshot ||
           type == MessageType::SceneFlagsSnapshot || type == MessageType::ActorSnapshot ||
           type == MessageType::CycleSnapshot || type == MessageType::ProgressionSnapshot;
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

    role = SessionRole::None;
    state = TransportState::Stopped;
    nextSequence = 1;
    connectionGeneration = 0;
    disconnectPeerRequested = false;
}

void DirectSession::DisconnectPeer() {
    if (role == SessionRole::Host && state == TransportState::Connected) {
        disconnectPeerRequested = true;
    }
}

bool DirectSession::Send(MessageType type, const std::vector<uint8_t>& payload, uint64_t streamId) {
    if (!running || state != TransportState::Connected || payload.size() > kMaximumPayloadSize) {
        return false;
    }
    Packet packet{ type, nextSequence.fetch_add(1), payload, streamId };
    std::lock_guard<std::mutex> lock(outgoingMutex);
    if (IsReplaceableSnapshot(type)) {
        for (auto iterator = outgoing.begin(); iterator != outgoing.end(); ++iterator) {
            if (iterator->type == type && iterator->streamId == streamId) {
                // This packet has a newer sequence number, so preserve wire ordering by moving it to the tail.
                outgoing.erase(iterator);
                break;
            }
        }
    }
    outgoing.push_back({ type, streamId, EncodePacket(packet) });
    return true;
}

std::vector<Packet> DirectSession::TakeIncomingPackets() {
    std::vector<Packet> packets;
    std::lock_guard<std::mutex> lock(incomingMutex);
    packets.reserve(incoming.size());
    while (!incoming.empty()) {
        packets.push_back(std::move(incoming.front()));
        incoming.pop_front();
    }
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

    SPDLOG_INFO("[HyruleCoop] Listening on TCP port {}", port);
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
        RunConnectedSocket(peer);
        SDLNet_TCP_Close(peer);
        ClearPacketQueues();
        if (state == TransportState::Error) {
            break;
        }
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

    RunConnectedSocket(peer);
    SDLNet_TCP_Close(peer);
}

void DirectSession::RunConnectedSocket(TCPsocket socket) {
    SDLNet_SocketSet socketSet = SDLNet_AllocSocketSet(1);
    if (socketSet == nullptr || SDLNet_TCP_AddSocket(socketSet, socket) < 0) {
        if (socketSet != nullptr) {
            SDLNet_FreeSocketSet(socketSet);
        }
        Fail(SDLNet_GetError());
        return;
    }

    disconnectPeerRequested = false;
    connectionGeneration.fetch_add(1);
    state = TransportState::Connected;
    std::vector<uint8_t> receiveBuffer;
    while (running) {
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
    }
    while (!packets.empty()) {
        if (!SendAll(socket, packets.front().bytes)) {
            FailPeer(SDLNet_GetError());
            return false;
        }
        packets.pop_front();
    }
    return true;
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
        {
            std::lock_guard<std::mutex> lock(incomingMutex);
            if (IsReplaceableSnapshot(packet.type)) {
                for (auto iterator = incoming.begin(); iterator != incoming.end();) {
                    if (iterator->type == packet.type && iterator->streamId == packet.streamId) {
                        iterator = incoming.erase(iterator);
                    } else {
                        ++iterator;
                    }
                }
            }
            incoming.push_back(std::move(packet));
        }
        receiveBuffer.erase(receiveBuffer.begin(), receiveBuffer.begin() + consumed);
    }
    return true;
}

bool DirectSession::SendAll(TCPsocket socket, const std::vector<uint8_t>& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        const int result = SDLNet_TCP_Send(socket, data.data() + sent, static_cast<int>(data.size() - sent));
        if (result <= 0) {
            return false;
        }
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
    }
    {
        std::lock_guard<std::mutex> lock(outgoingMutex);
        outgoing.clear();
    }
}

} // namespace HyruleCoop
