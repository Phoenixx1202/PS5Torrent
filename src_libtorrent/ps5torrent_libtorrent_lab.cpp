#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>
#include <libtorrent/extensions/ut_pex.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/posix_disk_io.hpp>
#include <libtorrent/read_resume_data.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/session_params.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>
#include <libtorrent/write_resume_data.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <exception>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <utility>
#include <vector>

namespace lt = libtorrent;
using clock_type = std::chrono::steady_clock;

namespace {

constexpr char data_root[] = "/data/PS5Torrent";
constexpr char download_root[] = "/data/PS5Torrent/libtorrent-downloads";
constexpr char state_root[] = "/data/PS5Torrent/libtorrent-state";
constexpr char resume_path[] = "/data/PS5Torrent/libtorrent-state/lab.fastresume";
constexpr char resume_temp_path[] = "/data/PS5Torrent/libtorrent-state/lab.fastresume.tmp";
constexpr char default_torrent_path[] = "/data/PS5Torrent/input.torrent";
constexpr char log_path[] = "/data/PS5Torrent/PS5Torrent-libtorrent-lab.log";
constexpr int progress_notify_step = 10;

struct notification_request {
  char reserved[45];
  char message[3075];
};

extern "C" int sceKernelSendNotificationRequest(int, notification_request *,
                                                size_t, int);

std::FILE *log_file = nullptr;
clock_type::time_point started;

void notify(char const *message) {
  notification_request request {};
  std::snprintf(request.message, sizeof(request.message), "%s", message);
  sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

void log_line(char const *phase, char const *message, bool flush = false) {
  if(log_file == nullptr) return;
  auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      clock_type::now() - started);
  std::fprintf(log_file, "[%lld ms] [%s] %s\n",
               static_cast<long long>(elapsed.count()), phase,
               message != nullptr ? message : "");
  if(flush) std::fflush(log_file);
}

bool ensure_directory(char const *path) {
  if(::mkdir(path, 0755) == 0 || errno == EEXIST) return true;
  char message[256];
  std::snprintf(message, sizeof(message), "mkdir failed: %.180s errno=%d",
                path, errno);
  log_line("fs", message, true);
  return false;
}

bool prepare_directories() {
  return ensure_directory(data_root) &&
         ensure_directory(download_root) &&
         ensure_directory(state_root);
}

bool read_file(char const *path, std::vector<char> &contents) {
  std::FILE *file = std::fopen(path, "rb");
  if(file == nullptr) return false;
  if(std::fseek(file, 0, SEEK_END) != 0) {
    std::fclose(file);
    return false;
  }
  long const size = std::ftell(file);
  if(size < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
    std::fclose(file);
    return false;
  }
  contents.resize(static_cast<std::size_t>(size));
  bool const ok = contents.empty() ||
      std::fread(contents.data(), 1, contents.size(), file) == contents.size();
  std::fclose(file);
  if(!ok) contents.clear();
  return ok;
}

bool write_resume(lt::add_torrent_params const &params) {
  std::vector<char> const contents = lt::write_resume_data_buf(params);
  std::FILE *file = std::fopen(resume_temp_path, "wb");
  if(file == nullptr) return false;
  bool const ok = std::fwrite(contents.data(), 1, contents.size(), file) ==
                  contents.size() && std::fflush(file) == 0;
  std::fclose(file);
  if(!ok || std::rename(resume_temp_path, resume_path) != 0) {
    std::remove(resume_temp_path);
    return false;
  }
  return true;
}

lt::add_torrent_params make_add_params(std::string const &source) {
  if(source.rfind("magnet:?", 0) == 0) return lt::parse_magnet_uri(source);
  return lt::load_torrent_file(source);
}

std::string source_from_args(int argc, char **argv) {
  if(argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0')
    return argv[1];
  return default_torrent_path;
}

void log_status(lt::torrent_status const &status) {
  char message[512];
  int const progress = static_cast<int>(status.progress_ppm / 10000);
  std::snprintf(message, sizeof(message),
      "%s | %d%% | down=%d B/s up=%d B/s peers=%d seeds=%d total=%lld done=%lld",
      status.name.c_str(), progress, status.download_payload_rate,
      status.upload_payload_rate, status.num_peers, status.num_seeds,
      static_cast<long long>(status.total_wanted),
      static_cast<long long>(status.total_wanted_done));
  log_line("status", message);
}

} // namespace

int main(int argc, char **argv) {
  started = clock_type::now();
  if(!prepare_directories()) {
    notify("PS5Torrent libtorrent lab: directory setup failed");
    return 2;
  }

  log_file = std::fopen(log_path, "ab");
  if(log_file != nullptr)
    std::fprintf(log_file, "\n=== PS5Torrent libtorrent lab %s ===\n",
                 PS5TORRENT_VERSION);

  std::string const source = source_from_args(argc, argv);
  log_line("startup", source.c_str(), true);
  notify("PS5Torrent libtorrent lab started");

  try {
    lt::settings_pack settings;
    settings.set_bool(lt::settings_pack::enable_dht, true);
    settings.set_bool(lt::settings_pack::enable_lsd, true);
    settings.set_bool(lt::settings_pack::enable_upnp, false);
    settings.set_bool(lt::settings_pack::enable_natpmp, false);
    settings.set_str(lt::settings_pack::listen_interfaces, "0.0.0.0:0");
    settings.set_int(lt::settings_pack::disk_write_mode,
                     lt::settings_pack::always_pwrite);
    settings.set_int(lt::settings_pack::alert_mask,
                     lt::alert_category::error |
                     lt::alert_category::storage |
                     lt::alert_category::status |
                     lt::alert_category::tracker |
                     lt::alert_category::peer |
                     lt::alert_category::dht);

    lt::session_params configuration(settings);
    configuration.disk_io_constructor = lt::posix_disk_io_constructor;
    lt::session session(std::move(configuration));
    session.add_extension(&lt::create_ut_metadata_plugin);
    session.add_extension(&lt::create_ut_pex_plugin);

    lt::add_torrent_params params = make_add_params(source);
    params.save_path = download_root;

    std::vector<char> resume_data;
    if(read_file(resume_path, resume_data)) {
      lt::error_code error;
      lt::add_torrent_params restored = lt::read_resume_data(resume_data, error);
      if(!error && restored.info_hashes == params.info_hashes) {
        params = std::move(restored);
        params.save_path = download_root;
        log_line("resume", "resume data restored");
      } else {
        log_line("resume", error ? error.message().c_str() :
                 "resume data ignored for different torrent");
      }
    }

    lt::torrent_handle handle = session.add_torrent(std::move(params));
    log_line("torrent", "torrent added to libtorrent session", true);

    int last_bucket = -1;
    auto last_status = clock_type::now() - std::chrono::seconds(10);
    auto last_resume = clock_type::now();
    bool resume_pending = false;

    for(;;) {
      std::vector<lt::alert *> alerts;
      session.pop_alerts(&alerts);
      for(lt::alert const *alert : alerts) {
        bool const important =
            lt::alert_cast<lt::torrent_error_alert>(alert) != nullptr ||
            lt::alert_cast<lt::file_error_alert>(alert) != nullptr ||
            lt::alert_cast<lt::metadata_failed_alert>(alert) != nullptr ||
            lt::alert_cast<lt::save_resume_data_failed_alert>(alert) != nullptr ||
            lt::alert_cast<lt::torrent_finished_alert>(alert) != nullptr;
        log_line("alert", alert->message().c_str(), important);

        if(auto const *resume =
              lt::alert_cast<lt::save_resume_data_alert>(alert)) {
          if(resume->handle == handle) {
            bool const saved = write_resume(resume->params);
            log_line("resume", saved ? "resume data saved" :
                     "failed to save resume data", !saved);
            resume_pending = false;
          }
        } else if(auto const *failed =
                     lt::alert_cast<lt::save_resume_data_failed_alert>(alert)) {
          if(failed->handle == handle) resume_pending = false;
        }

        if(lt::alert_cast<lt::torrent_finished_alert>(alert) != nullptr) {
          notify("PS5Torrent libtorrent lab: download finished and verified");
          handle.save_resume_data(lt::torrent_handle::save_info_dict);
          resume_pending = true;
        }
      }

      lt::torrent_status const status = handle.status();
      int const bucket = static_cast<int>(status.progress_ppm / 100000);
      if(bucket > last_bucket) {
        char message[160];
        std::snprintf(message, sizeof(message),
                      "PS5Torrent libtorrent lab: %d%% %d kB/s",
                      static_cast<int>(status.progress_ppm / 10000),
                      status.download_payload_rate / 1000);
        notify(message);
        last_bucket = bucket;
      }

      auto const now = clock_type::now();
      if(now - last_status >= std::chrono::seconds(5)) {
        log_status(status);
        std::fflush(log_file);
        last_status = now;
      }
      if(!resume_pending && now - last_resume >= std::chrono::seconds(90)) {
        handle.save_resume_data(lt::torrent_handle::only_if_modified |
                                lt::torrent_handle::save_info_dict);
        resume_pending = true;
        last_resume = now;
      }
      if(status.is_seeding && status.progress_ppm >= 1000000) {
        if(!resume_pending) {
          log_line("complete", "download complete; lab keeps seeding", true);
          handle.save_resume_data(lt::torrent_handle::save_info_dict);
          resume_pending = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(progress_notify_step));
      } else {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
      }
    }
  } catch(std::exception const &error) {
    log_line("exception", error.what(), true);
    notify("PS5Torrent libtorrent lab failed; check log");
    if(log_file != nullptr) std::fclose(log_file);
    return 1;
  } catch(...) {
    log_line("exception", "unknown exception", true);
    notify("PS5Torrent libtorrent lab failed; check log");
    if(log_file != nullptr) std::fclose(log_file);
    return 1;
  }
}
