#include "ps5_browser.h"
#include "ui.h"
#include <stdint.h>

extern int sceSystemServiceLaunchWebBrowser(const char *uri, void *args);
extern int sceUserServiceInitialize(void *params);
extern int sceUserServiceTerminate(void);
static int user_service_initialized = 0;
extern int sceUserServiceGetForegroundUser(int *user_id);
typedef struct {
    uint32_t size;
    int user_id;
    uint32_t app_options;
    uint64_t crash_report;
    uint64_t check_flags;
} launch_params_t;
extern int sceSystemServiceLaunchApp(const char *, const char *[], launch_params_t *);

int ps5_browser_open_tile(void)
{
    launch_params_t params = {0};
    params.size = sizeof(params);
    if (!user_service_initialized) return -1;
    int result = sceUserServiceGetForegroundUser(&params.user_id);
    if (result) return result;
    result = sceSystemServiceLaunchApp("HBTR00001", NULL, &params);
    if (result < 0) ui_error("Could not launch media tile (0x%x)", result);
    return result < 0 ? result : 0;
}

int ps5_browser_init(void)
{
    int result = sceUserServiceInitialize(NULL);
    if (result == 0) {
        user_service_initialized = 1;
        return 0;
    }

    ui_error("Could not initialize PS5 UserService (0x%x)", result);
    return result;
}

int ps5_browser_open(const char *url)
{
    if (!url || !url[0])
        return -1;

    if (!user_service_initialized) {
        ui_error("PS5 browser requested before UserService initialization");
        return -1;
    }

    int result = sceSystemServiceLaunchWebBrowser(url, NULL);
    if (result != 0)
        ui_error("Could not open PS5 browser (0x%x)", result);
    return result;
}

void ps5_browser_shutdown(void)
{
    if (!user_service_initialized) return;
    sceUserServiceTerminate();
    user_service_initialized = 0;
}
