#include "torrent_mgr.h"
#include "app_log.h"

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <ctime>
#include <utility>
#include <vector>

namespace lt = libtorrent;

namespace {

std::array<managed_torrent_t, MAX_TORRENTS> g_torrents{};
int g_num_torrents = 0;
uint64_t g_next_generation = 1;
std::unique_ptr<lt::session> g_session;

struct libtorrent_slot_t {
    lt::add_torrent_params params;
    lt::torrent_handle handle;
    bool added = false;
    bool paused = true;
    bool has_metadata = false;
    uint64_t last_downloaded = 0;
    uint64_t last_uploaded = 0;
};

std::array<std::unique_ptr<libtorrent_slot_t>, MAX_TORRENTS> g_slots{};

uint64_t now_seconds()
{
    return static_cast<uint64_t>(std::time(nullptr));
}

void safe_copy(char *dst, size_t dst_size, const std::string& value)
{
    if (!dst || dst_size == 0) return;
    std::snprintf(dst, dst_size, "%s", value.c_str());
}

void safe_copy_c(char *dst, size_t dst_size, const char *value)
{
    safe_copy(dst, dst_size, value ? std::string(value) : std::string());
}

void log_line(const char *level, const char *fmt, ...)
{
    char buffer[768];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    app_log_write(level, buffer);
}

std::string hex_hash(lt::sha1_hash const& hash)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.resize(40);
    auto const* bytes = reinterpret_cast<unsigned char const*>(hash.data());
    for (std::size_t i = 0; i < 20; ++i) {
        unsigned char byte = bytes[i];
        out[i * 2] = digits[byte >> 4];
        out[i * 2 + 1] = digits[byte & 0x0f];
    }
    return out;
}

lt::settings_pack make_settings()
{
    lt::settings_pack settings;
    settings.set_str(lt::settings_pack::user_agent, "PS5Torrent/2.0.4 libtorrent/" LIBTORRENT_VERSION);
    settings.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    settings.set_bool(lt::settings_pack::enable_dht, true);
    settings.set_bool(lt::settings_pack::enable_lsd, true);
    settings.set_bool(lt::settings_pack::enable_outgoing_utp, true);
    settings.set_bool(lt::settings_pack::enable_incoming_utp, true);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_bool(lt::settings_pack::announce_to_all_tiers, true);
    settings.set_bool(lt::settings_pack::announce_to_all_trackers, true);
    settings.set_int(lt::settings_pack::active_downloads, MAX_TORRENTS);
    settings.set_int(lt::settings_pack::connections_limit, 220);
    settings.set_int(lt::settings_pack::unchoke_slots_limit, 32);
    settings.set_int(lt::settings_pack::request_queue_time, 3);
    settings.set_int(lt::settings_pack::max_peerlist_size, 2500);
    settings.set_int(lt::settings_pack::alert_mask,
        lt::alert_category::error |
        lt::alert_category::storage |
        lt::alert_category::status |
        lt::alert_category::tracker |
        lt::alert_category::peer |
        lt::alert_category::dht);
    return settings;
}

bool ensure_session()
{
    if (g_session) return true;
    try {
        lt::session_params params(make_settings());
        g_session = std::make_unique<lt::session>(std::move(params));
        g_session->add_extension(&lt::create_ut_metadata_plugin);
        g_session->add_extension(&lt::create_ut_pex_plugin);
        app_log_write("INFO", "libtorrent session initialized");
        return true;
    } catch (std::exception const& e) {
        log_line("ERROR", "libtorrent session initialization failed: %s", e.what());
        return false;
    }
}

int find_by_handle(lt::torrent_handle const& handle)
{
    if (!handle.is_valid()) return -1;
    for (int i = 0; i < g_num_torrents; ++i) {
        if (g_slots[i] && g_slots[i]->added && g_slots[i]->handle == handle)
            return i;
    }
    return -1;
}

void set_error(managed_torrent_t& mt, torrent_error_t type, std::string const& message)
{
    mt.state = TORRENT_ERROR;
    mt.error_type = type;
    safe_copy(mt.error_msg, sizeof(mt.error_msg), message);
}

void fill_metadata_from_params(int index)
{
    managed_torrent_t& mt = g_torrents[index];
    libtorrent_slot_t& slot = *g_slots[index];

    if (slot.params.ti) {
        auto const& info = *slot.params.ti;
        safe_copy(mt.name, sizeof(mt.name), info.name());
        mt.total_size = static_cast<uint64_t>(std::max<std::int64_t>(0, info.total_size()));
        mt.piece_mgr.num_pieces = static_cast<size_t>(std::max(0, info.num_pieces()));
        mt.piece_mgr.piece_length = info.piece_length();
        mt.piece_mgr.total_size = info.total_size();
        auto best = info.info_hashes().get_best();
        std::string id = hex_hash(best);
        safe_copy(mt.id, sizeof(mt.id), id);
        std::memcpy(mt.info_hash, best.data(), 20);
        slot.has_metadata = true;
    } else {
        auto best = slot.params.info_hashes.get_best();
        if (!best.is_all_zeros()) {
            std::string id = hex_hash(best);
            safe_copy(mt.id, sizeof(mt.id), id);
            std::memcpy(mt.info_hash, best.data(), 20);
        } else {
            std::snprintf(mt.id, sizeof(mt.id), "magnet-%d", index);
        }
        if (!slot.params.name.empty())
            safe_copy(mt.name, sizeof(mt.name), slot.params.name);
        else if (mt.id[0])
            std::snprintf(mt.name, sizeof(mt.name), "Torrent_%s", mt.id);
        else
            std::snprintf(mt.name, sizeof(mt.name), "Torrent_%d", index);
    }
}

void erase_index(int index)
{
    for (int i = index; i < g_num_torrents - 1; ++i) {
        g_torrents[i] = g_torrents[i + 1];
        g_slots[i] = std::move(g_slots[i + 1]);
    }
    if (g_num_torrents > 0) {
        --g_num_torrents;
        std::memset(&g_torrents[g_num_torrents], 0, sizeof(g_torrents[g_num_torrents]));
        g_slots[g_num_torrents].reset();
    }
}

const char *state_name(torrent_state_t state)
{
    switch (state) {
    case TORRENT_STOPPED: return "stopped";
    case TORRENT_DOWNLOADING: return "downloading";
    case TORRENT_SEEDING: return "seeding";
    case TORRENT_ERROR: return "error";
    case TORRENT_DONE: return "done";
    default: return "stopped";
    }
}

struct json_writer_t {
    char *buf;
    size_t size;
    size_t pos;
};

void json_writef(json_writer_t *writer, const char *fmt, ...)
{
    if (!writer || writer->pos >= writer->size) return;
    va_list args;
    va_start(args, fmt);
    int written = std::vsnprintf(writer->buf + writer->pos,
                                 writer->size - writer->pos, fmt, args);
    va_end(args);
    if (written < 0) return;
    if (static_cast<size_t>(written) >= writer->size - writer->pos) {
        writer->pos = writer->size - 1;
        writer->buf[writer->pos] = '\0';
        return;
    }
    writer->pos += static_cast<size_t>(written);
}

void json_write_string(json_writer_t *writer, const char *value)
{
    static constexpr char hex[] = "0123456789abcdef";
    const unsigned char *p = reinterpret_cast<const unsigned char *>(value ? value : "");
    json_writef(writer, "\"");
    while (*p && writer->pos + 7 < writer->size) {
        unsigned char c = *p++;
        if (c == '"' || c == '\\') json_writef(writer, "\\%c", c);
        else if (c == '\b') json_writef(writer, "\\b");
        else if (c == '\f') json_writef(writer, "\\f");
        else if (c == '\n') json_writef(writer, "\\n");
        else if (c == '\r') json_writef(writer, "\\r");
        else if (c == '\t') json_writef(writer, "\\t");
        else if (c < 0x20) {
            char escaped[7] = {'\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], 0};
            json_writef(writer, "%s", escaped);
        } else json_writef(writer, "%c", c);
    }
    json_writef(writer, "\"");
}

void handle_alert(lt::alert const* alert)
{
    if (!alert) return;

    if (auto const* a = lt::alert_cast<lt::add_torrent_alert>(alert)) {
        int index = find_by_handle(a->handle);
        if (index >= 0) {
            g_slots[index]->added = true;
            g_slots[index]->handle = a->handle;
            g_torrents[index].state = TORRENT_DOWNLOADING;
            g_torrents[index].error_type = TERR_NONE;
            g_torrents[index].error_msg[0] = '\0';
            log_line("INFO", "libtorrent added torrent: id=%d, name=%s", index, g_torrents[index].name);
        }
    } else if (auto const* a = lt::alert_cast<lt::metadata_received_alert>(alert)) {
        int index = find_by_handle(a->handle);
        if (index >= 0) {
            g_slots[index]->has_metadata = true;
            auto status = a->handle.status(lt::torrent_handle::query_name | lt::torrent_handle::query_torrent_file);
            if (!status.name.empty()) safe_copy(g_torrents[index].name, sizeof(g_torrents[index].name), status.name);
            if (auto ti = status.torrent_file.lock()) {
                g_torrents[index].total_size = static_cast<uint64_t>(std::max<std::int64_t>(0, ti->total_size()));
            }
            log_line("INFO", "libtorrent metadata received: id=%d, name=%s", index, g_torrents[index].name);
        }
    } else if (auto const* a = lt::alert_cast<lt::torrent_finished_alert>(alert)) {
        int index = find_by_handle(a->handle);
        if (index >= 0) {
            g_torrents[index].state = TORRENT_DONE;
            g_torrents[index].progress = 1.0f;
            app_log_write("SUCCESS", "libtorrent download completed");
        }
    } else if (auto const* a = lt::alert_cast<lt::torrent_error_alert>(alert)) {
        int index = find_by_handle(a->handle);
        if (index >= 0) set_error(g_torrents[index], TERR_WRITE_FAILED, a->error.message());
        log_line("ERROR", "libtorrent torrent error: %s", a->error.message().c_str());
    } else if (auto const* a = lt::alert_cast<lt::tracker_error_alert>(alert)) {
        log_line("WARN", "libtorrent tracker error: %s", a->error.message().c_str());
    } else if (auto const* a = lt::alert_cast<lt::tracker_reply_alert>(alert)) {
        log_line("INFO", "libtorrent tracker reply: peers=%d", a->num_peers);
    } else if (auto const* a = lt::alert_cast<lt::peer_error_alert>(alert)) {
        log_line("WARN", "libtorrent peer error: %s", a->error.message().c_str());
    } else if (alert->category() & lt::alert_category::error) {
        log_line("WARN", "libtorrent alert: %s", alert->message().c_str());
    }
}

void pump_alerts()
{
    if (!g_session) return;
    std::vector<lt::alert*> alerts;
    g_session->pop_alerts(&alerts);
    for (lt::alert const* alert : alerts) handle_alert(alert);
}

void refresh_slot(int index)
{
    if (index < 0 || index >= g_num_torrents || !g_slots[index]) return;
    libtorrent_slot_t& slot = *g_slots[index];
    managed_torrent_t& mt = g_torrents[index];
    if (!slot.added || !slot.handle.is_valid()) return;

    try {
        auto status = slot.handle.status(lt::torrent_handle::query_name |
                                         lt::torrent_handle::query_save_path |
                                         lt::torrent_handle::query_torrent_file);
        if (!status.name.empty()) safe_copy(mt.name, sizeof(mt.name), status.name);
        if (auto ti = status.torrent_file.lock()) {
            mt.total_size = static_cast<uint64_t>(std::max<std::int64_t>(0, ti->total_size()));
            mt.piece_mgr.num_pieces = static_cast<size_t>(std::max(0, ti->num_pieces()));
            mt.piece_mgr.piece_length = ti->piece_length();
            mt.piece_mgr.total_size = ti->total_size();
            mt.piece_mgr.num_complete = static_cast<size_t>(std::max(0, status.num_pieces));
            mt.torrent = nullptr;
        }
        if (status.errc) {
            set_error(mt, TERR_WRITE_FAILED, status.errc.message());
        } else if (status.is_seeding) {
            mt.state = TORRENT_DONE;
            mt.progress = 1.0f;
        } else if (slot.paused) {
            mt.state = TORRENT_STOPPED;
        } else {
            mt.state = TORRENT_DOWNLOADING;
        }

        mt.progress = static_cast<float>(std::max(0.0f, std::min(1.0f, status.progress)));
        mt.downloaded = static_cast<uint64_t>(std::max<std::int64_t>(0, status.total_done));
        mt.uploaded = static_cast<uint64_t>(std::max<std::int64_t>(0, status.total_upload));
        mt.speed_down = static_cast<uint64_t>(std::max(0, status.download_rate));
        mt.speed_up = static_cast<uint64_t>(std::max(0, status.upload_rate));
        mt.num_peers = status.num_peers;
        mt.active_peers = status.num_peers;
        mt.tracker_announces = status.current_tracker.empty() ? 0 : 1;
        if (mt.total_size == 0 && status.total_wanted > 0)
            mt.total_size = static_cast<uint64_t>(status.total_wanted);
        if (mt.error_type == TERR_NONE) mt.error_msg[0] = '\0';
    } catch (std::exception const& e) {
        set_error(mt, TERR_TRACKER_FAILED, e.what());
    }
}

} // namespace

extern "C" void torrent_mgr_init(void)
{
    g_session.reset();
    for (auto& slot : g_slots) slot.reset();
    std::memset(g_torrents.data(), 0, sizeof(managed_torrent_t) * g_torrents.size());
    g_num_torrents = 0;
    ensure_session();
}

extern "C" int torrent_mgr_add(torrent_t *torrent, const char *save_path,
                                int is_magnet, const char *magnet_uri)
{
    (void)torrent;
    if (is_magnet && magnet_uri) return torrent_mgr_add_magnet(magnet_uri, save_path);
    return -1;
}

extern "C" int torrent_mgr_add_raw(const unsigned char *data, size_t data_len,
                                    const char *save_path)
{
    if (!data || data_len == 0 || !save_path || g_num_torrents >= MAX_TORRENTS)
        return -1;
    if (!ensure_session()) return -1;

    try {
        auto slot = std::make_unique<libtorrent_slot_t>();
        lt::span<char const> buffer(reinterpret_cast<char const*>(data), static_cast<std::ptrdiff_t>(data_len));
        slot->params = lt::load_torrent_buffer(buffer);
        slot->params.save_path = save_path;
        slot->params.flags |= lt::torrent_flags::auto_managed;
        slot->params.flags &= ~lt::torrent_flags::paused;
        slot->paused = true;

        int index = g_num_torrents;
        managed_torrent_t& mt = g_torrents[index];
        std::memset(&mt, 0, sizeof(mt));
        mt.generation = g_next_generation++;
        safe_copy_c(mt.save_path, sizeof(mt.save_path), save_path);
        mt.state = TORRENT_STOPPED;
        mt.error_type = TERR_NONE;
        mt.is_magnet = 0;
        g_slots[index] = std::move(slot);
        fill_metadata_from_params(index);
        ++g_num_torrents;
        log_line("INFO", "libtorrent torrent loaded from buffer: id=%d, bytes=%zu, name=%s", index, data_len, mt.name);
        return index;
    } catch (std::exception const& e) {
        log_line("ERROR", "libtorrent failed to load torrent buffer: %s", e.what());
        return -1;
    }
}

extern "C" int torrent_mgr_add_magnet(const char *magnet_uri, const char *save_path)
{
    if (!magnet_uri || std::strncmp(magnet_uri, "magnet:", 7) != 0 ||
        !save_path || g_num_torrents >= MAX_TORRENTS)
        return -1;
    if (!ensure_session()) return -1;

    lt::error_code ec;
    auto params = lt::parse_magnet_uri(magnet_uri, ec);
    if (ec) {
        log_line("ERROR", "libtorrent failed to parse magnet URI: %s", ec.message().c_str());
        return -1;
    }

    auto slot = std::make_unique<libtorrent_slot_t>();
    slot->params = std::move(params);
    slot->params.save_path = save_path;
    slot->params.flags |= lt::torrent_flags::auto_managed;
    slot->params.flags &= ~lt::torrent_flags::paused;
    slot->paused = true;

    int index = g_num_torrents;
    managed_torrent_t& mt = g_torrents[index];
    std::memset(&mt, 0, sizeof(mt));
    mt.generation = g_next_generation++;
    safe_copy_c(mt.save_path, sizeof(mt.save_path), save_path);
    safe_copy_c(mt.magnet_uri, sizeof(mt.magnet_uri), magnet_uri);
    mt.state = TORRENT_STOPPED;
    mt.error_type = TERR_NONE;
    mt.is_magnet = 1;
    g_slots[index] = std::move(slot);
    fill_metadata_from_params(index);
    ++g_num_torrents;
    log_line("INFO", "libtorrent magnet loaded: id=%d, name=%s", index, mt.name);
    return index;
}

extern "C" int torrent_mgr_start(int index)
{
    if (index < 0 || index >= g_num_torrents || !g_slots[index]) return -1;
    if (!ensure_session()) return -1;
    pump_alerts();

    managed_torrent_t& mt = g_torrents[index];
    libtorrent_slot_t& slot = *g_slots[index];
    mt.error_type = TERR_NONE;
    mt.error_msg[0] = '\0';

    try {
        if (!slot.added || !slot.handle.is_valid()) {
            lt::error_code ec;
            slot.handle = g_session->add_torrent(std::move(slot.params), ec);
            if (ec || !slot.handle.is_valid()) {
                set_error(mt, TERR_PARSE_FAILED, ec ? ec.message() : std::string("invalid torrent handle"));
                return -1;
            }
            slot.added = true;
        }
        slot.paused = false;
        slot.handle.resume();
        slot.handle.force_reannounce(0);
        mt.state = TORRENT_DOWNLOADING;
        mt.start_time = now_seconds();
        mt.last_speed_calc = mt.start_time;
        app_log_write("INFO", "libtorrent download started");
        return 0;
    } catch (std::exception const& e) {
        set_error(mt, TERR_PARSE_FAILED, e.what());
        return -1;
    }
}

extern "C" void torrent_mgr_stop(int index)
{
    if (index < 0 || index >= g_num_torrents || !g_slots[index]) return;
    libtorrent_slot_t& slot = *g_slots[index];
    if (slot.added && slot.handle.is_valid()) {
        try { slot.handle.pause(); } catch (...) {}
    }
    slot.paused = true;
    g_torrents[index].state = TORRENT_STOPPED;
    g_torrents[index].speed_down = 0;
    g_torrents[index].speed_up = 0;
    g_torrents[index].generation = g_next_generation++;
    app_log_write("INFO", "libtorrent torrent paused");
}

extern "C" void torrent_mgr_remove(int index)
{
    if (index < 0 || index >= g_num_torrents) return;
    if (g_session && g_slots[index] && g_slots[index]->added && g_slots[index]->handle.is_valid()) {
        try { g_session->remove_torrent(g_slots[index]->handle); } catch (...) {}
    }
    erase_index(index);
    app_log_write("INFO", "libtorrent torrent removed");
}

extern "C" managed_torrent_t *torrent_mgr_get(int index)
{
    if (index < 0 || index >= g_num_torrents) return nullptr;
    pump_alerts();
    refresh_slot(index);
    return &g_torrents[index];
}

extern "C" int torrent_mgr_count(void)
{
    return g_num_torrents;
}

extern "C" int torrent_mgr_capacity(void)
{
    return MAX_TORRENTS;
}

extern "C" int torrent_mgr_find(const unsigned char info_hash[20])
{
    if (!info_hash) return -1;
    for (int i = 0; i < g_num_torrents; ++i)
        if (std::memcmp(g_torrents[i].info_hash, info_hash, 20) == 0) return i;
    return -1;
}

extern "C" int torrent_mgr_tick(void)
{
    pump_alerts();
    int active = 0;
    for (int i = 0; i < g_num_torrents; ++i) {
        refresh_slot(i);
        if (g_torrents[i].state == TORRENT_DOWNLOADING ||
            g_torrents[i].state == TORRENT_SEEDING)
            ++active;
    }
    return active;
}

extern "C" void torrent_mgr_status_json(char *buf, size_t bufsz)
{
    if (!buf || bufsz < 64) return;
    torrent_mgr_tick();

    json_writer_t writer{buf, bufsz, 0};
    uint64_t now = now_seconds();
    json_writef(&writer, "{\"torrents\":[");
    for (int i = 0; i < g_num_torrents; ++i) {
        managed_torrent_t& mt = g_torrents[i];
        uint64_t remaining = mt.total_size > mt.downloaded ? mt.total_size - mt.downloaded : 0;
        uint64_t elapsed = mt.start_time > 0 && now >= mt.start_time ? now - mt.start_time : 0;
        size_t pieces_total = mt.piece_mgr.num_pieces;
        size_t pieces_done = mt.piece_mgr.num_complete;

        if (i > 0) json_writef(&writer, ",");
        json_writef(&writer, "{\"id\":%d,\"name\":", i);
        json_write_string(&writer, mt.name);
        json_writef(&writer, ",\"hash\":");
        json_write_string(&writer, mt.id);
        json_writef(&writer,
            ",\"state\":\"%s\"," 
            "\"progress\":%.3f,"
            "\"size\":%llu,"
            "\"downloaded\":%llu,"
            "\"uploaded\":%llu,"
            "\"remaining\":%llu,"
            "\"speed_down\":%llu,"
            "\"speed_up\":%llu,"
            "\"peers\":%d,"
            "\"active_peers\":%d,"
            "\"pieces_done\":%zu,"
            "\"pieces_total\":%zu,"
            "\"elapsed\":%llu,"
            "\"tracker_announces\":%d,"
            "\"is_magnet\":%s,"
            "\"save_path\":",
            state_name(mt.state),
            static_cast<double>(mt.progress),
            static_cast<unsigned long long>(mt.total_size),
            static_cast<unsigned long long>(mt.downloaded),
            static_cast<unsigned long long>(mt.uploaded),
            static_cast<unsigned long long>(remaining),
            static_cast<unsigned long long>(mt.speed_down),
            static_cast<unsigned long long>(mt.speed_up),
            mt.num_peers,
            mt.active_peers,
            pieces_done,
            pieces_total,
            static_cast<unsigned long long>(elapsed),
            mt.tracker_announces,
            mt.is_magnet ? "true" : "false");
        json_write_string(&writer, mt.save_path);
        json_writef(&writer, ",\"error\":");
        json_write_string(&writer, mt.error_msg[0] ? mt.error_msg : "");
        json_writef(&writer, "}");
    }
    json_writef(&writer, "]}");
}

extern "C" const char *torrent_mgr_error_str(torrent_error_t err)
{
    switch (err) {
    case TERR_NONE: return "No error";
    case TERR_PARSE_FAILED: return "Parse failed";
    case TERR_TRACKER_FAILED: return "Tracker failed";
    case TERR_NO_PEERS: return "No peers found";
    case TERR_WRITE_FAILED: return "Write failed";
    case TERR_HASH_MISMATCH: return "Hash mismatch";
    default: return "Unknown error";
    }
}

extern "C" void torrent_mgr_shutdown(void)
{
    if (g_session) {
        for (int i = g_num_torrents - 1; i >= 0; --i) torrent_mgr_remove(i);
        pump_alerts();
        g_session.reset();
    }
    for (auto& slot : g_slots) slot.reset();
    std::memset(g_torrents.data(), 0, sizeof(managed_torrent_t) * g_torrents.size());
    g_num_torrents = 0;
}
