/*
 * send_chat_raw.c - Direct USB Bulk sender for ESP32-S3 e-Paper Chat Display
 * Talks directly to USB usbfs (/dev/bus/usb/...) without requiring python or cdc_acm.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <linux/usbdevice_fs.h>

#define TARGET_VID  "303a"
#define TARGET_PID  "8000"
#define EP_CDC_OUT  0x02
#define EP_CDC_IN   0x84
#define CDC_DATA_IF 1

static int find_esp32_device(char *dev_path, size_t max_len) {
    DIR *dir = opendir("/sys/bus/usb/devices");
    if (!dir) return -1;

    struct dirent *ent;
    char path[256];
    char vid[16], pid[16];
    int bus = -1, dev = -1;

    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor", ent->d_name);
        FILE *fvid = fopen(path, "r");
        if (!fvid) continue;
        if (!fgets(vid, sizeof(vid), fvid)) { fclose(fvid); continue; }
        fclose(fvid);
        vid[strcspn(vid, "\r\n")] = '\0';

        snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct", ent->d_name);
        FILE *fpid = fopen(path, "r");
        if (!fpid) continue;
        if (!fgets(pid, sizeof(pid), fpid)) { fclose(fpid); continue; }
        fclose(fpid);
        pid[strcspn(pid, "\r\n")] = '\0';

        if (strcmp(vid, TARGET_VID) == 0 && strcmp(pid, TARGET_PID) == 0) {
            snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/busnum", ent->d_name);
            FILE *fbus = fopen(path, "r");
            if (fbus) { fscanf(fbus, "%d", &bus); fclose(fbus); }

            snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/devnum", ent->d_name);
            FILE *fdev = fopen(path, "r");
            if (fdev) { fscanf(fdev, "%d", &dev); fclose(fdev); }

            if (bus > 0 && dev > 0) {
                snprintf(dev_path, max_len, "/dev/bus/usb/%03d/%03d", bus, dev);
                closedir(dir);
                return 0;
            }
        }
    }
    closedir(dir);
    return -1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <message or command>\n", argv[0]);
        fprintf(stderr, "Examples:\n");
        fprintf(stderr, "  %s \"/agent Hello from Linux!\"\n", argv[0]);
        fprintf(stderr, "  %s \"/user Hello OpenStick\"\n", argv[0]);
        fprintf(stderr, "  %s \"/demo\"\n", argv[0]);
        fprintf(stderr, "  %s \"/rotate\"\n", argv[0]);
        fprintf(stderr, "  %s \"/clear\"\n", argv[0]);
        return 1;
    }

    char dev_path[128];
    if (find_esp32_device(dev_path, sizeof(dev_path)) != 0) {
        fprintf(stderr, "Error: ESP32 device (%s:%s) not found in /sys/bus/usb/devices!\n", TARGET_VID, TARGET_PID);
        return 1;
    }

    printf("Connecting to ESP32 on USB device: %s\n", dev_path);
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("Failed to open USB device (try running with sudo)");
        return 1;
    }

    int iface = CDC_DATA_IF;
    if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &iface) < 0) {
        perror("USBDEVFS_CLAIMINTERFACE failed");
        close(fd);
        return 1;
    }

    char tx_buf[512];
    snprintf(tx_buf, sizeof(tx_buf), "%s\n", argv[1]);
    size_t tx_len = strlen(tx_buf);

    struct usbdevfs_bulktransfer bulk_tx = {
        .ep = EP_CDC_OUT,
        .len = (unsigned int)tx_len,
        .timeout = 3000,
        .data = tx_buf
    };

    printf("Sending: \"%s\"\n", argv[1]);
    int ret = ioctl(fd, USBDEVFS_BULK, &bulk_tx);
    if (ret < 0) {
        perror("USB Bulk OUT transfer failed");
        ioctl(fd, USBDEVFS_RELEASEINTERFACE, &iface);
        close(fd);
        return 1;
    }

    // Wait for response ("OK\r\n")
    char rx_buf[64] = {0};
    struct usbdevfs_bulktransfer bulk_rx = {
        .ep = EP_CDC_IN,
        .len = sizeof(rx_buf) - 1,
        .timeout = 5000,
        .data = rx_buf
    };

    ret = ioctl(fd, USBDEVFS_BULK, &bulk_rx);
    if (ret > 0) {
        rx_buf[ret] = '\0';
        printf("[ESP32 Response] %s", rx_buf);
    } else {
        printf("[ESP32] Command sent successfully.\n");
    }

    ioctl(fd, USBDEVFS_RELEASEINTERFACE, &iface);
    close(fd);
    return 0;
}
