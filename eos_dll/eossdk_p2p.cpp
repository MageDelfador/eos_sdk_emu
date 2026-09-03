/*
 * Copyright (C) 2020 Nemirtingas
 * This file is part of the Nemirtingas's Epic Emulator
 *
 * The Nemirtingas's Epic Emulator is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * The Nemirtingas's Epic Emulator is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the Nemirtingas's Epic Emulator; if not, see
 * <http://www.gnu.org/licenses/>.
 */

#include "eossdk_p2p.h"
#include "eossdk_platform.h"
#include "eos_client_api.h"
#include "settings.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace sdk
{
namespace
{
struct cached_p2p_established_t
{
    EOS_ProductUserId remote_id{};
    std::string socket_name;
    EOS_EConnectionEstablishedType connection_type{ EOS_EConnectionEstablishedType::EOS_CET_NewConnection };
};

static std::unique_ptr<cached_p2p_established_t> g_cached_p2p_established;

static void fill_peer_connection_established_info(
    EOS_P2P_OnPeerConnectionEstablishedInfo& opcei,
    EOS_ProductUserId remote_id,
    std::string const& socket_name,
    EOS_EConnectionEstablishedType connection_type)
{
    opcei.ConnectionType = connection_type;
    opcei.RemoteUserId = remote_id;
    EOS_P2P_SocketId* socket_id = new EOS_P2P_SocketId;
    socket_id->ApiVersion = EOS_P2P_SOCKETID_API_LATEST;
    strncpy(const_cast<char*>(socket_id->SocketName),
        socket_name.c_str(),
        sizeof(EOS_P2P_SocketId::SocketName) - 1);
    socket_id->SocketName[sizeof(EOS_P2P_SocketId::SocketName) - 1] = '\0';
    opcei.SocketId = socket_id;
}

static void queue_peer_connection_established(EOSSDK_P2P* self, pFrameResult_t notif, cached_p2p_established_t const& cached)
{
    EOS_P2P_OnPeerConnectionEstablishedInfo& opcei =
        notif->GetCallback<EOS_P2P_OnPeerConnectionEstablishedInfo>();
    fill_peer_connection_established_info(opcei, cached.remote_id, cached.socket_name, cached.connection_type);
    notif->done = true;
    GetCB_Manager().add_callback(self, notif);
}
}

namespace
{
enum class ue_handshake_packet_type_e : uint8_t
{
    Initial = 0,
    Challenge = 1,
    Response = 2,
    Ack = 3,
};

struct ue_parsed_handshake_t
{
    uint8_t travel_count = 0;
    uint8_t client_id = 0;
    bool restarted = false;
    uint8_t min_supported_version = 0;
    uint8_t handshake_version = 0;
    ue_handshake_packet_type_e packet_type = ue_handshake_packet_type_e::Initial;
    uint8_t sent_count = 0;
    uint32_t local_network_version = 0;
    uint16_t runtime_features = 0;
    uint8_t secret_id = 0;
    double timestamp = 0.0;
    uint8_t cookie[20]{};
    size_t padding_bits = 0;
};

struct ue_pending_challenge_t
{
    double timestamp = 0.0;
    uint8_t secret_id = 0;
    uint8_t cookie[20]{};
    uint8_t assigned_client_id = 1;
};

constexpr size_t kHandshakeCookieBytes = 20;
constexpr uint8_t kHandshakeVersionRandomized = 1;
constexpr uint8_t kHandshakeVersionNetCL = 2;

class ue_bit_reader_t
{
public:
    explicit ue_bit_reader_t(std::string const& data)
        : _data(reinterpret_cast<uint8_t const*>(data.data()))
        , _num_bits(data.size() * 8)
    {
    }

    bool read_bit()
    {
        if (_pos >= _num_bits)
            return false;
        bool const bit = (_data[_pos >> 3] >> (_pos & 7)) & 1;
        ++_pos;
        return bit;
    }

    bool read_bits_to_bytes(uint8_t* out, size_t bit_count)
    {
        std::memset(out, 0, (bit_count + 7) / 8);
        for (size_t i = 0; i < bit_count; ++i)
        {
            if (!read_bit())
                return false;
            if ((_data[(_pos - 1) >> 3] >> ((_pos - 1) & 7)) & 1)
                out[i >> 3] |= static_cast<uint8_t>(1u << (i & 7));
        }
        return true;
    }

    bool read_double(double& out)
    {
        uint8_t raw[8]{};
        if (!read_bits_to_bytes(raw, 64))
            return false;
        std::memcpy(&out, raw, sizeof(out));
        return true;
    }

    bool read_serialize_int(uint32_t value_max, uint32_t& out)
    {
        if (value_max < 2)
            return false;

        out = 0;
        uint32_t mask = 1;
        while ((out + mask) < value_max)
        {
            if (_pos >= _num_bits)
                return false;
            if (read_bit())
                out |= mask;
            mask <<= 1;
        }
        return true;
    }

    size_t pos_bits() const { return _pos; }
    size_t num_bits() const { return _num_bits; }

private:
    uint8_t const* _data = nullptr;
    size_t _num_bits = 0;
    size_t _pos = 0;
};

class ue_bit_writer_t
{
public:
    void write_bit(bool value)
    {
        size_t const byte_index = _num_bits >> 3;
        if (byte_index >= _data.size())
            _data.push_back(0);

        if (value)
            _data[byte_index] |= static_cast<uint8_t>(1u << (_num_bits & 7));
        ++_num_bits;
    }

    void write_bits_from_bytes(uint8_t const* src, size_t bit_count)
    {
        for (size_t i = 0; i < bit_count; ++i)
            write_bit((src[i >> 3] >> (i & 7)) & 1);
    }

    void write_double(double value)
    {
        uint8_t raw[8]{};
        std::memcpy(raw, &value, sizeof(raw));
        write_bits_from_bytes(raw, 64);
    }

    void write_bytes(uint8_t const* src, size_t byte_count)
    {
        write_bits_from_bytes(src, byte_count * 8);
    }

    void write_serialize_int(uint32_t value, uint32_t value_max)
    {
        if (value_max < 2)
            return;

        value = std::min(value, value_max - 1);
        uint32_t new_value = 0;
        uint32_t mask = 1;
        while ((new_value + mask) < value_max && mask != 0)
        {
            write_bit((value & mask) != 0);
            new_value |= mask;
            mask <<= 1;
        }
    }

    std::string finish() const
    {
        return std::string(reinterpret_cast<char const*>(_data.data()), (_num_bits + 7) / 8);
    }

private:
    std::vector<uint8_t> _data;
    size_t _num_bits = 0;
};

void fill_random_bytes(uint8_t* buf, size_t len)
{
    static thread_local std::mt19937 gen{ std::random_device{}() };
    std::uniform_int_distribution<int> dis(0, 255);
    for (size_t i = 0; i < len; ++i)
        buf[i] = static_cast<uint8_t>(dis(gen));
}

void begin_ue_handshake(ue_bit_writer_t& writer,
    ue_handshake_packet_type_e packet_type,
    ue_parsed_handshake_t const& parsed,
    uint8_t sent_count)
{
    writer.write_serialize_int(parsed.travel_count, 4);
    writer.write_serialize_int(parsed.client_id, 8);
    writer.write_bit(true);
    writer.write_bit(parsed.restarted);

    if (parsed.handshake_version >= kHandshakeVersionRandomized)
    {
        uint8_t header[4] = {
            parsed.min_supported_version,
            parsed.handshake_version,
            static_cast<uint8_t>(packet_type),
            sent_count,
        };
        writer.write_bytes(header, sizeof(header));
    }

    if (parsed.handshake_version >= kHandshakeVersionNetCL)
    {
        uint8_t net_ver[4]{};
        std::memcpy(net_ver, &parsed.local_network_version, sizeof(net_ver));
        writer.write_bits_from_bytes(net_ver, 32);

        uint8_t runtime[2]{};
        std::memcpy(runtime, &parsed.runtime_features, sizeof(runtime));
        writer.write_bits_from_bytes(runtime, 16);
    }
}

void cap_ue_handshake(ue_bit_writer_t& writer, ue_parsed_handshake_t const& parsed)
{
    if (parsed.handshake_version != 0 && parsed.padding_bits >= 8)
    {
        size_t const pad_bytes = parsed.padding_bits / 8;
        std::vector<uint8_t> padding(pad_bytes);
        fill_random_bytes(padding.data(), padding.size());
        writer.write_bytes(padding.data(), padding.size());
    }
    writer.write_bit(true);
}

std::string format_hex_dump(std::string const& data, size_t max_bytes = 64)
{
    size_t const limit = std::min(data.size(), max_bytes);
    std::string out;
    out.reserve(limit * 3 + 16);
    char tmp[4]{};
    for (size_t i = 0; i < limit; ++i)
    {
        std::snprintf(tmp, sizeof(tmp), "%02x", static_cast<unsigned char>(data[i]));
        if (i != 0)
            out.push_back(' ');
        out.append(tmp);
    }
    if (data.size() > limit)
        out += " ...";
    return out;
}

bool parse_ue_stateless_handshake(std::string const& packet, ue_parsed_handshake_t& out)
{
    if (packet.size() < 16)
        return false;

    ue_bit_reader_t reader(packet);

    uint32_t travel_count = 0;
    uint32_t client_id = 0;
    if (!reader.read_serialize_int(4, travel_count))
        return false;
    if (!reader.read_serialize_int(8, client_id))
        return false;
    out.travel_count = static_cast<uint8_t>(travel_count);
    out.client_id = static_cast<uint8_t>(client_id);
    if (!reader.read_bit())
        return false;

    out.restarted = reader.read_bit();

    uint8_t header[4]{};
    if (!reader.read_bits_to_bytes(header, 32))
        return false;

    out.min_supported_version = header[0];
    out.handshake_version = header[1];
    out.packet_type = static_cast<ue_handshake_packet_type_e>(header[2]);
    out.sent_count = header[3];

    if (out.handshake_version >= kHandshakeVersionNetCL)
    {
        uint8_t net_ver[4]{};
        uint8_t runtime[2]{};
        if (!reader.read_bits_to_bytes(net_ver, 32))
            return false;
        if (!reader.read_bits_to_bytes(runtime, 16))
            return false;
        std::memcpy(&out.local_network_version, net_ver, sizeof(out.local_network_version));
        std::memcpy(&out.runtime_features, runtime, sizeof(out.runtime_features));
    }

    out.secret_id = reader.read_bit() ? 1 : 0;
    if (!reader.read_double(out.timestamp))
        return false;

    if (!reader.read_bits_to_bytes(out.cookie, kHandshakeCookieBytes * 8))
        return false;

    size_t const after_cookie = reader.pos_bits();
    size_t const remaining = reader.num_bits() - after_cookie;
    if (remaining < 1)
        return false;

    out.padding_bits = remaining - 1;
    return true;
}

std::optional<std::string> build_ue_challenge_from_initial(std::string const& initial,
    ue_pending_challenge_t& pending)
{
    ue_parsed_handshake_t parsed{};
    if (!parse_ue_stateless_handshake(initial, parsed))
        return std::nullopt;

    if (parsed.packet_type != ue_handshake_packet_type_e::Initial)
        return std::nullopt;

    if (parsed.padding_bits < 8)
        parsed.padding_bits = 64;

    fill_random_bytes(pending.cookie, sizeof(pending.cookie));
    pending.secret_id = 1;
    pending.timestamp = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    pending.assigned_client_id = static_cast<uint8_t>((parsed.client_id % 7) + 1);

    ue_bit_writer_t writer;
    ue_parsed_handshake_t challenge_header = parsed;
    challenge_header.packet_type = ue_handshake_packet_type_e::Challenge;
    begin_ue_handshake(writer, ue_handshake_packet_type_e::Challenge, challenge_header, 0);

    writer.write_bit(pending.secret_id != 0);
    writer.write_double(pending.timestamp);
    writer.write_bytes(pending.cookie, sizeof(pending.cookie));
    cap_ue_handshake(writer, parsed);

    return writer.finish();
}

std::optional<std::string> build_ue_ack_from_response(std::string const& response,
    ue_parsed_handshake_t const& challenge_state,
    ue_pending_challenge_t const& pending)
{
    ue_parsed_handshake_t parsed{};
    if (!parse_ue_stateless_handshake(response, parsed))
        return std::nullopt;

    if (parsed.packet_type != ue_handshake_packet_type_e::Response)
        return std::nullopt;

    if (parsed.secret_id != pending.secret_id)
        return std::nullopt;

    if (parsed.timestamp != pending.timestamp)
        return std::nullopt;

    if (std::memcmp(parsed.cookie, pending.cookie, sizeof(parsed.cookie)) != 0)
        return std::nullopt;

    ue_bit_writer_t writer;
    ue_parsed_handshake_t ack_header = challenge_state;
    ack_header.packet_type = ue_handshake_packet_type_e::Ack;
    ack_header.client_id = pending.assigned_client_id;
    begin_ue_handshake(writer, ue_handshake_packet_type_e::Ack, ack_header, 1);

    writer.write_bit(true);
    writer.write_double(pending.timestamp);

    uint8_t ack_cookie[kHandshakeCookieBytes]{};
    fill_random_bytes(ack_cookie, sizeof(ack_cookie));
    writer.write_bytes(ack_cookie, sizeof(ack_cookie));
    cap_ue_handshake(writer, challenge_state);

    return writer.finish();
}

struct p2p_peer_travel_stats_t
{
    uint32_t sends = 0;
    uint32_t recvs = 0;
    uint32_t acks = 0;
    bool logged_first_recv = false;
    bool sent_travel_handshake = false;
    bool recv_travel_handshake = false;
    bool recv_game_reply_after_handshake = false;
    bool host_pending_travel_reply = false;
    bool host_sent_travel_reply = false;
    bool host_pending_travel_ack = false;
    bool host_sent_travel_ack = false;
    std::string travel_initial_packet;
    std::string travel_response_packet;
    ue_pending_challenge_t pending_challenge{};
    std::chrono::steady_clock::time_point travel_handshake_sent{};
    std::chrono::steady_clock::time_point host_travel_handshake_received{};
    std::chrono::steady_clock::time_point host_travel_response_received{};
};

std::unordered_map<std::string, p2p_peer_travel_stats_t> g_p2p_peer_travel_stats;

p2p_peer_travel_stats_t& peer_travel_stats(std::string const& peer)
{
    return g_p2p_peer_travel_stats[peer];
}

bool should_mirror_game_p2p_for_peer(std::string const& peer_id)
{
    if (peer_id.empty())
        return false;

    if (GetEOS_Sessions().local_user_hosts_session_for_peer(peer_id))
        return true;

    return GetEOS_Lobby().is_peer_member_of_my_owned_lobby(peer_id);
}

void log_p2p_peer_endpoint(char const* event, std::string const& peer)
{
    APP_LOG(Log::LogLevel::INFO, "NET %s peer=%s endpoint=%s",
        event,
        peer.c_str(),
        GetNetwork().format_peer_endpoint(peer).c_str());
}

void log_ue_handshake_packet(char const* event, std::string const& peer, std::string const& packet)
{
    ue_parsed_handshake_t parsed{};
    if (!parse_ue_stateless_handshake(packet, parsed))
    {
        APP_LOG(Log::LogLevel::INFO,
            "UE handshake %s peer=%s bytes=%zu hex=%s (parse failed)",
            event,
            peer.c_str(),
            packet.size(),
            format_hex_dump(packet).c_str());
        return;
    }

    APP_LOG(Log::LogLevel::INFO,
        "UE handshake %s peer=%s bytes=%zu type=%u travel=%u client=%u netver=0x%x secret=%u hex=%s",
        event,
        peer.c_str(),
        packet.size(),
        static_cast<unsigned>(parsed.packet_type),
        static_cast<unsigned>(parsed.travel_count),
        static_cast<unsigned>(parsed.client_id),
        parsed.local_network_version,
        static_cast<unsigned>(parsed.secret_id),
        format_hex_dump(packet).c_str());
}

void note_p2p_send(std::string const& peer, uint8_t channel, uint32_t bytes)
{
    auto& stats = peer_travel_stats(peer);
    ++stats.sends;
    if (channel == 172 && bytes == 73)
    {
        stats.sent_travel_handshake = true;
        stats.travel_handshake_sent = std::chrono::steady_clock::now();
        APP_LOG(Log::LogLevel::INFO,
            "P2P travel handshake SENT peer=%s endpoint=%s channel=172 bytes=73 (waiting UE net reply >5b)",
            peer.c_str(),
            GetNetwork().format_peer_endpoint(peer).c_str());
    }

    if (channel == 172 && bytes > 5 && should_mirror_game_p2p_for_peer(peer))
    {
        stats.host_sent_travel_reply = true;
        stats.host_pending_travel_reply = false;
        if (stats.host_pending_travel_ack)
        {
            stats.host_sent_travel_ack = true;
            stats.host_pending_travel_ack = false;
        }
    }
}

void note_p2p_recv(std::string const& peer, uint8_t channel, uint32_t bytes)
{
    auto& stats = peer_travel_stats(peer);
    ++stats.recvs;

    if (!stats.logged_first_recv)
    {
        stats.logged_first_recv = true;
        log_p2p_peer_endpoint("P2P first IP datagram received from", peer);
    }

    if (channel == 172 && bytes == 73)
    {
        stats.recv_travel_handshake = true;
        APP_LOG(Log::LogLevel::INFO,
            "P2P travel handshake RECEIVED peer=%s endpoint=%s channel=172 bytes=73",
            peer.c_str(),
            GetNetwork().format_peer_endpoint(peer).c_str());
    }

    if (stats.sent_travel_handshake && bytes > 5 &&
        (channel == 172 || channel == 255))
    {
        stats.recv_game_reply_after_handshake = true;
        APP_LOG(Log::LogLevel::INFO,
            "P2P travel reply RECEIVED peer=%s endpoint=%s channel=%u bytes=%u",
            peer.c_str(),
            GetNetwork().format_peer_endpoint(peer).c_str(),
            static_cast<unsigned>(channel),
            bytes);
    }
}

void log_p2p_close_summary(std::string const& peer)
{
    auto it = g_p2p_peer_travel_stats.find(peer);
    if (it == g_p2p_peer_travel_stats.end())
    {
        log_p2p_peer_endpoint("P2P CloseConnection (no traffic stats)", peer);
        return;
    }

    auto const& stats = it->second;
    APP_LOG(Log::LogLevel::INFO,
        "P2P CloseConnection summary peer=%s endpoint=%s sends=%u recvs=%u acks=%u "
        "travel_sent=%s travel_recv=%s ue_reply_after_travel=%s",
        peer.c_str(),
        GetNetwork().format_peer_endpoint(peer).c_str(),
        stats.sends,
        stats.recvs,
        stats.acks,
        stats.sent_travel_handshake ? "yes" : "no",
        stats.recv_travel_handshake ? "yes" : "no",
        stats.recv_game_reply_after_handshake ? "yes" : "NO");

    if (stats.sent_travel_handshake && !stats.recv_game_reply_after_handshake)
    {
        APP_LOG(Log::LogLevel::INFO,
            "P2P travel FAILED: IP transport OK (tcp/udp acks seen) but no UE game reply after 73-byte handshake from peer=%s",
            peer.c_str());
    }

    g_p2p_peer_travel_stats.erase(it);
}

void log_p2p_transport(Network::peer_t const& peerid, char const* kind, int channel, size_t bytes, bool ok, char const* via)
{
    static std::unordered_map<std::string, uint32_t> send_counts;
    uint32_t& count = send_counts[peerid + kind];
    ++count;

    if (std::strcmp(kind, "send") == 0)
        note_p2p_send(peerid, static_cast<uint8_t>(channel), static_cast<uint32_t>(bytes));
    else if (std::strcmp(kind, "ack") == 0)
        ++peer_travel_stats(peerid).acks;

    if (!ok || count <= 30 || (count % 100) == 0 || (bytes == 73 && channel == 172))
    {
        APP_LOG(Log::LogLevel::INFO, "P2P %s #%u peer=%s endpoint=%s channel=%d bytes=%zu via=%s ok=%s",
            kind,
            count,
            peerid.c_str(),
            GetNetwork().format_peer_endpoint(peerid).c_str(),
            channel,
            bytes,
            via,
            ok ? "yes" : "NO");
    }
}

void log_p2p_game_io(char const* direction, std::string const& peer, uint8_t channel, uint32_t bytes)
{
    static std::unordered_map<std::string, uint32_t> counts;

    bool const heartbeat = (bytes == 5 && (channel == 172 || channel == 255));

    std::string const key = std::string(direction) + peer + std::to_string(channel);
    uint32_t& count = counts[key];
    ++count;

    if (heartbeat)
    {
        if (count <= 2 || (count % 250) == 0)
        {
            APP_LOG(Log::LogLevel::DEBUG, "P2P %s heartbeat peer=%s channel=%u bytes=%u (#%u)",
                direction, peer.c_str(), static_cast<unsigned>(channel), bytes, count);
        }
        return;
    }

    APP_LOG(Log::LogLevel::INFO, "P2P %s peer=%s channel=%u bytes=%u endpoint=%s",
        direction, peer.c_str(), static_cast<unsigned>(channel), bytes,
        GetNetwork().format_peer_endpoint(peer).c_str());

    if (std::strcmp(direction, "recv") == 0)
        note_p2p_recv(peer, channel, bytes);
}

bool send_p2p_network_message(Network::peer_t const& peerid, Network_Message_pb& msg, char const* kind, int channel, size_t payload_bytes)
{
    bool const has_tcp = GetNetwork().has_tcp_peer(peerid);
    char const* via = has_tcp ? "TCP" : "UDP";
    bool res = GetNetwork().SendToPeer(msg);
    if (!res)
    {
        GetNetwork().ensure_udp_route(peerid);
        res = GetNetwork().SendToPeer(msg);
        if (res)
            via = "UDP";
    }

    log_p2p_transport(peerid, kind, channel, payload_bytes, res, via);
    return res;
}

}

decltype(EOSSDK_P2P::connecting_timeout) EOSSDK_P2P::connecting_timeout;
decltype(EOSSDK_P2P::connection_timeout) EOSSDK_P2P::connection_timeout;

EOSSDK_P2P::EOSSDK_P2P():
    next_requested_channel(-1),
    _relay_control(EOS_ERelayControl::EOS_RC_AllowRelays),
    _p2p_port(7777),
    _max_additional_ports_to_try(99)
{
    GetCB_Manager().register_frame(this);
    GetCB_Manager().register_callbacks(this);

    GetNetwork().register_listener(this, 0, Network_Message_pb::MessagesCase::kP2P);
}

EOSSDK_P2P::~EOSSDK_P2P()
{
    GetNetwork().unregister_listener(this, 0, Network_Message_pb::MessagesCase::kP2P);

    GetCB_Manager().unregister_callbacks(this);
    GetCB_Manager().unregister_frame(this);

    GetCB_Manager().remove_all_notifications(this);
}


void EOSSDK_P2P::set_p2p_state_connected(EOS_ProductUserId remote_id, p2p_state_t& state)
{
    p2p_state_t::status_e oldStatus = state.status;
    state.status = p2p_state_t::status_e::connected;
    GetNetwork().ensure_udp_route(remote_id->to_string());
    log_p2p_peer_endpoint("P2P connected", remote_id->to_string());
    APP_LOG(Log::LogLevel::INFO, "P2P connected to %s socket=%s",
        remote_id->to_string().c_str(), state.socket_name.c_str());
    for (auto& out_msgs : state.p2p_out_messages)
    {// Send all previously stored messages
        send_p2p_data(remote_id->to_string(), &out_msgs);
    }
    state.p2p_out_messages.clear();

    EOS_EConnectionEstablishedType const connection_type =
        oldStatus == p2p_state_t::status_e::connection_loss
            ? EOS_EConnectionEstablishedType::EOS_CET_Reconnection
            : EOS_EConnectionEstablishedType::EOS_CET_NewConnection;

    cached_p2p_established_t cached;
    cached.remote_id = remote_id;
    cached.socket_name = state.socket_name;
    cached.connection_type = connection_type;

    std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnPeerConnectionEstablishedInfo::k_iCallback));
    if (notifs.empty())
    {
        g_cached_p2p_established = std::make_unique<cached_p2p_established_t>(cached);
    }
    else
    {
        for (auto& notif : notifs)
            queue_peer_connection_established(this, notif, cached);
    }
}

void EOSSDK_P2P::ensure_peer_connection(std::string const& peer_id, std::string const& socket_name)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (peer_id.empty())
        return;

    EOS_ProductUserId remote_id = GetProductUserId(peer_id);
    if (remote_id == nullptr)
        return;

    p2p_state_t& state = _p2p_connections[remote_id];
    if (state.status == p2p_state_t::status_e::connected)
        return;

    if (!socket_name.empty())
        state.socket_name = socket_name;
    else if (state.socket_name.empty())
        state.socket_name = "EOSP2PTransport";

    if (state.status == p2p_state_t::status_e::requesting)
    {
        APP_LOG(Log::LogLevel::INFO, "Auto-accepting P2P connection from %s", peer_id.c_str());
        P2P_Connect_Response_pb* resp = new P2P_Connect_Response_pb;
        resp->set_accepted(true);
        send_p2p_connection_response(peer_id, resp);
        set_p2p_state_connected(remote_id, state);
        GetNetwork().ensure_udp_route(peer_id);
        return;
    }

    if (state.status == p2p_state_t::status_e::closed ||
        state.status == p2p_state_t::status_e::connecting)
    {
        APP_LOG(Log::LogLevel::INFO, "Initiating P2P connection to %s", peer_id.c_str());
        state.status = p2p_state_t::status_e::connecting;
        state.connection_loss_start = std::chrono::steady_clock::now();
        P2P_Connect_Request_pb* req = new P2P_Connect_Request_pb;
        req->set_socket_name(state.socket_name);
        send_p2p_connection_request(peer_id, req);
        GetNetwork().ensure_udp_route(peer_id);
    }
}

/**
 * P2P functions to help manage sending and receiving of messages to peers.
 *
 * These functions will attempt to punch through NATs, but will fallback to using Epic relay servers if a direct connection cannot be established.
 */

/**
 * Send a packet to a peer at the specified address. If there is already an open connection to this peer, it will be
 * sent immediately. If there is no open connection, an attempt to connect to the peer will be made. An EOS_Success
 * result only means the data was accepted to be sent, not that it has been successfully delivered to the peer.
 *
 * @param Options Information about the data being sent, by who, to who
 * @return EOS_EResult::EOS_Success           - If packet was queued to be sent successfully
 *         EOS_EResult::EOS_InvalidParameters - If input was invalid
 *         EOS_EResult::EOS_LimitExceeded     - If amount of data being sent is too large
 */
EOS_EResult EOSSDK_P2P::SendPacket(const EOS_P2P_SendPacketOptions* Options)
{
    //TRACE_FUNC();
    GLOBAL_LOCK();
    
    if (Options == nullptr || Options->RemoteUserId == nullptr || Options->Data == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Settings::Inst().steam_passthrough)
    {
        APP_LOG(Log::LogLevel::INFO, "P2P SendPacket ignored in steam_passthrough peer=%s channel=%u bytes=%u",
            Options->RemoteUserId->to_string().c_str(),
            static_cast<unsigned>(Options->Channel),
            Options->DataLengthBytes);
        return EOS_EResult::EOS_Success;
    }

    p2p_state_t& p2p_state = _p2p_connections[Options->RemoteUserId];
    P2P_Data_Message_pb data;

    switch (Options->ApiVersion)
    {
        case EOS_P2P_SENDPACKET_API_003:
        {
            const EOS_P2P_SendPacketOptions003* opts = reinterpret_cast<const EOS_P2P_SendPacketOptions003*>(Options);
        }
        case EOS_P2P_SENDPACKET_API_002:
        {
            const EOS_P2P_SendPacketOptions002* opts = reinterpret_cast<const EOS_P2P_SendPacketOptions002*>(Options);
        }
        case EOS_P2P_SENDPACKET_API_001:
        {
            const EOS_P2P_SendPacketOptions001* opts = reinterpret_cast<const EOS_P2P_SendPacketOptions001*>(Options);
            data.set_data(reinterpret_cast<const char*>(Options->Data), Options->DataLengthBytes);
            data.set_channel(Options->Channel);
            data.set_socket_name(Options->SocketId->SocketName);
            data.set_user_id(Options->LocalUserId->to_string());
        }
    }

    switch(p2p_state.status)
    {
        case p2p_state_t::status_e::requesting:
        {
            APP_LOG(Log::LogLevel::INFO, "Implicit P2P acceptation on send");
            // If we have been requested to connect, then its an implicit acceptation
            set_p2p_state_connected(Options->RemoteUserId, p2p_state);
        }

        case p2p_state_t::status_e::connected:
        {// We're connected, send the message now
            if (Options->Channel == 255 &&
                should_mirror_game_p2p_for_peer(Options->RemoteUserId->to_string()))
            {
                // Host UE net driver sends on ch255; joiner listens on ch172 only.
                P2P_Data_Message_pb mirror = data;
                mirror.set_channel(172);
                send_p2p_data(Options->RemoteUserId->to_string(), &mirror);
                log_p2p_game_io("send redirect ch255->ch172", Options->RemoteUserId->to_string(), 172,
                    Options->DataLengthBytes);
            }
            else
            {
                send_p2p_data(Options->RemoteUserId->to_string(), &data);
            }

            if (g_cached_p2p_established != nullptr)
            {
                set_p2p_state_connected(Options->RemoteUserId, p2p_state);
                g_cached_p2p_established.reset();
            }
        }
        break;

        case p2p_state_t::status_e::connection_loss:
        case p2p_state_t::status_e::connecting:
        {
            // Save the message for later
            p2p_state.p2p_out_messages.emplace_back(std::move(data));
        }
        break;

        case p2p_state_t::status_e::closed:
        {
            // Save the message for later
            p2p_state.p2p_out_messages.emplace_back(std::move(data));

            p2p_state.status = p2p_state_t::status_e::connecting;
            p2p_state.socket_name = Options->SocketId->SocketName;
            p2p_state.connection_loss_start = std::chrono::steady_clock::now();

            P2P_Connect_Request_pb* req = new P2P_Connect_Request_pb;
            req->set_socket_name(p2p_state.socket_name);
            send_p2p_connection_request(Options->RemoteUserId->to_string(), req);
        }
    }

    return EOS_EResult::EOS_Success;
}

/**
 * Gets the size of the packet that will be returned by ReceivePacket for a particular user, if there is any available
 * packets to be retrieved.
 *
 * @param Options Information about who is requesting the size of their next packet
 * @param OutPacketSize The amount of bytes required to store the data of the next packet for the requested user
 * @return EOS_EResult::EOS_Success - If OutPacketSize was successfully set and there is data to be received
 *         EOS_EResult::EOS_InvalidParameters - If input was invalid
 *         EOS_EResult::EOS_NotFound  - If there are no packets available for the requesting user
 */
EOS_EResult EOSSDK_P2P::GetNextReceivedPacketSize(const EOS_P2P_GetNextReceivedPacketSizeOptions* Options, uint32_t* OutPacketSizeBytes)
{
    //TRACE_FUNC();
    std::lock_guard<std::recursive_mutex> lk(local_mutex);

    if (Options == nullptr || OutPacketSizeBytes == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Settings::Inst().steam_passthrough)
    {
        *OutPacketSizeBytes = 0;
        return EOS_EResult::EOS_NotFound;
    }

    bool has_packet = false;
    if (Options->RequestedChannel == nullptr)
    {
        if (!_p2p_in_messages_fifo.empty())
        {
            auto const& front = _p2p_in_messages_fifo.front();
            *OutPacketSizeBytes = static_cast<uint32_t>(front.data().length());
            next_requested_channel = front.channel();
            has_packet = true;
        }
    }
    else
    {
        next_requested_channel = *Options->RequestedChannel;
        auto& in_msgs = _p2p_in_messages[next_requested_channel];
        if (!in_msgs.empty())
        {
            *OutPacketSizeBytes = static_cast<uint32_t>(in_msgs.front().data().length());
            has_packet = true;
        }
    }

    if (has_packet)
    {
        return EOS_EResult::EOS_Success;
    }
    
    *OutPacketSizeBytes = 0;
    return EOS_EResult::EOS_NotFound;
}

/**
 * Receive the next packet for the local user, and information associated with this packet, if it exists.
 *
 * @param Options Information about who is requesting the size of their next packet, and how much data can be stored safely
 * @param OutPeerId The Remote User who sent data. Only set if there was a packet to receive.
 * @param OutSocketId The Socket ID of the data that was sent. Only set if there was a packet to receive.
 * @param OutChannel The channel the data was sent on. Only set if there was a packet to receive.
 * @param OutData Buffer to store the data being received. Must be at least EOS_P2P_GetNextReceivedPacketSize in length or data will be truncated
 * @param OutBytesWritten The amount of bytes written to OutData. Only set if there was a packet to receive.
 * @return EOS_EResult::EOS_Success - If the packet was received successfully
 *         EOS_EResult::EOS_InvalidParameters - If input was invalid
 *         EOS_EResult::EOS_NotFound - If there are no packets available for the requesting user
 */
EOS_EResult EOSSDK_P2P::ReceivePacket(const EOS_P2P_ReceivePacketOptions* Options, EOS_ProductUserId* OutPeerId, EOS_P2P_SocketId* OutSocketId, uint8_t* OutChannel, void* OutData, uint32_t* OutBytesWritten)
{
    std::lock_guard<std::recursive_mutex> lk(local_mutex);

    if (Options == nullptr || OutPeerId == nullptr || OutSocketId == nullptr ||
        OutChannel == nullptr || OutData == nullptr || OutBytesWritten == nullptr)
    {
        return EOS_EResult::EOS_InvalidParameters;
    }

    if (Settings::Inst().steam_passthrough)
    {
        *OutBytesWritten = 0;
        return EOS_EResult::EOS_NotFound;
    }

    if (Options->RequestedChannel != nullptr)
        next_requested_channel = *Options->RequestedChannel;

    bool const use_fifo = (Options->RequestedChannel == nullptr && next_requested_channel == -1);

    uint8_t channel = 0;
    if (use_fifo)
    {
        if (_p2p_in_messages_fifo.empty())
            return EOS_EResult::EOS_NotFound;
        channel = _p2p_in_messages_fifo.front().channel();
    }
    else
    {
        channel = static_cast<uint8_t>(next_requested_channel);
    }

    auto ch_it = _p2p_in_messages.find(channel);
    if (ch_it == _p2p_in_messages.end() || ch_it->second.empty())
        return EOS_EResult::EOS_NotFound;

    auto& msg = ch_it->second.front();

    *OutPeerId = GetProductUserId(msg.user_id());
    *OutBytesWritten = static_cast<uint32_t>(msg.data().copy(reinterpret_cast<char*>(OutData), Options->MaxDataSizeBytes));
    msg.socket_name().copy(OutSocketId->SocketName, sizeof(EOS_P2P_SocketId::SocketName));
    OutSocketId->SocketName[32] = 0;
    *OutChannel = msg.channel();
    next_requested_channel = -1;

    log_p2p_game_io("ReceivePacket", msg.user_id(), *OutChannel, *OutBytesWritten);

    ch_it->second.pop_front();

    if (use_fifo)
    {
        if (!_p2p_in_messages_fifo.empty())
            _p2p_in_messages_fifo.pop_front();
    }
    else
    {
        for (auto it = _p2p_in_messages_fifo.begin(); it != _p2p_in_messages_fifo.end(); ++it)
        {
            if (it->channel() == channel)
            {
                _p2p_in_messages_fifo.erase(it);
                break;
            }
        }
    }

    return EOS_EResult::EOS_Success;
}

/**
 * Listen for incoming connection requests on a particular Socket ID, or optionally all Socket IDs. The bound function
 * will only be called if the connection has not already been accepted.
 *
 * @param Options Information about who would like notifications, and (optionally) only for a specific socket
 * @param ClientData This value is returned to the caller when ConnectionRequestHandler is invoked
 * @param ConnectionRequestHandler The callback to be fired when we receive a connection request
 * @return A valid notification ID if successfully bound, or EOS_INVALID_NOTIFICATIONID otherwise
 */
EOS_NotificationId EOSSDK_P2P::AddNotifyPeerConnectionRequest(const EOS_P2P_AddNotifyPeerConnectionRequestOptions* Options, void* ClientData, EOS_P2P_OnIncomingConnectionRequestCallback ConnectionRequestHandler)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (ConnectionRequestHandler == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);
    
    EOS_P2P_OnIncomingConnectionRequestInfo& oicri = res->CreateCallback<EOS_P2P_OnIncomingConnectionRequestInfo>((CallbackFunc)ConnectionRequestHandler);
    oicri.ClientData = ClientData;
    oicri.LocalUserId = Settings::Inst().productuserid;
    oicri.RemoteUserId = GetProductUserId(sdk::NULL_USER_ID);
    oicri.SocketId = new EOS_P2P_SocketId;

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Stop listening for connection requests on a previously bound handler
 *
 * @param NotificationId The previously bound notification ID
 */
void EOSSDK_P2P::RemoveNotifyPeerConnectionRequest(EOS_NotificationId NotificationId)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    GetCB_Manager().remove_notification(this, NotificationId);
}

/**
 * Listen for when a connection is established. This is fired when we first connect to a peer, when we reconnect to a peer after a connection interruption,
 * and when our underlying network connection type changes (for example, from a direct connection to relay, or vice versa). Network Connection Type changes
 * will always be broadcast with a EOS_CET_Reconnection connection type, even if the connection was not interrupted.
 *
 * @param Options Information about who would like notifications about established connections, and for which socket
 * @param ClientData This value is returned to the caller when ConnectionEstablishedHandler is invoked
 * @param ConnectionEstablishedHandler The callback to be fired when a connection has been established
 * @return A valid notification ID if successfully bound, or EOS_INVALID_NOTIFICATIONID otherwise
 *
 * @see EOS_P2P_AddNotifyPeerConnectionInterrupted
 * @see EOS_P2P_AddNotifyPeerConnectionClosed
 * @see EOS_P2P_RemoveNotifyPeerConnectionEstablished
 */
EOS_NotificationId EOSSDK_P2P::AddNotifyPeerConnectionEstablished(const EOS_P2P_AddNotifyPeerConnectionEstablishedOptions* Options, void* ClientData, EOS_P2P_OnPeerConnectionEstablishedCallback ConnectionEstablishedHandler) {
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (ConnectionEstablishedHandler == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_P2P_OnPeerConnectionEstablishedInfo& opcei = res->CreateCallback<EOS_P2P_OnPeerConnectionEstablishedInfo>((CallbackFunc)ConnectionEstablishedHandler);
    opcei.ClientData = ClientData;
    opcei.LocalUserId = Settings::Inst().productuserid;
    opcei.RemoteUserId = GetProductUserId(sdk::NULL_USER_ID);
    opcei.SocketId = new EOS_P2P_SocketId;
    opcei.ConnectionType = EOS_EConnectionEstablishedType::EOS_CET_Reconnection;
    opcei.NetworkType = EOS_ENetworkConnectionType::EOS_NCT_DirectConnection;

    EOS_NotificationId const id = GetCB_Manager().add_notification(this, res);

    if (g_cached_p2p_established != nullptr)
    {
        pFrameResult_t delivery(new FrameResult);
        EOS_P2P_OnPeerConnectionEstablishedInfo& delivery_info =
            delivery->CreateCallback<EOS_P2P_OnPeerConnectionEstablishedInfo>((CallbackFunc)ConnectionEstablishedHandler);
        delivery_info.ClientData = ClientData;
        delivery_info.LocalUserId = Settings::Inst().productuserid;
        queue_peer_connection_established(this, delivery, *g_cached_p2p_established);
        g_cached_p2p_established.reset();
    }

    return id;
}


/**
 * Stop notifications for connections being established on a previously bound handler.
 *
 * @param NotificationId The previously bound notification ID
 *
 * @see EOS_P2P_AddNotifyPeerConnectionEstablished
 */
void EOSSDK_P2P::RemoveNotifyPeerConnectionEstablished(EOS_NotificationId NotificationId) {
    TRACE_FUNC();
    GLOBAL_LOCK();
    GetCB_Manager().remove_notification(this, NotificationId);
}


/**
 * Listen for when a previously opened connection is interrupted. The connection will automatically attempt to reestablish, but it may not be successful.
 *
 * If a connection reconnects, it will trigger the P2P PeerConnectionEstablished notification with the EOS_CET_Reconnection connection type.
 * If a connection fails to reconnect, it will trigger the P2P PeerConnectionClosed notification.
 *
 * @param Options Information about who would like notifications about interrupted connections, and for which socket
 * @param ClientData This value is returned to the caller when ConnectionInterruptedHandler is invoked
 * @param ConnectionInterruptedHandler The callback to be fired when an open connection has been interrupted
 * @return A valid notification ID if successfully bound, or EOS_INVALID_NOTIFICATIONID otherwise
 *
 * @see EOS_P2P_AddNotifyPeerConnectionEstablished
 * @see EOS_P2P_AddNotifyPeerConnectionClosed
 * @see EOS_P2P_RemoveNotifyPeerConnectionInterrupted
 */
EOS_NotificationId EOSSDK_P2P::AddNotifyPeerConnectionInterrupted(const EOS_P2P_AddNotifyPeerConnectionInterruptedOptions* Options, void* ClientData, EOS_P2P_OnPeerConnectionInterruptedCallback ConnectionInterruptedHandler){
    TRACE_FUNC();
    GLOBAL_LOCK();
    if (ConnectionInterruptedHandler == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_P2P_OnPeerConnectionInterruptedInfo& opcii = res->CreateCallback<EOS_P2P_OnPeerConnectionInterruptedInfo>((CallbackFunc)ConnectionInterruptedHandler);
    opcii.ClientData = ClientData;
    opcii.LocalUserId = Settings::Inst().productuserid;
    opcii.RemoteUserId = GetProductUserId(sdk::NULL_USER_ID);
    opcii.SocketId = new EOS_P2P_SocketId;

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Stop notifications for connections being interrupted on a previously bound handler
 *
 * @param NotificationId The previously bound notification ID
 */
void EOSSDK_P2P::RemoveNotifyPeerConnectionInterrupted(EOS_NotificationId NotificationId)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    GetCB_Manager().remove_notification(this, NotificationId);
}


/**
 * Listen for when a previously opened connection is closed
 *
 * @param Options Information about who would like notifications about closed connections, and for which socket
 * @param ClientData This value is returned to the caller when ConnectionClosedHandler is invoked
 * @param ConnectionClosedHandler The callback to be fired when we an open connection has been closed
 * @return A valid notification ID if successfully bound, or EOS_INVALID_NOTIFICATIONID otherwise
 */
EOS_NotificationId EOSSDK_P2P::AddNotifyPeerConnectionClosed(const EOS_P2P_AddNotifyPeerConnectionClosedOptions* Options, void* ClientData, EOS_P2P_OnRemoteConnectionClosedCallback ConnectionClosedHandler)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (ConnectionClosedHandler == nullptr)
        return EOS_INVALID_NOTIFICATIONID;

    pFrameResult_t res(new FrameResult);

    EOS_P2P_OnRemoteConnectionClosedInfo& orcci = res->CreateCallback<EOS_P2P_OnRemoteConnectionClosedInfo>((CallbackFunc)ConnectionClosedHandler);
    orcci.ClientData = ClientData;
    orcci.LocalUserId = Settings::Inst().productuserid;
    orcci.RemoteUserId = GetProductUserId(sdk::NULL_USER_ID);
    orcci.SocketId = new EOS_P2P_SocketId;
    orcci.Reason = EOS_EConnectionClosedReason::EOS_CCR_Unknown;

    return GetCB_Manager().add_notification(this, res);
}

/**
 * Stop notifications for connections being closed on a previously bound handler
 *
 * @param NotificationId The previously bound notification ID
 */
void EOSSDK_P2P::RemoveNotifyPeerConnectionClosed(EOS_NotificationId NotificationId)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    GetCB_Manager().remove_notification(this, NotificationId);
}

/**
 * Accept connections from a specific peer. If this peer has not attempted to connect yet, when they do, they will automatically be accepted.
 *
 * @param Options Information about who would like to accept a connection, and which connection
 * @return EOS_EResult::EOS_Success - if the provided data is valid
 *         EOS_EResult::EOS_InvalidParameters - if the provided data is invalid
 */
EOS_EResult EOSSDK_P2P::AcceptConnection(const EOS_P2P_AcceptConnectionOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->RemoteUserId == nullptr || Options->SocketId == nullptr)
        return EOS_EResult::EOS_InvalidParameters;
    
    auto& conn = _p2p_connections[Options->RemoteUserId];

    if (Options->SocketId != nullptr && Options->SocketId->SocketName[0] != '\0')
        conn.socket_name = Options->SocketId->SocketName;
    else if (conn.socket_name.empty())
        conn.socket_name = "EOSP2PTransport";

    if (conn.status == p2p_state_t::status_e::requesting)
    {
        P2P_Connect_Response_pb* resp = new P2P_Connect_Response_pb;
        resp->set_accepted(true);
        send_p2p_connection_response(Options->RemoteUserId->to_string(), resp);
    }
    
    set_p2p_state_connected(Options->RemoteUserId, conn);
    conn.connection_loss_start = {};
    return EOS_EResult::EOS_Success;
}

/**
 * Stop accepting new connections from a specific peer and close any open connections.
 *
 * @param Options Information about who would like to close a connection, and which connection.
 * @return EOS_EResult::EOS_Success - if the provided data is valid
 *         EOS_EResult::EOS_InvalidParameters - if the provided data is invalid
 */
EOS_EResult EOSSDK_P2P::CloseConnection(const EOS_P2P_CloseConnectionOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();
    
    if (Options == nullptr || Options->RemoteUserId == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    if (Settings::Inst().steam_passthrough)
    {
        APP_LOG(Log::LogLevel::INFO, "P2P CloseConnection ignored in steam_passthrough peer=%s",
            Options->RemoteUserId->to_string().c_str());
        return EOS_EResult::EOS_Success;
    }

    if (Options->SocketId == nullptr)
    {
        auto& conn = _p2p_connections[Options->RemoteUserId];
        APP_LOG(Log::LogLevel::INFO, "P2P CloseConnection requested peer=%s socket=<all> prev_state=%d",
            Options->RemoteUserId->to_string().c_str(),
            static_cast<int>(conn.status));
        conn.p2p_out_messages.clear();
        for (auto& in_msgs : _p2p_in_messages)
        {
            in_msgs.second.erase(std::remove_if(in_msgs.second.begin(), in_msgs.second.end(), [&Options](P2P_Data_Message_pb& msg)
            {
                return msg.user_id() == Options->RemoteUserId->to_string();
            }), in_msgs.second.end());
        }

        if (conn.status != p2p_state_t::status_e::closed)
        {
            log_p2p_close_summary(Options->RemoteUserId->to_string());
            APP_LOG(Log::LogLevel::INFO, "P2P CloseConnection peer=%s socket=%s prev_state=%d",
                Options->RemoteUserId->to_string().c_str(),
                conn.socket_name.c_str(),
                static_cast<int>(conn.status));
            conn.status = p2p_state_t::status_e::closed;

            P2P_Connection_Close_pb* close = new P2P_Connection_Close_pb;
            send_p2p_connetion_close(Options->RemoteUserId->to_string(), close);
        }
    }
    else
    {
        std::string target_sock_name = Options->SocketId->SocketName;
        auto& conn = _p2p_connections[Options->RemoteUserId];
        APP_LOG(Log::LogLevel::INFO, "P2P CloseConnection requested peer=%s socket=%s prev_state=%d",
            Options->RemoteUserId->to_string().c_str(),
            target_sock_name.c_str(),
            static_cast<int>(conn.status));
        conn.p2p_out_messages.clear();
        for (auto& in_msgs : _p2p_in_messages)
        {
            in_msgs.second.erase(std::remove_if(in_msgs.second.begin(), in_msgs.second.end(), [&Options](P2P_Data_Message_pb& msg)
            {
                return msg.user_id() == Options->RemoteUserId->to_string();
            }), in_msgs.second.end());
        }

        if (conn.status != p2p_state_t::status_e::closed)
        {
            if (conn.socket_name == target_sock_name)
            {
                log_p2p_close_summary(Options->RemoteUserId->to_string());
                conn.status = p2p_state_t::status_e::closed;

                P2P_Connection_Close_pb* close = new P2P_Connection_Close_pb;
                send_p2p_connetion_close(Options->RemoteUserId->to_string(), close);
            }
        }
    }
    
    return EOS_EResult::EOS_Success;
}

/**
 * Close any open Connections for a specific Peer Connection ID.
 *
 * @param Options Information about who would like to close connections, and by what socket ID
 * @return EOS_EResult::EOS_Success - if the provided data is valid
 *         EOS_EResult::EOS_InvalidParameters - if the provided data is invalid
 */
EOS_EResult EOSSDK_P2P::CloseConnections(const EOS_P2P_CloseConnectionsOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->SocketId == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    std::string target_sock_name = Options->SocketId->SocketName;
    for (auto& conn : _p2p_connections)
    {
        if (conn.second.socket_name == target_sock_name)
        {
            conn.second.status = p2p_state_t::status_e::closed;

            P2P_Connection_Close_pb* close = new P2P_Connection_Close_pb;
            send_p2p_connetion_close(conn.first->to_string(), close);
        }
    }
    
    return EOS_EResult::EOS_Success;
}

/**
 * Query the current NAT-type of our connection.
 *
 * @param Options Information about what version of the EOS_P2P_QueryNATType API is supported
 * @param NATTypeQueriedHandler The callback to be fired when we finish querying our NAT type
 */
void EOSSDK_P2P::QueryNATType(const EOS_P2P_QueryNATTypeOptions* Options, void* ClientData, const EOS_P2P_OnQueryNATTypeCompleteCallback NATTypeQueriedHandler)
{
    TRACE_FUNC();
    APP_LOG(Log::LogLevel::DEBUG, "TODO");
    GLOBAL_LOCK();

    if (NATTypeQueriedHandler == nullptr)
        return;

    pFrameResult_t res(new FrameResult);
    EOS_P2P_OnQueryNATTypeCompleteInfo& pqntci = res->CreateCallback<EOS_P2P_OnQueryNATTypeCompleteInfo>((CallbackFunc)NATTypeQueriedHandler, std::chrono::milliseconds(5000));
    pqntci.ClientData = ClientData;

    if (Options == nullptr)
    {
        pqntci.NATType = EOS_ENATType::EOS_NAT_Unknown;
        pqntci.ResultCode = EOS_EResult::EOS_InvalidParameters;
    }
    else
    {
        pqntci.NATType = EOS_ENATType::EOS_NAT_Open;
        pqntci.ResultCode = EOS_EResult::EOS_Success;
    }
    
    res->done = true;
    GetCB_Manager().add_callback(this, res);
}

/**
 * Get our last-queried NAT-type, if it has been successfully queried.
 *
 * @param Options Information about what version of the EOS_P2P_GetNATType API they support
 * @param OutNATType The queried NAT Type, or unknown if unknown
 * @return EOS_EResult::EOS_Success - if we have cached data
 *         EOS_EResult::EOS_NotFound - If we do not have queried data cached
 *         EOS_EResult::EOS_IncompatibleVersion - If the provided version is unknown
 */
EOS_EResult EOSSDK_P2P::GetNATType(const EOS_P2P_GetNATTypeOptions* Options, EOS_ENATType* OutNATType)
{
    TRACE_FUNC();
    APP_LOG(Log::LogLevel::DEBUG, "TODO");
    GLOBAL_LOCK();

    *OutNATType = EOS_ENATType::EOS_NAT_Moderate;
    return EOS_EResult::EOS_Success;
}

/**
 * Set how relay servers are to be used. This setting does not immediately apply to existing connections, but may apply to existing
 * connections if the connection requires renegotiation.
 *
 * @param Options Information about relay server config options
 * @return EOS_EResult::EOS_Success - if the options were set successfully
 *         EOS_EResult::EOS_InvalidParameters - if the options are invalid in some way
 */
EOS_EResult EOSSDK_P2P::SetRelayControl(const EOS_P2P_SetRelayControlOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if(Options == nullptr)
        return EOS_EResult::EOS_InvalidParameters;
    
    _relay_control = Options->RelayControl;
    return EOS_EResult::EOS_Success;
}

/**
 * Get the current relay control setting.
 *
 * @param Options Information about what version of the EOS_P2P_GetRelayControl API is supported
 * @param OutRelayControl The relay control setting currently configured
 * @return EOS_EResult::EOS_Success - if the input was valid
 *         EOS_EResult::EOS_InvalidParameters - if the input was invalid in some way
 */
EOS_EResult EOSSDK_P2P::GetRelayControl(const EOS_P2P_GetRelayControlOptions* Options, EOS_ERelayControl* OutRelayControl)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || OutRelayControl == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    *OutRelayControl = _relay_control;
    return EOS_EResult::EOS_Success;
}

/**
 * Set configuration options related to network ports.
 *
 * @param Options Information about network ports config options
 * @return EOS_EResult::EOS_Success - if the options were set successfully
 *         EOS_EResult::EOS_InvalidParameters - if the options are invalid in some way
 */
EOS_EResult EOSSDK_P2P::SetPortRange(const EOS_P2P_SetPortRangeOptions* Options)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || Options->Port <= 1024)
        return EOS_EResult::EOS_InvalidParameters;

    _p2p_port = Options->Port;
    _max_additional_ports_to_try = Options->MaxAdditionalPortsToTry;
    return EOS_EResult::EOS_Success;
}

/**
 * Get the current chosen port and the amount of other ports to try above the chosen port if the chosen port is unavailable.
 *
 * @param Options Information about what version of the EOS_P2P_GetPortRange API is supported
 * @param OutPort The port that will be tried first
 * @param OutNumAdditionalPortsToTry The amount of ports to try above the value in OutPort, if OutPort is unavailable
 * @return EOS_EResult::EOS_Success - if the input options were valid
 *         EOS_EResult::EOS_InvalidParameters - if the input was invalid in some way
 */
EOS_EResult EOSSDK_P2P::GetPortRange(const EOS_P2P_GetPortRangeOptions* Options, uint16_t* OutPort, uint16_t* OutNumAdditionalPortsToTry)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    if (Options == nullptr || OutPort == nullptr || OutNumAdditionalPortsToTry == nullptr)
        return EOS_EResult::EOS_InvalidParameters;

    *OutPort = _p2p_port;
    *OutNumAdditionalPortsToTry = _max_additional_ports_to_try;
    return EOS_EResult::EOS_Success;
}

///////////////////////////////////////////////////////////////////////////////
//                           Network Send messages                           //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_P2P::send_p2p_connection_request(Network::peer_t const& peerid, P2P_Connect_Request_pb* req) const
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    P2P_Message_pb* p2p = new P2P_Message_pb;

    p2p->set_allocated_connect_request(req);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    msg.set_allocated_p2p(p2p);

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_P2P::send_p2p_connection_response(Network::peer_t const& peerid, P2P_Connect_Response_pb* resp) const
{
    TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    P2P_Message_pb* p2p = new P2P_Message_pb;

    p2p->set_allocated_connect_response(resp);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    msg.set_allocated_p2p(p2p);

    return GetNetwork().TCPSendTo(msg);
}

bool EOSSDK_P2P::send_p2p_data(Network::peer_t const& peerid, P2P_Data_Message_pb* data) const
{
    //TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    P2P_Message_pb* p2p = new P2P_Message_pb;

    p2p->set_allocated_data_message(data);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    msg.set_allocated_p2p(p2p);

    size_t const payload_bytes = p2p->has_data_message() ? p2p->data_message().data().size() : 0;
    int const channel = p2p->has_data_message() ? static_cast<int>(p2p->data_message().channel()) : -1;
    auto res = send_p2p_network_message(peerid, msg, "send", channel, payload_bytes);

    (void)p2p->release_data_message();

    return res;
}

bool EOSSDK_P2P::send_p2p_data_ack(Network::peer_t const& peerid, P2P_Data_Acknowledge_pb* ack) const
{
    //TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    P2P_Message_pb* p2p = new P2P_Message_pb;

    p2p->set_allocated_data_acknowledge(ack);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    msg.set_allocated_p2p(p2p);

    int const channel = p2p->has_data_acknowledge() ? static_cast<int>(p2p->data_acknowledge().channel()) : -1;
    return send_p2p_network_message(peerid, msg, "ack", channel, 0);
}

bool EOSSDK_P2P::send_p2p_connetion_close(Network::peer_t const& peerid, P2P_Connection_Close_pb* close) const
{
    //TRACE_FUNC();
    std::string const& user_id = Settings::Inst().productuserid->to_string();

    Network_Message_pb msg;
    P2P_Message_pb* p2p = new P2P_Message_pb;

    p2p->set_allocated_connection_close(close);

    msg.set_source_id(user_id);
    msg.set_dest_id(peerid);
    msg.set_game_id(Settings::Inst().network_game_id());

    msg.set_allocated_p2p(p2p);

    return GetNetwork().TCPSendTo(msg);
}

///////////////////////////////////////////////////////////////////////////////
//                          Network Receive messages                         //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_P2P::on_peer_connect(Network_Message_pb const& msg, Network_Peer_Connect_pb const& peer)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    auto peer_id = GetProductUserId(msg.source_id());
    auto it = _p2p_connections.find(peer_id);
    if (it != _p2p_connections.end() && it->second.status == p2p_state_t::status_e::connection_loss)
    {
        it->second.status = p2p_state_t::status_e::connected;

        // Now that the client is back, send all queued messages
        for (auto& msg : it->second.p2p_out_messages)
        {
            send_p2p_data(it->first->to_string(), &msg);
        }
        it->second.p2p_out_messages.clear();
    }

    return true;
}

bool EOSSDK_P2P::on_peer_disconnect(Network_Message_pb const& msg, Network_Peer_Disconnect_pb const& peer)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    auto peer_id = GetProductUserId(msg.source_id());
    auto it = _p2p_connections.find(peer_id);
    if (it != _p2p_connections.end())
    {
        it->second.connection_loss_start = std::chrono::steady_clock::now();
    }

    std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnPeerConnectionInterruptedInfo::k_iCallback));
    for (auto& notif : notifs)
    {
        EOS_P2P_OnPeerConnectionInterruptedInfo& opcii = notif->GetCallback<EOS_P2P_OnPeerConnectionInterruptedInfo>();
        opcii.LocalUserId = Settings::Inst().productuserid;
        opcii.RemoteUserId = peer_id;
        notif->GetFunc()(notif->GetFuncParam());
    }

    return true;
}

bool EOSSDK_P2P::on_p2p_connection_request(Network_Message_pb const& msg, P2P_Connect_Request_pb const& req)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    auto peer_id = GetProductUserId(msg.source_id());
    auto& conn = _p2p_connections[peer_id];
    if (conn.status != p2p_state_t::status_e::connected)
    {
        conn.status = p2p_state_t::status_e::requesting;
        conn.connection_loss_start = std::chrono::steady_clock::now();
        conn.socket_name = req.socket_name();
        std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnIncomingConnectionRequestInfo::k_iCallback));
        for (auto& notif : notifs)
        {
            EOS_P2P_OnIncomingConnectionRequestInfo& oicrc = notif->GetCallback<EOS_P2P_OnIncomingConnectionRequestInfo>();
            oicrc.RemoteUserId = peer_id;
            strncpy(const_cast<char*>(oicrc.SocketId->SocketName), req.socket_name().c_str(), sizeof(EOS_P2P_SocketId::SocketName));

            notif->GetFunc()(notif->GetFuncParam());
        }

        for (auto const& entry : GetEOS_Sessions()._sessions)
        {
            session_state_t const* session = &entry.second;
            if (GetEOS_Sessions().is_player_in_session(msg.source_id(), const_cast<session_state_t*>(session)) ||
                GetEOS_Sessions().is_player_registered(msg.source_id(), const_cast<session_state_t*>(session)))
            {
                APP_LOG(Log::LogLevel::INFO, "Auto-accepting P2P for session member %s", msg.source_id().c_str());
                ensure_peer_connection(msg.source_id(), req.socket_name());
                break;
            }
        }

        if (conn.status == p2p_state_t::status_e::requesting &&
            GetEOS_Lobby().is_peer_member_of_my_owned_lobby(msg.source_id()))
        {
            APP_LOG(Log::LogLevel::INFO, "Auto-accepting P2P for lobby member %s", msg.source_id().c_str());
            ensure_peer_connection(msg.source_id(), req.socket_name());
        }
    }
    else
    {
        P2P_Connect_Response_pb* resp = new P2P_Connect_Response_pb;
        resp->set_accepted(true);
        send_p2p_connection_response(msg.source_id(), resp);
    }

    return true;
}

bool EOSSDK_P2P::on_p2p_connection_response(Network_Message_pb const& msg, P2P_Connect_Response_pb const& resp)
{
    TRACE_FUNC();
    GLOBAL_LOCK();
    
    EOS_ProductUserId remote_id = GetProductUserId(msg.source_id());
    if (resp.accepted())
    {
        set_p2p_state_connected(remote_id, _p2p_connections[remote_id]);
    }
    else
    {
        std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnRemoteConnectionClosedInfo::k_iCallback));
        for (auto& notif : notifs)
        {
            EOS_P2P_OnRemoteConnectionClosedInfo& orcci = notif->GetCallback<EOS_P2P_OnRemoteConnectionClosedInfo>();
            orcci.Reason = EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer;
            orcci.RemoteUserId = remote_id;

            notif->GetFunc()(notif->GetFuncParam());
        }
    }

    return true;
}

bool EOSSDK_P2P::on_p2p_data(Network_Message_pb const& msg, P2P_Data_Message_pb const& data)
{
    //TRACE_FUNC();
    std::lock_guard<std::recursive_mutex> lk(local_mutex);

    EOS_ProductUserId remote_id = GetProductUserId(msg.source_id());
    auto& p2p_state = _p2p_connections[remote_id];

    P2P_Data_Acknowledge_pb* ack = new P2P_Data_Acknowledge_pb;

    switch (p2p_state.status)
    {
        case p2p_state_t::status_e::connecting:
        {
            APP_LOG(Log::LogLevel::INFO, "Implicit P2P acceptation on receive");
            set_p2p_state_connected(remote_id, p2p_state);
        }

        case p2p_state_t::status_e::connected:
        {
            ack->set_channel(data.channel());
            ack->set_accepted(true);
            _p2p_in_messages[data.channel()].emplace_back(data);
            _p2p_in_messages_fifo.push_back(data);
            log_p2p_game_io("recv", msg.source_id(), static_cast<uint8_t>(data.channel()),
                static_cast<uint32_t>(data.data().size()));

            // Redpoint/UE: joiner sends on ch172; host game listens on ch255.
            if (data.channel() == 172 &&
                should_mirror_game_p2p_for_peer(msg.source_id()))
            {
                auto& travel_stats = peer_travel_stats(msg.source_id());
                if (static_cast<uint32_t>(data.data().size()) == 73)
                {
                    travel_stats.host_pending_travel_reply = true;
                    travel_stats.host_travel_handshake_received = std::chrono::steady_clock::now();
                    travel_stats.travel_initial_packet = data.data();
                    log_ue_handshake_packet("Initial recv", msg.source_id(), data.data());
                }
                else if (travel_stats.host_sent_travel_reply &&
                         static_cast<uint32_t>(data.data().size()) > 5)
                {
                    ue_parsed_handshake_t parsed{};
                    if (parse_ue_stateless_handshake(data.data(), parsed) &&
                        parsed.packet_type == ue_handshake_packet_type_e::Response)
                    {
                        travel_stats.host_pending_travel_ack = true;
                        travel_stats.host_travel_response_received = std::chrono::steady_clock::now();
                        travel_stats.travel_response_packet = data.data();
                        log_ue_handshake_packet("Response recv", msg.source_id(), data.data());
                    }
                }

                P2P_Data_Message_pb mirror = data;
                mirror.set_channel(255);
                _p2p_in_messages[255].emplace_back(mirror);
                _p2p_in_messages_fifo.push_back(mirror);
                log_p2p_game_io("recv redirect ch172->ch255", msg.source_id(), 255,
                    static_cast<uint32_t>(data.data().size()));
            }
        }
        break;

        default:
            ack->set_accepted(false);
    }

    return send_p2p_data_ack(msg.source_id(), ack);
}

bool EOSSDK_P2P::on_p2p_data_ack(Network_Message_pb const& msg, P2P_Data_Acknowledge_pb const& ack)
{
    //TRACE_FUNC();
    GLOBAL_LOCK();

    return true;
}

bool EOSSDK_P2P::on_p2p_connection_close(Network_Message_pb const& msg, P2P_Connection_Close_pb const& close)
{
    TRACE_FUNC();
    GLOBAL_LOCK();

    std::vector<pFrameResult_t> notifs = std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnRemoteConnectionClosedInfo::k_iCallback));
    for (auto& notif : notifs)
    {
        EOS_P2P_OnRemoteConnectionClosedInfo& orcci = notif->GetCallback<EOS_P2P_OnRemoteConnectionClosedInfo>();
        orcci.Reason = EOS_EConnectionClosedReason::EOS_CCR_ClosedByPeer;
        orcci.RemoteUserId = GetProductUserId(msg.source_id());

        notif->GetFunc()(notif->GetFuncParam());
    }

    _p2p_connections[GetProductUserId(msg.source_id())].status = p2p_state_t::status_e::closed;

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//                                 IRunFrame                                 //
///////////////////////////////////////////////////////////////////////////////
bool EOSSDK_P2P::CBRunFrame()
{
    GLOBAL_LOCK();

    for (auto it = _p2p_connections.begin(); it != _p2p_connections.end(); ++it)
    {
        switch(it->second.status)
        {
            case p2p_state_t::status_e::requesting:
            {
                auto now = std::chrono::steady_clock::now();
                if ((now - it->second.connection_loss_start) > connecting_timeout)
                {// We didn't accept the connection
                    it->second.status = p2p_state_t::status_e::closed;
                    it->second.p2p_out_messages.clear();
                }
            }
            break;

            case p2p_state_t::status_e::connecting:
            {
                auto now = std::chrono::steady_clock::now();
                if ((now - it->second.connection_loss_start) > connecting_timeout)
                {
                    APP_LOG(Log::LogLevel::INFO, "P2P connect timeout peer=%s socket=%s",
                        it->first->to_string().c_str(), it->second.socket_name.c_str());
                    it->second.status = p2p_state_t::status_e::closed;
                    it->second.p2p_out_messages.clear();

                    std::vector<pFrameResult_t> notifs(std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnRemoteConnectionClosedInfo::k_iCallback)));
                    for (auto& notif : notifs)
                    {
                        EOS_P2P_OnRemoteConnectionClosedInfo& orcci = notif->GetCallback<EOS_P2P_OnRemoteConnectionClosedInfo>();
                        orcci.RemoteUserId = it->first;
                        strncpy(const_cast<char*>(orcci.SocketId->SocketName), it->second.socket_name.c_str(), sizeof(orcci.SocketId->SocketName));
                        const_cast<char*>(orcci.SocketId->SocketName)[sizeof(orcci.SocketId->SocketName) - 1] = '\0';
                        orcci.Reason = EOS_EConnectionClosedReason::EOS_CCR_ConnectionFailed;
                    }
                }
            }
            break;

            case p2p_state_t::status_e::connection_loss:
            {
                auto now = std::chrono::steady_clock::now();
                if ((now - it->second.connection_loss_start) > connection_timeout)
                {
                    it->second.status = p2p_state_t::status_e::closed;
                    it->second.p2p_out_messages.clear();

                    std::vector<pFrameResult_t> notifs(std::move(GetCB_Manager().get_notifications(this, EOS_P2P_OnRemoteConnectionClosedInfo::k_iCallback)));
                    for (auto& notif : notifs)
                    {
                        EOS_P2P_OnRemoteConnectionClosedInfo& orcci = notif->GetCallback<EOS_P2P_OnRemoteConnectionClosedInfo>();
                        orcci.RemoteUserId = it->first;
                        strncpy(const_cast<char*>(orcci.SocketId->SocketName), it->second.socket_name.c_str(), sizeof(orcci.SocketId->SocketName));
                        const_cast<char*>(orcci.SocketId->SocketName)[sizeof(orcci.SocketId->SocketName) - 1] = '\0';
                        orcci.Reason = EOS_EConnectionClosedReason::EOS_CCR_TimedOut;
                    }
                }
            }
            break;
        }
    }

    auto const now = std::chrono::steady_clock::now();
    for (auto& entry : g_p2p_peer_travel_stats)
    {
        auto& stats = entry.second;
        if (!should_mirror_game_p2p_for_peer(entry.first))
            continue;

        EOS_ProductUserId remote_id = GetProductUserId(entry.first);
        if (remote_id == nullptr)
            continue;

        auto conn_it = _p2p_connections.find(remote_id);
        if (conn_it == _p2p_connections.end() ||
            conn_it->second.status != p2p_state_t::status_e::connected)
        {
            continue;
        }

        auto send_proxy_packet = [&](uint8_t channel, std::string const& payload, char const* log_tag)
        {
            P2P_Data_Message_pb* reply = new P2P_Data_Message_pb;
            reply->set_channel(channel);
            reply->set_data(payload);
            reply->set_socket_name(conn_it->second.socket_name.empty() ? "EOSP2PTransport" : conn_it->second.socket_name);
            reply->set_user_id(Settings::Inst().productuserid->to_string());
            send_p2p_data(entry.first, reply);

            APP_LOG(Log::LogLevel::INFO,
                "P2P game channel proxy ch172 %s peer=%s endpoint=%s bytes=%zu hex=%s",
                log_tag,
                entry.first.c_str(),
                GetNetwork().format_peer_endpoint(entry.first).c_str(),
                payload.size(),
                format_hex_dump(payload).c_str());
        };

        if (stats.host_pending_travel_reply && !stats.host_sent_travel_reply &&
            (now - stats.host_travel_handshake_received) >= std::chrono::milliseconds(400) &&
            !stats.travel_initial_packet.empty())
        {
            auto challenge = build_ue_challenge_from_initial(stats.travel_initial_packet, stats.pending_challenge);
            if (!challenge.has_value())
            {
                APP_LOG(Log::LogLevel::WARN,
                    "UE handshake Challenge synthesis failed peer=%s initial_bytes=%zu hex=%s",
                    entry.first.c_str(),
                    stats.travel_initial_packet.size(),
                    format_hex_dump(stats.travel_initial_packet).c_str());
                stats.host_pending_travel_reply = false;
                continue;
            }

            log_ue_handshake_packet("Challenge synth", entry.first, *challenge);
            send_proxy_packet(172, *challenge, "UE Challenge reply");
            stats.host_pending_travel_reply = false;
            stats.host_sent_travel_reply = true;
        }

        if (stats.host_pending_travel_ack && !stats.host_sent_travel_ack &&
            (now - stats.host_travel_response_received) >= std::chrono::milliseconds(400) &&
            !stats.travel_initial_packet.empty() &&
            !stats.travel_response_packet.empty())
        {
            ue_parsed_handshake_t initial_parsed{};
            if (!parse_ue_stateless_handshake(stats.travel_initial_packet, initial_parsed))
                continue;

            auto ack = build_ue_ack_from_response(
                stats.travel_response_packet,
                initial_parsed,
                stats.pending_challenge);
            if (!ack.has_value())
                continue;

            log_ue_handshake_packet("Ack synth", entry.first, *ack);
            send_proxy_packet(172, *ack, "UE Ack reply");
            stats.host_pending_travel_ack = false;
            stats.host_sent_travel_ack = true;
        }
    }

    return true;
}

bool EOSSDK_P2P::RunNetwork(Network_Message_pb const& msg)
{
    switch (msg.messages_case())
    {
        case Network_Message_pb::MessagesCase::kP2P:
        {
            P2P_Message_pb const& p2p = msg.p2p();
            switch (p2p.message_case())
            {
                case P2P_Message_pb::MessageCase::kConnectRequest : return on_p2p_connection_request(msg, p2p.connect_request());
                case P2P_Message_pb::MessageCase::kConnectResponse: return on_p2p_connection_response(msg, p2p.connect_response());
                case P2P_Message_pb::MessageCase::kDataMessage    : return on_p2p_data(msg, p2p.data_message());
                case P2P_Message_pb::MessageCase::kDataAcknowledge: return on_p2p_data_ack(msg, p2p.data_acknowledge());
                case P2P_Message_pb::MessageCase::kConnectionClose: return on_p2p_connection_close(msg, p2p.connection_close());
                default: APP_LOG(Log::LogLevel::WARN, "Unhandled network message %d", p2p.message_case());
            }
        }
    }

    return true;
}

bool EOSSDK_P2P::RunCallbacks(pFrameResult_t res)
{
    GLOBAL_LOCK();

    return res->done;
}

void EOSSDK_P2P::FreeCallback(pFrameResult_t res)
{
    GLOBAL_LOCK();

    switch (res->ICallback())
    {
        /////////////////////////////
        //        Callbacks        //
        /////////////////////////////
        //case callback_type::k_iCallback:
        //{
        //    callback_type& callback = res->GetCallback<callback_type>();
        //    // Free resources
        //}
        //break;
        /////////////////////////////
        //      Notifications      //
        /////////////////////////////
        case EOS_P2P_OnIncomingConnectionRequestInfo::k_iCallback:
        {
            EOS_P2P_OnIncomingConnectionRequestInfo& callback = res->GetCallback<EOS_P2P_OnIncomingConnectionRequestInfo>();
            delete callback.SocketId;
        }
        break;
        case EOS_P2P_OnRemoteConnectionClosedInfo::k_iCallback:
        {
            EOS_P2P_OnRemoteConnectionClosedInfo& callback = res->GetCallback<EOS_P2P_OnRemoteConnectionClosedInfo>();
            delete callback.SocketId;
        }
        break;
        case EOS_P2P_OnPeerConnectionEstablishedInfo::k_iCallback:
        {
            EOS_P2P_OnPeerConnectionEstablishedInfo& callback = res->GetCallback<EOS_P2P_OnPeerConnectionEstablishedInfo>();
            delete callback.SocketId;
        }
        break;
    }
}

}