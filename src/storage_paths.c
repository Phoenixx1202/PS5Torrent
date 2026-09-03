#include "storage_paths.h"
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

void storage_paths_get(storage_path_t *paths, size_t *count)
{
    if (!paths || !count) return;

    size_t i = 0;

    /* USB drives - most common for homebrew */
    strcpy(paths[i].path,  "/mnt/usb0/");
    strcpy(paths[i].label, "USB Pendrive (usb0)");
    strcpy(paths[i].icon,  "💾");
    paths[i].is_removable = 1;
    paths[i].is_default   = 1;
    i++;

    strcpy(paths[i].path,  "/mnt/usb1/");
    strcpy(paths[i].label, "Second USB (usb1)");
    strcpy(paths[i].icon,  "💾");
    paths[i].is_removable = 1;
    paths[i].is_default   = 0;
    i++;

    strcpy(paths[i].path,  "/mnt/usb2/");
    strcpy(paths[i].label, "Third USB (usb2)");
    strcpy(paths[i].icon,  "💾");
    paths[i].is_removable = 1;
    paths[i].is_default   = 0;
    i++;

    /* NVMe / internal SSD */
    strcpy(paths[i].path,  "/mnt/nvme0/");
    strcpy(paths[i].label, "NVMe SSD (nvme0)");
    strcpy(paths[i].icon,  "💿");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    strcpy(paths[i].path,  "/mnt/nvme1/");
    strcpy(paths[i].label, "Second NVMe (nvme1)");
    strcpy(paths[i].icon,  "💿");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    /* External HDD/SSD via USB */
    strcpy(paths[i].path,  "/mnt/ext0/");
    strcpy(paths[i].label, "External HDD (ext0)");
    strcpy(paths[i].icon,  "💽");
    paths[i].is_removable = 1;
    paths[i].is_default   = 0;
    i++;

    strcpy(paths[i].path,  "/mnt/ext1/");
    strcpy(paths[i].label, "Second External (ext1)");
    strcpy(paths[i].icon,  "💽");
    paths[i].is_removable = 1;
    paths[i].is_default   = 0;
    i++;

    /* Internal system data */
    strcpy(paths[i].path,  "/data/");
    strcpy(paths[i].label, "System Data (internal)");
    strcpy(paths[i].icon,  "🔧");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    strcpy(paths[i].path,  "/system_data/");
    strcpy(paths[i].label, "System Data (alt)");
    strcpy(paths[i].icon,  "🔧");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    /* SATA */
    strcpy(paths[i].path,  "/mnt/sata0/");
    strcpy(paths[i].label, "SATA SSD (sata0)");
    strcpy(paths[i].icon,  "🖴");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    /* User home */
    strcpy(paths[i].path,  "/user/");
    strcpy(paths[i].label, "User Data");
    strcpy(paths[i].icon,  "👤");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    /* Temp */
    strcpy(paths[i].path,  "/tmp/");
    strcpy(paths[i].label, "Temp (RAM)");
    strcpy(paths[i].icon,  "⚡");
    paths[i].is_removable = 0;
    paths[i].is_default   = 0;
    i++;

    *count = i;
}

const char *storage_paths_get_default(void)
{
    return "/mnt/usb0/";
}

int storage_path_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
        return 1;
    return 0;
}

int storage_path_is_writable(const char *path)
{
    struct statvfs fs;

    return storage_path_exists(path) &&
           access(path, W_OK | X_OK) == 0 &&
           statvfs(path, &fs) == 0 &&
           !(fs.f_flag & ST_RDONLY);
}
