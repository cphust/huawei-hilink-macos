/*
 * HiLinkSwitch — based on function0xMarki/HiLinkSwitch, MIT License.
 * Upstream: f4f0aac8d33a152cf60db53cecd555daa1d6dae1
 *
 * 为 E8372h-155 提供只读状态与单次 USB 模式切换。
 * 不修改网络服务、路由、DNS、Wi-Fi、VPN 或系统安全设置。
 */
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/usb/IOUSBLib.h>
#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <xpc/xpc.h>

#define HUAWEI_VID 0x12d1
#define CDROM_PID 0x1f01
#define MAX_DISKS 8
#define WAIT_SECONDS 60

extern char **environ;

enum state { QUERY_ERROR, ABSENT, AMBIGUOUS, CDROM, UNSUPPORTED,
             WAIT_DRIVER, WAIT_INTERFACE, WAIT_ADDRESS, READY };
struct device {
    io_service_t service;
    int64_t pid, location;
    bool ecm, link;
    int interfaces;
    char interface[IF_NAMESIZE], ipv4[INET_ADDRSTRLEN], driver[128];
};

static double monotime(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static void logmsg(const char *fmt, ...)
{
    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tm;
    if (localtime_r(&now, &tm))
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    fprintf(stderr, "%s hilink-switch: ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

static int64_t int_property(io_service_t svc, CFStringRef key)
{
    int64_t value = -1;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    if (ref) {
        if (CFGetTypeID(ref) == CFNumberGetTypeID())
            CFNumberGetValue(ref, kCFNumberSInt64Type, &value);
        CFRelease(ref);
    }
    return value;
}

static bool string_property(io_service_t svc, CFStringRef key, char *buf, size_t size)
{
    bool ok = false;
    CFTypeRef ref = IORegistryEntryCreateCFProperty(svc, key, kCFAllocatorDefault, 0);
    if (ref) {
        if (CFGetTypeID(ref) == CFStringGetTypeID())
            ok = CFStringGetCString(ref, buf, (CFIndex)size, kCFStringEncodingUTF8);
        CFRelease(ref);
    }
    return ok;
}

static kern_return_t usb_devices(io_iterator_t *it)
{
    return IOServiceGetMatchingServices(kIOMainPortDefault,
        IOServiceMatching(kIOUSBHostDeviceClassName), it);
}

static bool usable_ipv4(uint32_t address)
{
    uint32_t a = ntohl(address);
    return a && (a >> 24) != 127 && (a >> 16) != 0xa9fe;
}

static void read_network(struct device *d)
{
    io_iterator_t it;
    if (IORegistryEntryCreateIterator(d->service, kIOServicePlane,
            kIORegistryIterateRecursively, &it) != KERN_SUCCESS)
        return;
    io_object_t obj;
    while ((obj = IOIteratorNext(it))) {
        io_name_t name = "";
        IORegistryEntryGetName(obj, name);
        if (!strcmp(name, "AppleUserECM") || !strcmp(name, "AppleUSBECMControl")) {
            d->ecm = true;
            snprintf(d->driver, sizeof d->driver, "%s", name);
        }
        if (IOObjectConformsTo(obj, "IONetworkInterface")) {
            char bsd[IF_NAMESIZE] = "";
            if (string_property(obj, CFSTR("BSD Name"), bsd, sizeof bsd)) {
                d->interfaces++;
                snprintf(d->interface, sizeof d->interface, "%s", bsd);
            }
        }
        IOObjectRelease(obj);
    }
    IOObjectRelease(it);
    struct ifaddrs *addresses;
    if (getifaddrs(&addresses) != 0)
        return;
    for (struct ifaddrs *a = addresses; a; a = a->ifa_next) {
        if (strcmp(a->ifa_name, d->interface))
            continue;
        d->link = (a->ifa_flags & IFF_UP) && (a->ifa_flags & IFF_RUNNING);
        if (a->ifa_addr && a->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)a->ifa_addr;
            if (usable_ipv4(sin->sin_addr.s_addr))
                inet_ntop(AF_INET, &sin->sin_addr, d->ipv4, sizeof d->ipv4);
        }
    }
    freeifaddrs(addresses);
}

/* location=-1 用于初次查找；切换后只等待原 USB 物理位置，避免认错别的网卡。 */
static int scan(int64_t location, struct device *d)
{
    memset(d, 0, sizeof *d);
    io_iterator_t it;
    if (usb_devices(&it) != KERN_SUCCESS)
        return -1;
    int count = 0;
    io_service_t dev;
    while ((dev = IOIteratorNext(it))) {
        if (int_property(dev, CFSTR(kUSBVendorID)) == HUAWEI_VID &&
            (location == -1 || int_property(dev, CFSTR(kUSBDevicePropertyLocationID)) == location)) {
            count++;
            if (count == 1) {
                d->service = dev;
                d->pid = int_property(dev, CFSTR(kUSBProductID));
                d->location = int_property(dev, CFSTR(kUSBDevicePropertyLocationID));
                read_network(d);
                continue;
            }
        }
        IOObjectRelease(dev);
    }
    IOObjectRelease(it);
    return count;
}

static void release_device(struct device *d)
{
    if (d->service)
        IOObjectRelease(d->service);
    d->service = IO_OBJECT_NULL;
}

static enum state classify(int count, const struct device *d)
{
    if (count < 0) return QUERY_ERROR;
    if (!count) return ABSENT;
    if (count > 1 || d->interfaces > 1) return AMBIGUOUS;
    if (d->pid == CDROM_PID) return CDROM;
    if (!d->ecm) return d->interfaces ? UNSUPPORTED : WAIT_DRIVER;
    if (!d->interfaces) return WAIT_INTERFACE;
    if (!d->link || !d->ipv4[0]) return WAIT_ADDRESS;
    return READY;
}

static const char *state_name(enum state s)
{
    const char *names[] = { "query_error", "absent", "ambiguous", "cdrom",
        "unsupported", "waiting_driver", "waiting_interface", "waiting_address", "address_ready" };
    return names[s];
}

static int state_exit(enum state s)
{
    if (s == READY) return 0;
    if (s == QUERY_ERROR) return 1;
    if (s == ABSENT) return 3;
    if (s == AMBIGUOUS) return 4;
    return 5;
}

static void report(int count, const struct device *d)
{
    enum state s = classify(count, d);
    printf("state=%s\n", state_name(s));
    printf("devices=%d\n", count);
    if (count == 1) {
        printf("usb_id=12d1:%04llx\nlocation=0x%08llx\n",
            (long long)d->pid, (long long)d->location);
        printf("driver=%s\ninterface=%s\nipv4=%s\n",
            d->driver[0] ? d->driver : "none",
            d->interface[0] ? d->interface : "none",
            d->ipv4[0] ? d->ipv4 : "none");
    }
    /* DHCP 就绪不代表互联网已验证；外网测试由独立脚本完成。 */
    printf("internet=not_tested\n");
    fflush(stdout);
}

static int find_disks(io_service_t dev, io_object_t media[])
{
    io_iterator_t it;
    if (IORegistryEntryCreateIterator(dev, kIOServicePlane,
            kIORegistryIterateRecursively, &it) != KERN_SUCCESS)
        return -1;
    int n = 0;
    bool overflow = false;
    io_object_t obj;
    while ((obj = IOIteratorNext(it))) {
        if (IOObjectConformsTo(obj, kIOMediaClass)) {
            CFTypeRef whole = IORegistryEntryCreateCFProperty(obj, CFSTR(kIOMediaWholeKey), kCFAllocatorDefault, 0);
            if (whole == kCFBooleanTrue) {
                if (n < MAX_DISKS) {
                    media[n++] = obj;
                    obj = IO_OBJECT_NULL;
                } else {
                    overflow = true;
                }
            }
            if (whole) CFRelease(whole);
        }
        if (obj) IOObjectRelease(obj);
    }
    IOObjectRelease(it);
    if (overflow) {
        for (int i = 0; i < n; i++) IOObjectRelease(media[i]);
        return -1;
    }
    return n;
}

struct unmount_result { bool done, abandoned; DAReturn status; };

static void unmount_cb(DADiskRef disk, DADissenterRef dissenter, void *ctx)
{
    struct unmount_result *r = ctx;
    (void)disk;
    r->done = true;
    r->status = dissenter ? DADissenterGetStatus(dissenter) : kDAReturnSuccess;
    if (dissenter)
        logmsg("卸载失败: 0x%x", DADissenterGetStatus(dissenter));
    if (r->abandoned) free(r);
}

static int try_unmount(DADiskRef disk, DADiskUnmountOptions options, double deadline)
{
    struct unmount_result *r = calloc(1, sizeof *r);
    if (!r) return -1;
    DADiskUnmount(disk, kDADiskUnmountOptionWhole | options, unmount_cb, r);
    while (!r->done && monotime() < deadline)
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, true);
    if (!r->done) {
        /* 回调可能晚于超时；上下文由迟到的回调释放，不引用已返回的栈。 */
        r->abandoned = true;
        return -1;
    }
    int ok = r->status == kDAReturnSuccess || r->status == kDAReturnNotMounted ? 1 :
        r->status == kDAReturnBusy ? 0 :
        r->status == kDAReturnNotPrivileged ? -3 : -2;
    free(r);
    return ok;
}

static bool same_media(DADiskRef disk, io_object_t media)
{
    io_service_t current = DADiskCopyIOMedia(disk);
    bool same = current && IOObjectIsEqualTo(current, media);
    if (current) IOObjectRelease(current);
    return same;
}

static bool force_allowed(int result, bool readonly, bool same)
{
    return result == 0 && readonly && same;
}

/* 本机实测 DA 直接调用被拒绝，而系统 diskutil 可由当前用户正常卸载。
   只在该错误下回退到系统工具，不使用 sudo、shell 或强制卸载选项。 */
static bool system_unmount(const char *bsd, double deadline)
{
    if (!bsd || monotime() >= deadline - 2) return false;
    char *args[] = { "/usr/sbin/diskutil", "unmountDisk", (char *)bsd, NULL };
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions)) return false;
    posix_spawn_file_actions_adddup2(&actions, STDERR_FILENO, STDOUT_FILENO);
    pid_t pid;
    int result = posix_spawn(&pid, args[0], &actions, NULL, args, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result) return false;
    double until = monotime() + 15;
    if (until > deadline - 1) until = deadline - 1;
    int status = 0;
    do {
        pid_t reaped = waitpid(pid, &status, WNOHANG);
        if (reaped == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (reaped < 0) return false;
        usleep(100000);
    } while (monotime() < until);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return false;
}

static bool unmount_disk(DASessionRef session, io_object_t media, double deadline)
{
    DADiskRef disk = DADiskCreateFromIOMedia(kCFAllocatorDefault, session, media);
    if (!disk) return false;
    bool readonly = false;
    CFDictionaryRef desc = DADiskCopyDescription(disk);
    if (desc) {
        readonly = CFDictionaryGetValue(desc, kDADiskDescriptionMediaWritableKey) == kCFBooleanFalse;
        CFRelease(desc);
    }
    if (!same_media(disk, media)) {
        CFRelease(disk);
        return false;
    }
    const char *name = DADiskGetBSDName(disk);
    logmsg("卸载目标设备卷 %s (%s)", name ? name : "?", readonly ? "只读" : "可写");
    double limit = monotime() + 15;
    if (limit > deadline) limit = deadline;
    int result = try_unmount(disk, kDADiskUnmountOptionDefault, limit);
    if (result == -3 && same_media(disk, media)) {
        logmsg("DiskArbitration 权限不足，改用系统 diskutil 正常卸载目标卷");
        result = system_unmount(name, deadline) ? 1 : -2;
    }
    if (force_allowed(result, readonly, same_media(disk, media))) {
        logmsg("只读安装光盘忙，尝试强制卸载；不对可写卷使用此操作");
        limit = monotime() + 10;
        if (limit > deadline) limit = deadline;
        result = try_unmount(disk, kDADiskUnmountOptionForce, limit);
    }
    CFRelease(disk);
    return result == 1;
}

static IOReturn send_switch(io_service_t dev)
{
    IOCFPlugInInterface **plugin = NULL;
    IOUSBDeviceInterface **usb = NULL;
    SInt32 score;
    IOReturn kr = IOCreatePlugInInterfaceForService(dev, kIOUSBDeviceUserClientTypeID,
        kIOCFPlugInInterfaceID, &plugin, &score);
    if (kr != kIOReturnSuccess || !plugin)
        return kr ? kr : kIOReturnError;
    HRESULT hr = (*plugin)->QueryInterface(plugin, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
        (LPVOID *)&usb);
    IODestroyPlugInInterface(plugin);
    if (hr || !usb) return kIOReturnError;
    /* 保留华为原服务的行为：接口已被存储驱动打开时仍可尝试控制请求。 */
    IOReturn open_kr = (*usb)->USBDeviceOpen(usb);
    if (open_kr != kIOReturnSuccess)
        logmsg("USBDeviceOpen: 0x%x，尝试设备控制请求", open_kr);
    IOUSBDevRequest req = {
        .bmRequestType = 0x40, .bRequest = 0xA1,
        .wValue = 0, .wIndex = 0, .wLength = 0, .pData = NULL,
    };
    kr = (*usb)->DeviceRequest(usb, &req);
    if (open_kr == kIOReturnSuccess) (*usb)->USBDeviceClose(usb);
    (*usb)->Release(usb);
    return kr;
}

static int wait_ready(int64_t location, double deadline)
{
    enum state last = QUERY_ERROR;
    do {
        struct device d;
        int count = scan(location, &d);
        enum state current = classify(count, &d);
        if (current != last) {
            logmsg("等待状态: %s", state_name(current));
            last = current;
        }
        if (current == READY || current == QUERY_ERROR || current == AMBIGUOUS || current == UNSUPPORTED) {
            report(count, &d);
            release_device(&d);
            return state_exit(current);
        }
        release_device(&d);
        usleep(250000);
    } while (monotime() < deadline);
    logmsg("等待超时: %s；未证明 USB 联网成功", state_name(last));
    return 5;
}

static int switch_one(void)
{
    double deadline = monotime() + WAIT_SECONDS - 1;
    struct device d;
    int count = scan(-1, &d);
    enum state s = classify(count, &d);
    report(count, &d);
    if (s != CDROM) {
        int64_t location = d.location;
        release_device(&d);
        if (s == WAIT_DRIVER || s == WAIT_INTERFACE || s == WAIT_ADDRESS)
            return wait_ready(location, deadline);
        return state_exit(s);
    }
    if (d.location < 0) {
        release_device(&d);
        return 1;
    }
    int64_t location = d.location;
    DASessionRef session = DASessionCreate(kCFAllocatorDefault);
    if (!session) {
        logmsg("无法创建 DiskArbitration 会话");
        release_device(&d);
        return 1;
    }
    DASessionScheduleWithRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    mach_timespec_t quiet = { 5, 0 };
    IOReturn quiet_result = IOServiceWaitQuiet(d.service, &quiet);
    io_object_t media[MAX_DISKS];
    int ndisks = quiet_result == kIOReturnSuccess ? find_disks(d.service, media) : -1;
    bool ok = ndisks >= 0;
    for (int i = 0; i < ndisks; i++) {
        if (ok) ok = unmount_disk(session, media[i], deadline);
        IOObjectRelease(media[i]);
    }
    DASessionUnscheduleFromRunLoop(session, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFRelease(session);
    if (!ok || monotime() >= deadline) {
        logmsg("未发送切换请求：设备或卷尚未就绪");
        release_device(&d);
        return 1;
    }
    /* 一次调用只发一条请求；重新枚举导致的请求错误仍须用实际网卡状态判定。 */
    IOReturn kr = send_switch(d.service);
    logmsg("已尝试一次切换请求，返回 0x%x", kr);
    release_device(&d);
    return wait_ready(location, deadline);
}

static int acquire_lock(void)
{
    char path[1024];
    size_t len = confstr(_CS_DARWIN_USER_TEMP_DIR, path, sizeof path);
    if (!len || len + 32 >= sizeof path) return -1;
    strlcat(path, "local.hilink-switch.lock", sizeof path);
    int fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        return -1;
    }
    return fd; /* 随进程退出释放锁；不删除锁文件，避免并发打开不同 inode。 */
}

static void timed_out(int signal_number)
{
    (void)signal_number;
    static const char message[] = "hilink-switch: 总操作超过 60 秒，退出；未确认成功\n";
    (void)write(STDERR_FILENO, message, sizeof message - 1);
    _exit(5);
}

int main(int argc, char *argv[])
{
    bool status = argc == 1 || (argc == 2 && !strcmp(argv[1], "--status"));
    bool background = argc == 2 && !strcmp(argv[1], "--launchd");
    bool action = argc == 2 && !strcmp(argv[1], "--switch");
    if (!status && !background && !action) {
        fprintf(stderr, "usage: %s [--status|--switch|--launchd]\n", argv[0]);
        return 2;
    }
    if (status) {
        struct device d;
        int count = scan(-1, &d);
        report(count, &d);
        int result = state_exit(classify(count, &d));
        release_device(&d);
        return result;
    }
    if (background) {
        dispatch_queue_t q = dispatch_queue_create("hilink-switch.events", NULL);
        xpc_set_event_stream_handler("com.apple.iokit.matching", q, ^(xpc_object_t event) {
            (void)event;
        });
    }
    int lockfd = acquire_lock();
    if (lockfd < 0) {
        logmsg("已有切换任务运行，或无法取得当前用户的互斥锁");
        return 75;
    }
    signal(SIGALRM, timed_out);
    alarm(WAIT_SECONDS);
    int result = switch_one();
    alarm(0);
    close(lockfd);
    /* 登录时未插入设备属正常状态；手动执行仍返回 absent。 */
    return background && result == 3 ? 0 : result;
}
