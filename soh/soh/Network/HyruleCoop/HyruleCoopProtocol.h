#pragma once

#include "Coordination.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace HyruleCoop {

constexpr uint32_t kPacketMagic = 0x48434F50; // HCOP
constexpr uint16_t kProtocolVersion = 5;
constexpr size_t kHeaderSize = 24;
constexpr uint32_t kMaximumPayloadSize = 1024 * 1024;
constexpr size_t kMaximumActorAdapterWords = 64;

enum class GameId : uint8_t {
    OcarinaOfTime = 1,
    MajorasMask = 2,
};

enum class MessageType : uint16_t {
    Hello = 1,
    HelloAck = 2,
    ClockSnapshot = 3,
    SnapshotRequest = 4,
    Heartbeat = 5,
    PlayerSnapshot = 6,
    SceneFlagIntent = 7,
    SceneFlagsSnapshot = 8,
    ActorSnapshot = 9,
    CycleSnapshot = 10,
    BarrierSnapshot = 11,
    BarrierReady = 12,
    AttackIntent = 13,
    CollectibleIntent = 14,
    ProgressionSnapshot = 15,
    ProgressionIntent = 16,
    PlayerPresentation = 17,
};

struct Packet {
    MessageType type;
    uint32_t sequence;
    std::vector<uint8_t> payload;
    uint64_t streamId = 0;
};

enum class DecodeResult {
    NeedMoreData,
    Decoded,
    Invalid,
};

class ByteWriter {
  public:
    void WriteU8(uint8_t value);
    void WriteU16(uint16_t value);
    void WriteU32(uint32_t value);
    void WriteU64(uint64_t value);
    void WriteF32(float value);
    void WriteString(const std::string& value);
    const std::vector<uint8_t>& Data() const;

  private:
    std::vector<uint8_t> data;
};

class ByteReader {
  public:
    explicit ByteReader(const std::vector<uint8_t>& data);

    bool ReadU8(uint8_t& value);
    bool ReadU16(uint16_t& value);
    bool ReadU32(uint32_t& value);
    bool ReadU64(uint64_t& value);
    bool ReadF32(float& value);
    bool ReadString(std::string& value);
    bool AtEnd() const;

  private:
    const std::vector<uint8_t>& data;
    size_t offset = 0;
};

std::vector<uint8_t> EncodePacket(const Packet& packet);
DecodeResult TryDecodePacket(const std::vector<uint8_t>& buffer, Packet& packet, size_t& consumed,
                             std::string& error);

struct HelloMessage {
    GameId gameId;
    std::string buildId;
    std::string playerName;
    uint64_t requestedSessionEpoch = 0;
    uint64_t requestedParticipantId = 0;
    uint64_t resumeTokenHigh = 0;
    uint64_t resumeTokenLow = 0;
    CapabilityList capabilities;
};

struct HelloAckMessage {
    bool accepted = false;
    std::string playerName;
    uint64_t participantId = 0;
    uint64_t sessionEpoch = 0;
    uint32_t worldGeneration = 0;
    uint64_t resumeTokenHigh = 0;
    uint64_t resumeTokenLow = 0;
    CapabilityList capabilities;
    std::string reason;
};

struct ClockSnapshotMessage {
    SessionScope scope;
    uint32_t hostTick;
    uint16_t dayTime;
    uint16_t skyboxTime;
    uint16_t timeSpeed;
    bool night;
};

struct PlayerSnapshotMessage {
    SessionScope scope;
    uint32_t tick = 0;
    int16_t scene = -1;
    int16_t room = -1;
    int32_t entrance = 0;
    int32_t linkAge = 0;
    float position[3] = {};
    int16_t rotation[3] = {};
    std::array<int16_t, 72> joints = {};
    int16_t previousTranslation[3] = {};
    uint8_t movementFlags = 0;
    int16_t upperLimbRotation[3] = {};
    int8_t boots = 0;
    int8_t shield = 0;
    int8_t tunic = 0;
    uint8_t currentMask = 0;
    uint32_t stateFlags1 = 0;
    uint32_t stateFlags2 = 0;
    uint8_t buttonItem = 0;
    int8_t itemAction = 0;
    int8_t heldItemAction = 0;
    uint8_t modelGroup = 0;
    int8_t invincibilityTimer = 0;
    int16_t modelState = 0;
    float modelBlend = 0.0f;
    int8_t actionVariable = 0;
    float linearVelocity = 0.0f;
    int16_t focusActorId = -1;
    int8_t meleeWeaponState = 0;
    int8_t meleeWeaponAnimation = 0;
};

struct PlayerPresentationMessage {
    SessionScope scope;
    uint32_t revision = 0;
    int8_t boots = 0;
    int8_t shield = 0;
    int8_t tunic = 0;
    uint8_t currentMask = 0;
    uint8_t buttonItem = 0;
    int8_t itemAction = 0;
    int8_t heldItemAction = 0;
    uint8_t modelGroup = 0;
};

struct CycleSnapshotMessage {
    SessionScope scope;
    uint32_t hostTick = 0;
    uint32_t cycleGeneration = 0;
    int32_t day = 0;
    uint16_t time = 0;
    uint16_t skyboxTime = 0;
    int32_t timeSpeedOffset = 0;
    uint16_t sceneTimeSpeed = 0;
    int32_t isNight = 0;
    uint8_t weatherMode = 0;
    uint8_t pendingTransition = 0;
};

struct SnapshotRequestMessage {
    SessionScope scope;
    int16_t scene = -1;
    int16_t room = -1;
};

struct SceneFlagIntentMessage {
    SessionScope scope;
    uint64_t participantId = 0;
    uint64_t requestId = 0;
    int16_t scene = -1;
    int16_t flagType = 0;
    int16_t flag = 0;
    bool set = true;
};

struct SceneFlagsSnapshotMessage {
    SessionScope scope;
    uint64_t revision = 0;
    int16_t scene = -1;
    uint32_t chest = 0;
    uint32_t switches = 0;
    uint32_t clear = 0;
    uint32_t collectible = 0;
};

struct ActorSnapshotMessage {
    SessionScope scope;
    uint32_t hostTick = 0;
    uint64_t entityId = 0;
    uint64_t acknowledgedRequestId = 0;
    int16_t scene = -1;
    int16_t room = -1;
    int16_t actorId = -1;
    int16_t params = 0;
    float homePosition[3] = {};
    float position[3] = {};
    float velocity[3] = {};
    float scale[3] = {};
    int16_t worldRotation[3] = {};
    int16_t shapeRotation[3] = {};
    float speed = 0.0f;
    float gravity = 0.0f;
    int16_t health = 0;
    uint16_t stateId = 0;
    float animationFrame = 0.0f;
    float animationSpeed = 0.0f;
    bool alive = true;
    uint8_t adapterWordCount = 0;
    std::array<int16_t, kMaximumActorAdapterWords> adapterState = {};
};

struct BarrierSnapshotMessage {
    BarrierState state;
};

struct BarrierReadyMessage {
    SessionScope scope;
    uint64_t operationEpoch = 0;
    uint64_t participantId = 0;
    int16_t currentScene = -1;
    int16_t currentRoom = -1;
};

struct AttackIntentMessage {
    SessionScope scope;
    uint64_t participantId = 0;
    uint64_t requestId = 0;
    uint64_t entityId = 0;
    uint32_t playerTick = 0;
    int16_t scene = -1;
    uint8_t attackKind = 0;
};

struct CollectibleIntentMessage {
    SessionScope scope;
    uint64_t participantId = 0;
    uint64_t requestId = 0;
    uint64_t locationId = 0;
    int16_t scene = -1;
    int16_t flagType = 0;
    int16_t flag = 0;
};

struct CollectedLocation {
    uint64_t locationId = 0;
    int16_t scene = -1;
    int16_t flagType = 0;
    int16_t flag = 0;
};

enum class ProgressionIntentKind : uint8_t {
    ItemReceived = 1,
    DungeonKeyUsed = 2,
    GlobalFlagChanged = 3,
};

struct SharedProgressionState {
    std::array<uint8_t, 18> durableItems = {};
    std::array<uint8_t, 2> tradeItems = {};
    uint8_t bottleOwnershipMask = 0;
    uint16_t equipment = 0;
    uint32_t upgrades = 0;
    uint32_t questItems = 0;
    std::array<uint8_t, 20> dungeonItems = {};
    std::array<int8_t, 19> dungeonKeys = {};
    int16_t healthCapacity = 0;
    uint8_t magicLevel = 0;
    uint8_t isMagicAcquired = 0;
    uint8_t isDoubleMagicAcquired = 0;
    uint8_t isDoubleDefenseAcquired = 0;
    uint8_t bgsFlag = 0;
    int16_t gsTokens = 0;
    std::array<uint16_t, 14> eventChkInf = {};

    bool operator==(const SharedProgressionState&) const = default;
};

struct ProgressionIntentMessage {
    SessionScope scope;
    uint64_t participantId = 0;
    uint64_t requestId = 0;
    ProgressionIntentKind kind = ProgressionIntentKind::ItemReceived;
    uint16_t itemId = 0;
    uint16_t modIndex = 0;
    uint16_t mapIndex = 0;
    int8_t remainingDungeonKeys = 0;
    int16_t flagType = 0;
    int16_t flag = 0;
    bool set = true;
};

struct ProgressionSnapshotMessage {
    SessionScope scope;
    uint64_t revision = 0;
    std::vector<CollectedLocation> locations;
    SharedProgressionState shared;
};

std::vector<uint8_t> EncodeHello(const HelloMessage& message);
std::optional<HelloMessage> DecodeHello(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeHelloAck(const HelloAckMessage& message);
std::optional<HelloAckMessage> DecodeHelloAck(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeClockSnapshot(const ClockSnapshotMessage& message);
std::optional<ClockSnapshotMessage> DecodeClockSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodePlayerSnapshot(const PlayerSnapshotMessage& message);
std::optional<PlayerSnapshotMessage> DecodePlayerSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodePlayerPresentation(const PlayerPresentationMessage& message);
std::optional<PlayerPresentationMessage> DecodePlayerPresentation(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeCycleSnapshot(const CycleSnapshotMessage& message);
std::optional<CycleSnapshotMessage> DecodeCycleSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeSnapshotRequest(const SnapshotRequestMessage& message);
std::optional<SnapshotRequestMessage> DecodeSnapshotRequest(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeSceneFlagIntent(const SceneFlagIntentMessage& message);
std::optional<SceneFlagIntentMessage> DecodeSceneFlagIntent(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeSceneFlagsSnapshot(const SceneFlagsSnapshotMessage& message);
std::optional<SceneFlagsSnapshotMessage> DecodeSceneFlagsSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeActorSnapshot(const ActorSnapshotMessage& message);
std::optional<ActorSnapshotMessage> DecodeActorSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeBarrierSnapshot(const BarrierSnapshotMessage& message);
std::optional<BarrierSnapshotMessage> DecodeBarrierSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeBarrierReady(const BarrierReadyMessage& message);
std::optional<BarrierReadyMessage> DecodeBarrierReady(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeAttackIntent(const AttackIntentMessage& message);
std::optional<AttackIntentMessage> DecodeAttackIntent(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeCollectibleIntent(const CollectibleIntentMessage& message);
std::optional<CollectibleIntentMessage> DecodeCollectibleIntent(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeProgressionSnapshot(const ProgressionSnapshotMessage& message);
std::optional<ProgressionSnapshotMessage> DecodeProgressionSnapshot(const std::vector<uint8_t>& payload);
std::vector<uint8_t> EncodeProgressionIntent(const ProgressionIntentMessage& message);
std::optional<ProgressionIntentMessage> DecodeProgressionIntent(const std::vector<uint8_t>& payload);

} // namespace HyruleCoop
