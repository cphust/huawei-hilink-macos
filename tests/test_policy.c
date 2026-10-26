#define main hilink_cli_main
#include "../hilink-switch.c"
#undef main
#include <assert.h>

int main(void)
{
    struct device d = { .pid = 0x14db, .ecm = true, .interfaces = 1, .link = true };
    strcpy(d.interface, "en99");
    strcpy(d.ipv4, "192.168.8.100");
    assert(classify(1, &d) == READY);
    /* 上游的误判回归：即使保留旧快照，设备消失也不能判为成功。 */
    assert(classify(0, &d) == ABSENT);
    assert(classify(-1, &d) == QUERY_ERROR);
    assert(classify(2, &d) == AMBIGUOUS);
    d.pid = CDROM_PID;
    assert(classify(1, &d) == CDROM);
    d.pid = 0x14db;
    d.ecm = false;
    assert(classify(1, &d) == UNSUPPORTED);
    d.interfaces = 0;
    assert(classify(1, &d) == WAIT_DRIVER);
    d.ecm = true;
    assert(classify(1, &d) == WAIT_INTERFACE);
    d.interfaces = 1;
    d.ipv4[0] = 0;
    assert(classify(1, &d) == WAIT_ADDRESS);
    strcpy(d.ipv4, "192.168.8.100");
    d.link = false;
    assert(classify(1, &d) == WAIT_ADDRESS);
    d.interfaces = 2;
    assert(classify(1, &d) == AMBIGUOUS);
    assert(!usable_ipv4(inet_addr("169.254.8.1")));
    assert(!usable_ipv4(inet_addr("0.0.0.0")));
    assert(usable_ipv4(inet_addr("192.168.8.100")));
    assert(!force_allowed(0, false, true));
    assert(!force_allowed(-1, true, true));
    assert(!force_allowed(-3, true, true));
    assert(!force_allowed(0, true, false));
    assert(force_allowed(0, true, true));
    /* 两个进程入口使用同一把锁；测试不接触 USB 或磁盘卸载。 */
    int fd = acquire_lock();
    assert(fd >= 0);
    assert(acquire_lock() < 0);
    close(fd);
    fd = acquire_lock();
    assert(fd >= 0);
    close(fd);
    puts("PASS: 状态判定、可写卷保护、超时/权限分支、互斥锁");
    return 0;
}
