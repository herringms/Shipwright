#define SDL_MAIN_HANDLED
#include "soh/Network/HyruleCoop/DirectSession.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include <array>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace HyruleCoop;

static bool WaitForState(DirectSession& session, TransportState expected, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (session.GetState() == expected) {
            return true;
        }
        if (session.GetState() == TransportState::Error) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

static std::vector<Packet> WaitForPackets(DirectSession& session, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        std::vector<Packet> packets = session.TakeIncomingPackets();
        if (!packets.empty()) {
            return packets;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return {};
}

static bool WaitForRealtime(DirectSession& host, DirectSession& guest, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (host.IsRealtimeReady() && guest.IsRealtimeReady()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

int main() {
    assert(SDL_Init(0) == 0);
    assert(SDLNet_Init() == 0);
    assert(SDL_setenv("HYRULE_COOP_TEST_UDP_DROP_EVERY", "3", 1) == 0);
    assert(SDL_setenv("HYRULE_COOP_TEST_UDP_DELAY_MS", "10", 1) == 0);
    assert(SDL_setenv("HYRULE_COOP_TEST_UDP_REORDER_PAIRS", "1", 1) == 0);

    DirectSession host;
    DirectSession guest;
    constexpr uint16_t port = 43491;

    assert(host.StartHost(port));
    assert(WaitForState(host, TransportState::Listening, std::chrono::seconds(2)));
    assert(guest.StartClient("127.0.0.1", port));
    assert(WaitForState(host, TransportState::Connected, std::chrono::seconds(2)));
    assert(WaitForState(guest, TransportState::Connected, std::chrono::seconds(2)));
    assert(host.GetConnectionGeneration() == 1);

    HelloMessage hello;
    hello.gameId = GameId::OcarinaOfTime;
    hello.buildId = "same-build";
    hello.playerName = "Tillya";
    hello.capabilities = { Capability::Coordination };
    assert(guest.Send(MessageType::Hello, EncodeHello(hello)));
    const std::vector<Packet> hostPackets = WaitForPackets(host, std::chrono::seconds(2));
    assert(hostPackets.size() == 1);
    assert(hostPackets[0].type == MessageType::Hello);
    const auto receivedHello = DecodeHello(hostPackets[0].payload);
    assert(receivedHello.has_value());
    assert(receivedHello->playerName == "Tillya");

    HelloAckMessage ack;
    ack.accepted = true;
    ack.playerName = "Host Link";
    ack.participantId = 2;
    ack.sessionEpoch = 100;
    ack.resumeTokenHigh = 0x1234;
    ack.resumeTokenLow = 0x5678;
    ack.capabilities = hello.capabilities;
    host.ConfigureRealtime({ 100, 0 }, 2, ack.resumeTokenHigh, ack.resumeTokenLow);
    assert(host.Send(MessageType::HelloAck, EncodeHelloAck(ack)));
    const std::vector<Packet> guestPackets = WaitForPackets(guest, std::chrono::seconds(2));
    assert(guestPackets.size() == 1);
    assert(guestPackets[0].type == MessageType::HelloAck);
    const auto receivedAck = DecodeHelloAck(guestPackets[0].payload);
    assert(receivedAck.has_value());
    assert(receivedAck->accepted);
    assert(receivedAck->participantId == 2);
    assert(receivedAck->playerName == "Host Link");
    guest.ConfigureRealtime({ 100, 0 }, 2, ack.resumeTokenHigh, ack.resumeTokenLow);
    assert(WaitForRealtime(host, guest, std::chrono::seconds(2)));

    AttackIntentMessage attack;
    attack.scope = { 100, 0 };
    attack.participantId = 2;
    attack.requestId = 77;
    attack.entityId = 0xBABA;
    attack.playerTick = 42;
    attack.scene = 1;
    attack.attackKind = 1;
    assert(guest.SendRepeatedRealtime(MessageType::AttackIntent, EncodeAttackIntent(attack), attack.entityId, 40,
                                      400));
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    const std::vector<Packet> attackPackets = host.TakeIncomingPackets();
    assert(attackPackets.size() >= 2);
    const uint32_t attackSequence = attackPackets.front().sequence;
    for (const Packet& packet : attackPackets) {
        assert(packet.type == MessageType::AttackIntent);
        assert(packet.sequence == attackSequence);
        const auto receivedAttack = DecodeAttackIntent(packet.payload);
        assert(receivedAttack.has_value());
        assert(receivedAttack->requestId == attack.requestId);
        assert(receivedAttack->entityId == attack.entityId);
    }
    guest.CancelRepeatedRealtime(MessageType::AttackIntent, attack.entityId);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    host.TakeIncomingPackets();

    ActorSnapshotMessage attackResult;
    attackResult.scope = { 100, 0 };
    attackResult.entityId = attack.entityId;
    attackResult.acknowledgedRequestId = attack.requestId;
    attackResult.actorId = 1;
    attackResult.alive = true;
    assert(host.SendReliable(MessageType::ActorSnapshot, EncodeActorSnapshot(attackResult), attackResult.entityId));
    const std::vector<Packet> attackResultPackets = WaitForPackets(guest, std::chrono::seconds(2));
    assert(attackResultPackets.size() == 1);
    assert(attackResultPackets[0].type == MessageType::ActorSnapshot);

    for (uint32_t tick = 0; tick < 100; ++tick) {
        PlayerSnapshotMessage snapshot;
        snapshot.tick = tick;
        assert(host.Send(MessageType::PlayerSnapshot, EncodePlayerSnapshot(snapshot), 11));
        snapshot.tick = 1000 + tick;
        assert(host.Send(MessageType::PlayerSnapshot, EncodePlayerSnapshot(snapshot), 22));
        ProgressionSnapshotMessage progression;
        progression.scope = { 100, 0 };
        progression.revision = tick;
        assert(host.Send(MessageType::ProgressionSnapshot, EncodeProgressionSnapshot(progression), 1));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const std::vector<Packet> snapshots = guest.TakeIncomingPackets();
    bool foundProgression = false;
    bool foundFirstPlayer = false;
    bool foundSecondPlayer = false;
    for (const Packet& packet : snapshots) {
        if (packet.type == MessageType::ProgressionSnapshot) {
            const auto progression = DecodeProgressionSnapshot(packet.payload);
            assert(packet.streamId == 1);
            assert(progression.has_value());
            assert(progression->revision == 99);
            foundProgression = true;
            continue;
        }
        assert(packet.type == MessageType::PlayerSnapshot);
        const auto latestSnapshot = DecodePlayerSnapshot(packet.payload);
        assert(latestSnapshot.has_value());
        if (packet.streamId == 11) {
            assert(latestSnapshot->tick >= 90);
            foundFirstPlayer = true;
        } else {
            assert(packet.streamId == 22);
            assert(latestSnapshot->tick >= 1090);
            foundSecondPlayer = true;
        }
    }
    assert(foundProgression);
    assert(foundFirstPlayer);
    assert(foundSecondPlayer);

    host.DisableRealtime();
    assert(!host.IsRealtimeReady());

    attack.requestId = 78;
    attack.entityId = 0xBABB;
    assert(guest.SendRepeatedRealtime(MessageType::AttackIntent, EncodeAttackIntent(attack), attack.entityId, 40,
                                      160));
    const std::vector<Packet> fallbackAttackPackets = WaitForPackets(host, std::chrono::seconds(2));
    assert(fallbackAttackPackets.size() == 1);
    assert(fallbackAttackPackets[0].type == MessageType::AttackIntent);
    const auto fallbackAttack = DecodeAttackIntent(fallbackAttackPackets[0].payload);
    assert(fallbackAttack.has_value());
    assert(fallbackAttack->requestId == attack.requestId);
    assert(fallbackAttack->entityId == attack.entityId);

    ClockSnapshotMessage fallbackClock{};
    fallbackClock.scope = { 100, 0 };
    fallbackClock.dayTime = 0x4000;
    assert(host.Send(MessageType::ClockSnapshot, EncodeClockSnapshot(fallbackClock)));
    const std::vector<Packet> fallbackPackets = WaitForPackets(guest, std::chrono::seconds(2));
    assert(fallbackPackets.size() == 1);
    assert(fallbackPackets[0].type == MessageType::ClockSnapshot);
    const auto receivedFallbackClock = DecodeClockSnapshot(fallbackPackets[0].payload);
    assert(receivedFallbackClock.has_value());
    assert(receivedFallbackClock->dayTime == fallbackClock.dayTime);

    guest.Stop();
    assert(WaitForState(host, TransportState::Listening, std::chrono::seconds(2)));

    IPaddress rawAddress;
    assert(SDLNet_ResolveHost(&rawAddress, "127.0.0.1", port) == 0);
    TCPsocket malformedGuest = SDLNet_TCP_Open(&rawAddress);
    assert(malformedGuest != nullptr);
    assert(WaitForState(host, TransportState::Connected, std::chrono::seconds(2)));
    assert(host.GetConnectionGeneration() == 2);
    const std::array<uint8_t, kHeaderSize> malformedHeader{};
    assert(SDLNet_TCP_Send(malformedGuest, malformedHeader.data(), malformedHeader.size()) ==
           static_cast<int>(malformedHeader.size()));
    assert(WaitForState(host, TransportState::Listening, std::chrono::seconds(2)));
    SDLNet_TCP_Close(malformedGuest);

    DirectSession rejectedGuest;
    assert(rejectedGuest.StartClient("127.0.0.1", port));
    assert(WaitForState(host, TransportState::Connected, std::chrono::seconds(2)));
    assert(WaitForState(rejectedGuest, TransportState::Connected, std::chrono::seconds(2)));
    assert(host.GetConnectionGeneration() == 3);
    HelloAckMessage rejectedAck;
    rejectedAck.reason = "different build";
    assert(host.Send(MessageType::HelloAck, EncodeHelloAck(rejectedAck)));
    host.DisconnectPeer();
    const std::vector<Packet> rejectedPackets = WaitForPackets(rejectedGuest, std::chrono::seconds(2));
    assert(rejectedPackets.size() == 1);
    const auto rejection = DecodeHelloAck(rejectedPackets[0].payload);
    assert(rejection.has_value() && !rejection->accepted);
    assert(WaitForState(host, TransportState::Listening, std::chrono::seconds(2)));
    rejectedGuest.Stop();

    DirectSession reconnectedGuest;
    assert(reconnectedGuest.StartClient("127.0.0.1", port));
    assert(WaitForState(host, TransportState::Connected, std::chrono::seconds(2)));
    assert(WaitForState(reconnectedGuest, TransportState::Connected, std::chrono::seconds(2)));
    assert(host.GetConnectionGeneration() == 4);
    assert(reconnectedGuest.Send(MessageType::Hello, EncodeHello(hello)));
    const std::vector<Packet> reconnectPackets = WaitForPackets(host, std::chrono::seconds(2));
    assert(reconnectPackets.size() == 1);
    assert(reconnectPackets[0].type == MessageType::Hello);

    reconnectedGuest.Stop();
    host.Stop();
    SDL_setenv("HYRULE_COOP_TEST_UDP_DROP_EVERY", "0", 1);
    SDL_setenv("HYRULE_COOP_TEST_UDP_DELAY_MS", "0", 1);
    SDL_setenv("HYRULE_COOP_TEST_UDP_REORDER_PAIRS", "0", 1);
    SDLNet_Quit();
    SDL_Quit();
    std::cout << "HyruleCoop direct session tests passed\n";
    return 0;
}
