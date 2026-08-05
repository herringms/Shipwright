#include "HyruleCoopProtocol.h"

#include <algorithm>
#include <bit>

namespace HyruleCoop {
namespace {

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

uint16_t ReadU16At(const std::vector<uint8_t>& data, size_t offset) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1]);
}

uint32_t ReadU32At(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint32_t>(data[offset]) << 24) | (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

uint64_t ReadU64At(const std::vector<uint8_t>& data, size_t offset) {
    return (static_cast<uint64_t>(ReadU32At(data, offset)) << 32) | ReadU32At(data, offset + 4);
}

void WriteScope(ByteWriter& writer, const SessionScope& scope) {
    writer.WriteU64(scope.sessionEpoch);
    writer.WriteU32(scope.worldGeneration);
}

bool ReadScope(ByteReader& reader, SessionScope& scope) {
    return reader.ReadU64(scope.sessionEpoch) && reader.ReadU32(scope.worldGeneration);
}

void WriteCapabilities(ByteWriter& writer, const CapabilityList& capabilities) {
    const auto normalized = NormalizeCapabilities(capabilities);
    if (!normalized.has_value()) {
        writer.WriteU8(0);
        return;
    }
    writer.WriteU8(static_cast<uint8_t>(normalized->size()));
    for (const std::string& capability : normalized.value()) {
        writer.WriteString(capability);
    }
}

bool ReadCapabilities(ByteReader& reader, CapabilityList& capabilities) {
    uint8_t count = 0;
    if (!reader.ReadU8(count) || count > 64) {
        return false;
    }
    capabilities.clear();
    capabilities.reserve(count);
    for (uint8_t index = 0; index < count; ++index) {
        std::string capability;
        if (!reader.ReadString(capability)) {
            return false;
        }
        capabilities.push_back(std::move(capability));
    }
    const auto normalized = NormalizeCapabilities(capabilities);
    if (!normalized.has_value() || normalized->size() != capabilities.size()) {
        return false;
    }
    capabilities = normalized.value();
    return true;
}

} // namespace

void ByteWriter::WriteU8(uint8_t value) {
    data.push_back(value);
}

void ByteWriter::WriteU16(uint16_t value) {
    AppendU16(data, value);
}

void ByteWriter::WriteU32(uint32_t value) {
    AppendU32(data, value);
}

void ByteWriter::WriteU64(uint64_t value) {
    AppendU64(data, value);
}

void ByteWriter::WriteF32(float value) {
    WriteU32(std::bit_cast<uint32_t>(value));
}

void ByteWriter::WriteString(const std::string& value) {
    const size_t length = std::min(value.size(), static_cast<size_t>(UINT16_MAX));
    WriteU16(static_cast<uint16_t>(length));
    data.insert(data.end(), value.begin(), value.begin() + length);
}

const std::vector<uint8_t>& ByteWriter::Data() const {
    return data;
}

ByteReader::ByteReader(const std::vector<uint8_t>& data) : data(data) {
}

bool ByteReader::ReadU8(uint8_t& value) {
    if (offset + 1 > data.size()) {
        return false;
    }
    value = data[offset++];
    return true;
}

bool ByteReader::ReadU16(uint16_t& value) {
    if (offset + 2 > data.size()) {
        return false;
    }
    value = ReadU16At(data, offset);
    offset += 2;
    return true;
}

bool ByteReader::ReadU32(uint32_t& value) {
    if (offset + 4 > data.size()) {
        return false;
    }
    value = ReadU32At(data, offset);
    offset += 4;
    return true;
}

bool ByteReader::ReadU64(uint64_t& value) {
    if (offset + 8 > data.size()) {
        return false;
    }
    value = ReadU64At(data, offset);
    offset += 8;
    return true;
}

bool ByteReader::ReadF32(float& value) {
    uint32_t bits = 0;
    if (!ReadU32(bits)) {
        return false;
    }
    value = std::bit_cast<float>(bits);
    return true;
}

bool ByteReader::ReadString(std::string& value) {
    uint16_t length;
    if (!ReadU16(length) || offset + length > data.size()) {
        return false;
    }
    value.assign(reinterpret_cast<const char*>(data.data() + offset), length);
    offset += length;
    return true;
}

bool ByteReader::AtEnd() const {
    return offset == data.size();
}

std::vector<uint8_t> EncodePacket(const Packet& packet) {
    std::vector<uint8_t> data;
    data.reserve(kHeaderSize + packet.payload.size());
    AppendU32(data, kPacketMagic);
    AppendU16(data, kProtocolVersion);
    AppendU16(data, static_cast<uint16_t>(packet.type));
    AppendU32(data, static_cast<uint32_t>(packet.payload.size()));
    AppendU32(data, packet.sequence);
    AppendU64(data, packet.streamId);
    data.insert(data.end(), packet.payload.begin(), packet.payload.end());
    return data;
}

DecodeResult TryDecodePacket(const std::vector<uint8_t>& buffer, Packet& packet, size_t& consumed,
                             std::string& error) {
    consumed = 0;
    error.clear();
    if (buffer.size() < kHeaderSize) {
        return DecodeResult::NeedMoreData;
    }

    if (ReadU32At(buffer, 0) != kPacketMagic) {
        error = "invalid packet magic";
        return DecodeResult::Invalid;
    }
    if (ReadU16At(buffer, 4) != kProtocolVersion) {
        error = "unsupported protocol version";
        return DecodeResult::Invalid;
    }

    const uint32_t payloadSize = ReadU32At(buffer, 8);
    if (payloadSize > kMaximumPayloadSize) {
        error = "packet payload exceeds limit";
        return DecodeResult::Invalid;
    }
    if (buffer.size() < kHeaderSize + payloadSize) {
        return DecodeResult::NeedMoreData;
    }

    packet.type = static_cast<MessageType>(ReadU16At(buffer, 6));
    packet.sequence = ReadU32At(buffer, 12);
    packet.streamId = ReadU64At(buffer, 16);
    packet.payload.assign(buffer.begin() + kHeaderSize, buffer.begin() + kHeaderSize + payloadSize);
    consumed = kHeaderSize + payloadSize;
    return DecodeResult::Decoded;
}

std::vector<uint8_t> EncodeHello(const HelloMessage& message) {
    ByteWriter writer;
    writer.WriteU8(static_cast<uint8_t>(message.gameId));
    writer.WriteString(message.buildId);
    writer.WriteString(message.playerName);
    writer.WriteU64(message.requestedSessionEpoch);
    writer.WriteU64(message.requestedParticipantId);
    writer.WriteU64(message.resumeTokenHigh);
    writer.WriteU64(message.resumeTokenLow);
    WriteCapabilities(writer, message.capabilities);
    return writer.Data();
}

std::optional<HelloMessage> DecodeHello(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    uint8_t gameId;
    HelloMessage message;
    if (!reader.ReadU8(gameId) || !reader.ReadString(message.buildId) || !reader.ReadString(message.playerName) ||
        !reader.ReadU64(message.requestedSessionEpoch) || !reader.ReadU64(message.requestedParticipantId) ||
        !reader.ReadU64(message.resumeTokenHigh) || !reader.ReadU64(message.resumeTokenLow) ||
        !ReadCapabilities(reader, message.capabilities) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.gameId = static_cast<GameId>(gameId);
    return message;
}

std::vector<uint8_t> EncodeHelloAck(const HelloAckMessage& message) {
    ByteWriter writer;
    writer.WriteU8(message.accepted ? 1 : 0);
    writer.WriteString(message.playerName);
    writer.WriteU64(message.participantId);
    writer.WriteU64(message.sessionEpoch);
    writer.WriteU32(message.worldGeneration);
    writer.WriteU64(message.resumeTokenHigh);
    writer.WriteU64(message.resumeTokenLow);
    WriteCapabilities(writer, message.capabilities);
    writer.WriteString(message.reason);
    return writer.Data();
}

std::optional<HelloAckMessage> DecodeHelloAck(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    uint8_t accepted;
    HelloAckMessage message;
    if (!reader.ReadU8(accepted) || !reader.ReadString(message.playerName) || !reader.ReadU64(message.participantId) ||
        !reader.ReadU64(message.sessionEpoch) || !reader.ReadU32(message.worldGeneration) ||
        !reader.ReadU64(message.resumeTokenHigh) || !reader.ReadU64(message.resumeTokenLow) ||
        !ReadCapabilities(reader, message.capabilities) || !reader.ReadString(message.reason) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.accepted = accepted != 0;
    return message;
}

std::vector<uint8_t> EncodeClockSnapshot(const ClockSnapshotMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU32(message.hostTick);
    writer.WriteU16(message.dayTime);
    writer.WriteU16(message.skyboxTime);
    writer.WriteU16(message.timeSpeed);
    writer.WriteU8(message.night ? 1 : 0);
    return writer.Data();
}

std::optional<ClockSnapshotMessage> DecodeClockSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    uint8_t night;
    ClockSnapshotMessage message;
    if (!ReadScope(reader, message.scope) || !reader.ReadU32(message.hostTick) ||
        !reader.ReadU16(message.dayTime) || !reader.ReadU16(message.skyboxTime) ||
        !reader.ReadU16(message.timeSpeed) || !reader.ReadU8(night) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.night = night != 0;
    return message;
}

std::vector<uint8_t> EncodePlayerSnapshot(const PlayerSnapshotMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU32(message.tick);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU16(static_cast<uint16_t>(message.room));
    writer.WriteU32(static_cast<uint32_t>(message.entrance));
    writer.WriteU32(static_cast<uint32_t>(message.linkAge));
    for (float value : message.position) {
        writer.WriteF32(value);
    }
    for (int16_t value : message.rotation) {
        writer.WriteU16(static_cast<uint16_t>(value));
    }
    for (int16_t value : message.joints) {
        writer.WriteU16(static_cast<uint16_t>(value));
    }
    for (int16_t value : message.previousTranslation) {
        writer.WriteU16(static_cast<uint16_t>(value));
    }
    writer.WriteU8(message.movementFlags);
    for (int16_t value : message.upperLimbRotation) {
        writer.WriteU16(static_cast<uint16_t>(value));
    }
    writer.WriteU8(static_cast<uint8_t>(message.boots));
    writer.WriteU8(static_cast<uint8_t>(message.shield));
    writer.WriteU8(static_cast<uint8_t>(message.tunic));
    writer.WriteU8(message.currentMask);
    writer.WriteU32(message.stateFlags1);
    writer.WriteU32(message.stateFlags2);
    writer.WriteU8(message.buttonItem);
    writer.WriteU8(static_cast<uint8_t>(message.itemAction));
    writer.WriteU8(static_cast<uint8_t>(message.heldItemAction));
    writer.WriteU8(message.modelGroup);
    writer.WriteU8(static_cast<uint8_t>(message.invincibilityTimer));
    writer.WriteU16(static_cast<uint16_t>(message.modelState));
    writer.WriteF32(message.modelBlend);
    writer.WriteU8(static_cast<uint8_t>(message.actionVariable));
    writer.WriteF32(message.linearVelocity);
    writer.WriteU16(static_cast<uint16_t>(message.focusActorId));
    writer.WriteU8(static_cast<uint8_t>(message.meleeWeaponState));
    writer.WriteU8(static_cast<uint8_t>(message.meleeWeaponAnimation));
    return writer.Data();
}

std::optional<PlayerSnapshotMessage> DecodePlayerSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    PlayerSnapshotMessage message;
    uint16_t signed16 = 0;
    uint32_t signed32 = 0;
    uint8_t signed8 = 0;

    if (!ReadScope(reader, message.scope) || !reader.ReadU32(message.tick) || !reader.ReadU16(signed16)) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(signed16);
    if (!reader.ReadU16(signed16)) {
        return std::nullopt;
    }
    message.room = static_cast<int16_t>(signed16);
    if (!reader.ReadU32(signed32)) {
        return std::nullopt;
    }
    message.entrance = static_cast<int32_t>(signed32);
    if (!reader.ReadU32(signed32)) {
        return std::nullopt;
    }
    message.linkAge = static_cast<int32_t>(signed32);
    for (float& value : message.position) {
        if (!reader.ReadF32(value)) {
            return std::nullopt;
        }
    }
    for (int16_t& value : message.rotation) {
        if (!reader.ReadU16(signed16)) {
            return std::nullopt;
        }
        value = static_cast<int16_t>(signed16);
    }
    for (int16_t& value : message.joints) {
        if (!reader.ReadU16(signed16)) {
            return std::nullopt;
        }
        value = static_cast<int16_t>(signed16);
    }
    for (int16_t& value : message.previousTranslation) {
        if (!reader.ReadU16(signed16)) {
            return std::nullopt;
        }
        value = static_cast<int16_t>(signed16);
    }
    if (!reader.ReadU8(message.movementFlags)) {
        return std::nullopt;
    }
    for (int16_t& value : message.upperLimbRotation) {
        if (!reader.ReadU16(signed16)) {
            return std::nullopt;
        }
        value = static_cast<int16_t>(signed16);
    }
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.boots = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.shield = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.tunic = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(message.currentMask) || !reader.ReadU32(message.stateFlags1) ||
        !reader.ReadU32(message.stateFlags2) ||
        !reader.ReadU8(message.buttonItem) || !reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.itemAction = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.heldItemAction = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(message.modelGroup) || !reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.invincibilityTimer = static_cast<int8_t>(signed8);
    if (!reader.ReadU16(signed16)) {
        return std::nullopt;
    }
    message.modelState = static_cast<int16_t>(signed16);
    if (!reader.ReadF32(message.modelBlend) || !reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.actionVariable = static_cast<int8_t>(signed8);
    if (!reader.ReadF32(message.linearVelocity) || !reader.ReadU16(signed16)) {
        return std::nullopt;
    }
    message.focusActorId = static_cast<int16_t>(signed16);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.meleeWeaponState = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.meleeWeaponAnimation = static_cast<int8_t>(signed8);
    return reader.AtEnd() ? std::optional<PlayerSnapshotMessage>(message) : std::nullopt;
}

std::vector<uint8_t> EncodePlayerPresentation(const PlayerPresentationMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU32(message.revision);
    writer.WriteU8(static_cast<uint8_t>(message.boots));
    writer.WriteU8(static_cast<uint8_t>(message.shield));
    writer.WriteU8(static_cast<uint8_t>(message.tunic));
    writer.WriteU8(message.currentMask);
    writer.WriteU8(message.buttonItem);
    writer.WriteU8(static_cast<uint8_t>(message.itemAction));
    writer.WriteU8(static_cast<uint8_t>(message.heldItemAction));
    writer.WriteU8(message.modelGroup);
    return writer.Data();
}

std::optional<PlayerPresentationMessage> DecodePlayerPresentation(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    PlayerPresentationMessage message;
    uint8_t signed8 = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU32(message.revision) || !reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.boots = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.shield = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.tunic = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(message.currentMask) || !reader.ReadU8(message.buttonItem) || !reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.itemAction = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(signed8)) {
        return std::nullopt;
    }
    message.heldItemAction = static_cast<int8_t>(signed8);
    if (!reader.ReadU8(message.modelGroup) || !reader.AtEnd()) {
        return std::nullopt;
    }
    return message;
}

std::vector<uint8_t> EncodeCycleSnapshot(const CycleSnapshotMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU32(message.hostTick);
    writer.WriteU32(message.cycleGeneration);
    writer.WriteU32(static_cast<uint32_t>(message.day));
    writer.WriteU16(message.time);
    writer.WriteU16(message.skyboxTime);
    writer.WriteU32(static_cast<uint32_t>(message.timeSpeedOffset));
    writer.WriteU16(message.sceneTimeSpeed);
    writer.WriteU32(static_cast<uint32_t>(message.isNight));
    writer.WriteU8(message.weatherMode);
    writer.WriteU8(message.pendingTransition);
    return writer.Data();
}

std::optional<CycleSnapshotMessage> DecodeCycleSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    CycleSnapshotMessage message;
    uint32_t signedValue = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU32(message.hostTick) ||
        !reader.ReadU32(message.cycleGeneration) ||
        !reader.ReadU32(signedValue)) {
        return std::nullopt;
    }
    message.day = static_cast<int32_t>(signedValue);
    if (!reader.ReadU16(message.time) || !reader.ReadU16(message.skyboxTime) || !reader.ReadU32(signedValue)) {
        return std::nullopt;
    }
    message.timeSpeedOffset = static_cast<int32_t>(signedValue);
    if (!reader.ReadU16(message.sceneTimeSpeed) || !reader.ReadU32(signedValue)) {
        return std::nullopt;
    }
    message.isNight = static_cast<int32_t>(signedValue);
    if (!reader.ReadU8(message.weatherMode) || !reader.ReadU8(message.pendingTransition) || !reader.AtEnd()) {
        return std::nullopt;
    }
    return message;
}

std::vector<uint8_t> EncodeSnapshotRequest(const SnapshotRequestMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU16(static_cast<uint16_t>(message.room));
    return writer.Data();
}

std::optional<SnapshotRequestMessage> DecodeSnapshotRequest(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    SnapshotRequestMessage message;
    uint16_t signedValue = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.room = static_cast<int16_t>(signedValue);
    if (!reader.AtEnd()) {
        return std::nullopt;
    }
    return message;
}

std::vector<uint8_t> EncodeSceneFlagIntent(const SceneFlagIntentMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.participantId);
    writer.WriteU64(message.requestId);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU16(static_cast<uint16_t>(message.flagType));
    writer.WriteU16(static_cast<uint16_t>(message.flag));
    writer.WriteU8(message.set ? 1 : 0);
    return writer.Data();
}

std::optional<SceneFlagIntentMessage> DecodeSceneFlagIntent(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    SceneFlagIntentMessage message;
    uint16_t signedValue = 0;
    uint8_t set = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.participantId) ||
        !reader.ReadU64(message.requestId) || !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.flagType = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.flag = static_cast<int16_t>(signedValue);
    if (!reader.ReadU8(set) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.set = set != 0;
    return message;
}

std::vector<uint8_t> EncodeSceneFlagsSnapshot(const SceneFlagsSnapshotMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.revision);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU32(message.chest);
    writer.WriteU32(message.switches);
    writer.WriteU32(message.clear);
    writer.WriteU32(message.collectible);
    return writer.Data();
}

std::optional<SceneFlagsSnapshotMessage> DecodeSceneFlagsSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    SceneFlagsSnapshotMessage message;
    uint16_t scene = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.revision) || !reader.ReadU16(scene) ||
        !reader.ReadU32(message.chest) ||
        !reader.ReadU32(message.switches) || !reader.ReadU32(message.clear) ||
        !reader.ReadU32(message.collectible) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(scene);
    return message;
}

std::vector<uint8_t> EncodeActorSnapshot(const ActorSnapshotMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU32(message.hostTick);
    writer.WriteU64(message.entityId);
    writer.WriteU64(message.acknowledgedRequestId);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU16(static_cast<uint16_t>(message.room));
    writer.WriteU16(static_cast<uint16_t>(message.actorId));
    writer.WriteU16(static_cast<uint16_t>(message.params));
    for (float value : message.homePosition) {
        writer.WriteF32(value);
    }
    for (float value : message.position) {
        writer.WriteF32(value);
    }
    for (float value : message.velocity) {
        writer.WriteF32(value);
    }
    for (float value : message.scale) {
        writer.WriteF32(value);
    }
    for (int16_t value : message.worldRotation) {
        writer.WriteU16(static_cast<uint16_t>(value));
    }
    for (int16_t value : message.shapeRotation) {
        writer.WriteU16(static_cast<uint16_t>(value));
    }
    writer.WriteF32(message.speed);
    writer.WriteF32(message.gravity);
    writer.WriteU16(static_cast<uint16_t>(message.health));
    writer.WriteU16(message.stateId);
    writer.WriteF32(message.animationFrame);
    writer.WriteF32(message.animationSpeed);
    writer.WriteU8(message.alive ? 1 : 0);
    const uint8_t wordCount = std::min<uint8_t>(message.adapterWordCount, kMaximumActorAdapterWords);
    writer.WriteU8(wordCount);
    for (uint8_t i = 0; i < wordCount; ++i) {
        writer.WriteU16(static_cast<uint16_t>(message.adapterState[i]));
    }
    return writer.Data();
}

std::optional<ActorSnapshotMessage> DecodeActorSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    ActorSnapshotMessage message;
    uint16_t signedValue = 0;
    uint8_t alive = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU32(message.hostTick) ||
        !reader.ReadU64(message.entityId) || !reader.ReadU64(message.acknowledgedRequestId) ||
        !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.room = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.actorId = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.params = static_cast<int16_t>(signedValue);
    for (float& value : message.homePosition) {
        if (!reader.ReadF32(value)) {
            return std::nullopt;
        }
    }
    for (float& value : message.position) {
        if (!reader.ReadF32(value)) {
            return std::nullopt;
        }
    }
    for (float& value : message.velocity) {
        if (!reader.ReadF32(value)) {
            return std::nullopt;
        }
    }
    for (float& value : message.scale) {
        if (!reader.ReadF32(value)) {
            return std::nullopt;
        }
    }
    for (int16_t& value : message.worldRotation) {
        if (!reader.ReadU16(signedValue)) {
            return std::nullopt;
        }
        value = static_cast<int16_t>(signedValue);
    }
    for (int16_t& value : message.shapeRotation) {
        if (!reader.ReadU16(signedValue)) {
            return std::nullopt;
        }
        value = static_cast<int16_t>(signedValue);
    }
    if (!reader.ReadF32(message.speed) || !reader.ReadF32(message.gravity) || !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.health = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(message.stateId) || !reader.ReadF32(message.animationFrame) ||
        !reader.ReadF32(message.animationSpeed) || !reader.ReadU8(alive) ||
        !reader.ReadU8(message.adapterWordCount) || message.adapterWordCount > kMaximumActorAdapterWords) {
        return std::nullopt;
    }
    message.alive = alive != 0;
    for (uint8_t i = 0; i < message.adapterWordCount; ++i) {
        if (!reader.ReadU16(signedValue)) {
            return std::nullopt;
        }
        message.adapterState[i] = static_cast<int16_t>(signedValue);
    }
    return reader.AtEnd() ? std::optional<ActorSnapshotMessage>(message) : std::nullopt;
}

std::vector<uint8_t> EncodeBarrierSnapshot(const BarrierSnapshotMessage& message) {
    ByteWriter writer;
    const BarrierState& state = message.state;
    writer.WriteU64(state.operationEpoch);
    WriteScope(writer, state.scope);
    writer.WriteU8(static_cast<uint8_t>(state.kind));
    writer.WriteU8(static_cast<uint8_t>(state.phase));
    writer.WriteString(state.manifestHash);
    writer.WriteU16(static_cast<uint16_t>(state.targetScene));
    writer.WriteU16(static_cast<uint16_t>(state.targetRoom));
    writer.WriteU32(static_cast<uint32_t>(state.targetEntrance));
    writer.WriteU64(state.deadlineTick);
    writer.WriteU8(static_cast<uint8_t>(std::min<size_t>(state.participants.size(), 64)));
    for (size_t index = 0; index < state.participants.size() && index < 64; ++index) {
        writer.WriteU64(state.participants[index]);
    }
    writer.WriteU8(static_cast<uint8_t>(std::min<size_t>(state.readyParticipants.size(), 64)));
    for (size_t index = 0; index < state.readyParticipants.size() && index < 64; ++index) {
        writer.WriteU64(state.readyParticipants[index]);
    }
    return writer.Data();
}

std::optional<BarrierSnapshotMessage> DecodeBarrierSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    BarrierSnapshotMessage message;
    uint8_t kind = 0;
    uint8_t phase = 0;
    uint16_t signedValue = 0;
    uint8_t count = 0;
    if (!reader.ReadU64(message.state.operationEpoch) || !ReadScope(reader, message.state.scope) ||
        !reader.ReadU8(kind) || kind > static_cast<uint8_t>(BarrierKind::CycleTransition) ||
        !reader.ReadU8(phase) || phase > static_cast<uint8_t>(BarrierPhase::Aborted) ||
        !reader.ReadString(message.state.manifestHash) || message.state.manifestHash.size() > 128 ||
        !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.state.kind = static_cast<BarrierKind>(kind);
    message.state.phase = static_cast<BarrierPhase>(phase);
    message.state.targetScene = static_cast<int16_t>(signedValue);
    uint32_t targetEntrance = 0;
    if (!reader.ReadU16(signedValue) || !reader.ReadU32(targetEntrance) ||
        !reader.ReadU64(message.state.deadlineTick) || !reader.ReadU8(count) || count > 64) {
        return std::nullopt;
    }
    message.state.targetRoom = static_cast<int16_t>(signedValue);
    message.state.targetEntrance = static_cast<int32_t>(targetEntrance);
    message.state.participants.resize(count);
    for (uint64_t& participantId : message.state.participants) {
        if (!reader.ReadU64(participantId) || participantId == 0) {
            return std::nullopt;
        }
    }
    if (!reader.ReadU8(count) || count > 64) {
        return std::nullopt;
    }
    message.state.readyParticipants.resize(count);
    for (uint64_t& participantId : message.state.readyParticipants) {
        if (!reader.ReadU64(participantId) || participantId == 0) {
            return std::nullopt;
        }
    }
    return reader.AtEnd() ? std::optional<BarrierSnapshotMessage>(message) : std::nullopt;
}

std::vector<uint8_t> EncodeBarrierReady(const BarrierReadyMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.operationEpoch);
    writer.WriteU64(message.participantId);
    writer.WriteU16(static_cast<uint16_t>(message.currentScene));
    writer.WriteU16(static_cast<uint16_t>(message.currentRoom));
    return writer.Data();
}

std::optional<BarrierReadyMessage> DecodeBarrierReady(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    BarrierReadyMessage message;
    uint16_t signedValue = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.operationEpoch) ||
        !reader.ReadU64(message.participantId) || !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.currentScene = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.currentRoom = static_cast<int16_t>(signedValue);
    return message;
}

std::vector<uint8_t> EncodeAttackIntent(const AttackIntentMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.participantId);
    writer.WriteU64(message.requestId);
    writer.WriteU64(message.entityId);
    writer.WriteU32(message.playerTick);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU8(message.attackKind);
    return writer.Data();
}

std::optional<AttackIntentMessage> DecodeAttackIntent(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    AttackIntentMessage message;
    uint16_t scene = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.participantId) ||
        !reader.ReadU64(message.requestId) || !reader.ReadU64(message.entityId) ||
        !reader.ReadU32(message.playerTick) || !reader.ReadU16(scene) || !reader.ReadU8(message.attackKind) ||
        !reader.AtEnd()) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(scene);
    return message;
}

std::vector<uint8_t> EncodeCollectibleIntent(const CollectibleIntentMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.participantId);
    writer.WriteU64(message.requestId);
    writer.WriteU64(message.locationId);
    writer.WriteU16(static_cast<uint16_t>(message.scene));
    writer.WriteU16(static_cast<uint16_t>(message.flagType));
    writer.WriteU16(static_cast<uint16_t>(message.flag));
    return writer.Data();
}

std::optional<CollectibleIntentMessage> DecodeCollectibleIntent(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    CollectibleIntentMessage message;
    uint16_t signedValue = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.participantId) ||
        !reader.ReadU64(message.requestId) || !reader.ReadU64(message.locationId) ||
        !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.scene = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.flagType = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue) || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.flag = static_cast<int16_t>(signedValue);
    return message;
}

std::vector<uint8_t> EncodeProgressionSnapshot(const ProgressionSnapshotMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.revision);
    const uint16_t count = static_cast<uint16_t>(std::min<size_t>(message.locations.size(), 4096));
    writer.WriteU16(count);
    for (uint16_t index = 0; index < count; ++index) {
        const CollectedLocation& location = message.locations[index];
        writer.WriteU64(location.locationId);
        writer.WriteU16(static_cast<uint16_t>(location.scene));
        writer.WriteU16(static_cast<uint16_t>(location.flagType));
        writer.WriteU16(static_cast<uint16_t>(location.flag));
    }
    for (uint8_t item : message.shared.durableItems) {
        writer.WriteU8(item);
    }
    for (uint8_t item : message.shared.tradeItems) {
        writer.WriteU8(item);
    }
    writer.WriteU8(message.shared.bottleOwnershipMask);
    writer.WriteU16(message.shared.equipment);
    writer.WriteU32(message.shared.upgrades);
    writer.WriteU32(message.shared.questItems);
    for (uint8_t item : message.shared.dungeonItems) {
        writer.WriteU8(item);
    }
    for (int8_t keys : message.shared.dungeonKeys) {
        writer.WriteU8(static_cast<uint8_t>(keys));
    }
    writer.WriteU16(static_cast<uint16_t>(message.shared.healthCapacity));
    writer.WriteU8(message.shared.linkAge);
    writer.WriteU8(message.shared.magicLevel);
    writer.WriteU8(message.shared.isMagicAcquired);
    writer.WriteU8(message.shared.isDoubleMagicAcquired);
    writer.WriteU8(message.shared.isDoubleDefenseAcquired);
    writer.WriteU8(message.shared.bgsFlag);
    writer.WriteU16(static_cast<uint16_t>(message.shared.gsTokens));
    for (uint16_t flags : message.shared.eventChkInf) {
        writer.WriteU16(flags);
    }
    return writer.Data();
}

std::optional<ProgressionSnapshotMessage> DecodeProgressionSnapshot(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    ProgressionSnapshotMessage message;
    uint16_t count = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.revision) || !reader.ReadU16(count) ||
        count > 4096) {
        return std::nullopt;
    }
    message.locations.resize(count);
    for (CollectedLocation& location : message.locations) {
        uint16_t signedValue = 0;
        if (!reader.ReadU64(location.locationId) || location.locationId == 0 || !reader.ReadU16(signedValue)) {
            return std::nullopt;
        }
        location.scene = static_cast<int16_t>(signedValue);
        if (!reader.ReadU16(signedValue)) {
            return std::nullopt;
        }
        location.flagType = static_cast<int16_t>(signedValue);
        if (!reader.ReadU16(signedValue)) {
            return std::nullopt;
        }
        location.flag = static_cast<int16_t>(signedValue);
    }
    for (uint8_t& item : message.shared.durableItems) {
        if (!reader.ReadU8(item)) {
            return std::nullopt;
        }
    }
    for (uint8_t& item : message.shared.tradeItems) {
        if (!reader.ReadU8(item)) {
            return std::nullopt;
        }
    }
    if (!reader.ReadU8(message.shared.bottleOwnershipMask) || !reader.ReadU16(message.shared.equipment) ||
        !reader.ReadU32(message.shared.upgrades) || !reader.ReadU32(message.shared.questItems)) {
        return std::nullopt;
    }
    for (uint8_t& item : message.shared.dungeonItems) {
        if (!reader.ReadU8(item)) {
            return std::nullopt;
        }
    }
    for (int8_t& keys : message.shared.dungeonKeys) {
        uint8_t encoded = 0;
        if (!reader.ReadU8(encoded)) {
            return std::nullopt;
        }
        keys = static_cast<int8_t>(encoded);
    }
    uint16_t signedValue = 0;
    if (!reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.shared.healthCapacity = static_cast<int16_t>(signedValue);
    if (!reader.ReadU8(message.shared.linkAge) || !reader.ReadU8(message.shared.magicLevel) ||
        !reader.ReadU8(message.shared.isMagicAcquired) ||
        !reader.ReadU8(message.shared.isDoubleMagicAcquired) ||
        !reader.ReadU8(message.shared.isDoubleDefenseAcquired) || !reader.ReadU8(message.shared.bgsFlag) ||
        !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.shared.gsTokens = static_cast<int16_t>(signedValue);
    for (uint16_t& flags : message.shared.eventChkInf) {
        if (!reader.ReadU16(flags)) {
            return std::nullopt;
        }
    }
    return reader.AtEnd() ? std::optional<ProgressionSnapshotMessage>(message) : std::nullopt;
}

std::vector<uint8_t> EncodeProgressionIntent(const ProgressionIntentMessage& message) {
    ByteWriter writer;
    WriteScope(writer, message.scope);
    writer.WriteU64(message.participantId);
    writer.WriteU64(message.requestId);
    writer.WriteU8(static_cast<uint8_t>(message.kind));
    writer.WriteU16(message.itemId);
    writer.WriteU16(message.modIndex);
    writer.WriteU16(message.mapIndex);
    writer.WriteU8(static_cast<uint8_t>(message.remainingDungeonKeys));
    writer.WriteU16(static_cast<uint16_t>(message.flagType));
    writer.WriteU16(static_cast<uint16_t>(message.flag));
    writer.WriteU8(message.set ? 1 : 0);
    return writer.Data();
}

std::optional<ProgressionIntentMessage> DecodeProgressionIntent(const std::vector<uint8_t>& payload) {
    ByteReader reader(payload);
    ProgressionIntentMessage message;
    uint8_t kind = 0;
    uint8_t remainingKeys = 0;
    uint16_t signedValue = 0;
    uint8_t set = 0;
    if (!ReadScope(reader, message.scope) || !reader.ReadU64(message.participantId) ||
        !reader.ReadU64(message.requestId) || !reader.ReadU8(kind) || !reader.ReadU16(message.itemId) ||
        !reader.ReadU16(message.modIndex) || !reader.ReadU16(message.mapIndex) ||
        !reader.ReadU8(remainingKeys) || !reader.ReadU16(signedValue)) {
        return std::nullopt;
    }
    message.flagType = static_cast<int16_t>(signedValue);
    if (!reader.ReadU16(signedValue) || !reader.ReadU8(set) || set > 1 || !reader.AtEnd()) {
        return std::nullopt;
    }
    message.flag = static_cast<int16_t>(signedValue);
    message.set = set != 0;
    if (kind < static_cast<uint8_t>(ProgressionIntentKind::ItemReceived) ||
        kind > static_cast<uint8_t>(ProgressionIntentKind::GlobalFlagChanged)) {
        return std::nullopt;
    }
    message.kind = static_cast<ProgressionIntentKind>(kind);
    message.remainingDungeonKeys = static_cast<int8_t>(remainingKeys);
    return message;
}

} // namespace HyruleCoop
