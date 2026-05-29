#include "robot_video_service.h"

#include <os/str.h>
#include <components/log.h>
#include "cli.h"

#define TAG "robot_video_cli"
#define LOGI(...) BK_LOGI(TAG, ##__VA_ARGS__)

static void robot_video_cli(char *pcWriteBuffer, int xWriteBufferLen, int argc, char **argv)
{
    (void)pcWriteBuffer;
    (void)xWriteBufferLen;

    if (argc < 2) {
        LOGI("usage: rvideo open|close|status\n");
        return;
    }

    if (os_strcmp(argv[1], "open") == 0) {
        robot_video_config_t cfg;
        robot_video_service_default_config(&cfg);
        robot_video_service_start(&cfg);
    } else if (os_strcmp(argv[1], "close") == 0) {
        robot_video_service_stop();
    } else if (os_strcmp(argv[1], "status") == 0) {
        robot_video_status_t status = {0};
        robot_video_service_get_status(&status);
        LOGI("running=%d %ux%u@%u bitrate=%u\n",
             status.running, status.config.width, status.config.height,
             status.config.fps, status.config.bitrate_kbps);
    }
}

#define ROBOT_VIDEO_CMD_CNT 1
static const struct cli_command s_robot_video_cmds[ROBOT_VIDEO_CMD_CNT] = {
    {"rvideo", "rvideo open|close|status", robot_video_cli},
};

int robot_video_cli_init(void)
{
    return cli_register_commands(s_robot_video_cmds, ROBOT_VIDEO_CMD_CNT);
}
