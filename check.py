#!/usr/bin/env python3
"""绑定华为 USB 接口的低流量检查，不使用系统 DNS 或环境代理，不改变网络配置。"""
import argparse
import datetime
import json
import os
from pathlib import Path
import secrets
import socket
import ssl
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
# macOS SDK netinet/in.h 的 IP_BOUND_IF；系统 Python 没有导出此常量。
IP_BOUND_IF = 25


def command(*args):
    p = subprocess.run(args, capture_output=True, text=True, timeout=15)
    if p.returncode:
        raise RuntimeError(p.stderr.strip() or p.stdout.strip() or str(args))
    return p.stdout


def bind_usb(kind, interface, address):
    s = socket.socket(socket.AF_INET, kind)
    try:
        s.settimeout(6)
        index = socket.if_nametoindex(interface)
        s.setsockopt(socket.IPPROTO_IP, IP_BOUND_IF, index)
        s.bind((address, 0))
        if s.getsockopt(socket.IPPROTO_IP, IP_BOUND_IF) != index:
            raise RuntimeError('接口绑定未生效')
        return s
    except Exception:
        s.close()
        raise


def skip_name(data, offset):
    while True:
        size = data[offset]
        if size & 0xc0 == 0xc0:
            return offset + 2
        offset += 1
        if size == 0:
            return offset
        if size > 63:
            raise ValueError('DNS 名称格式不支持')
        offset += size


def dns_lookup(interface, address, server, name):
    identifier = secrets.randbelow(65536)
    question = b''.join(bytes([len(x)]) + x.encode('ascii') for x in name.split('.')) + b'\0'
    packet = struct.pack('!HHHHHH', identifier, 0x0100, 1, 0, 0, 0) + question + struct.pack('!HH', 1, 1)
    with bind_usb(socket.SOCK_DGRAM, interface, address) as s:
        s.connect((server, 53))
        s.send(packet)
        data = s.recv(4096)
    ident, flags, questions, answers, _, _ = struct.unpack('!HHHHHH', data[:12])
    if ident != identifier or not flags & 0x8000 or flags & 0x020f:
        raise RuntimeError('DNS 响应错误或被截断')
    pos = 12
    for _ in range(questions):
        pos = skip_name(data, pos) + 4
    ips = []
    for _ in range(answers):
        pos = skip_name(data, pos)
        kind, family, _, length = struct.unpack('!HHIH', data[pos:pos + 10])
        pos += 10
        if kind == 1 and family == 1 and length == 4:
            ips.append(socket.inet_ntoa(data[pos:pos + length]))
        pos += length
    if not ips:
        raise RuntimeError('DNS 未返回 IPv4 地址')
    return ips


def http_head(interface, address, destination, host, secure):
    port = 443 if secure else 80
    s = bind_usb(socket.SOCK_STREAM, interface, address)
    try:
        s.connect((destination, port))
        if secure:
            s = ssl.create_default_context().wrap_socket(s, server_hostname=host)
        s.sendall(('HEAD / HTTP/1.1\r\nHost: ' + host +
                   '\r\nUser-Agent: HiLinkCheck/1.0\r\nConnection: close\r\n\r\n').encode('ascii'))
        first = s.recv(4096).split(b'\r\n', 1)[0].decode('ascii', errors='replace')
        parts = first.split()
        if len(parts) < 2 or not parts[0].startswith('HTTP/') or not parts[1].isdigit():
            raise RuntimeError('未获得有效 HTTP 响应: ' + first)
        if int(parts[1]) >= 500:
            raise RuntimeError('HTTP 服务端错误: ' + first)
        return {'status': first, 'local': s.getsockname()[0], 'remote': destination, 'interface': interface}
    finally:
        s.close()


def run_check():
    state = dict(line.split('=', 1) for line in command(str(ROOT / 'build/hilink-switch'), '--status').splitlines() if '=' in line)
    if state.get('state') != 'address_ready':
        raise RuntimeError('USB 尚未获得地址')
    interface, address = state['interface'], state['ipv4']
    remote = command('/sbin/route', '-n', 'get', '192.168.0.69')
    if 'interface: en0' not in remote:
        raise RuntimeError('NoMachine 对端路由不再是 en0，停止检查')
    en0 = command('/sbin/ifconfig', 'en0')
    if 'inet 192.168.0.34 netmask 0xffffff00' not in en0 or 'status: active' not in en0:
        raise RuntimeError('en0 状态与保护条件不符')
    gateway = command('/usr/sbin/ipconfig', 'getoption', interface, 'router').split()[0]
    dns_server = command('/usr/sbin/ipconfig', 'getoption', interface, 'domain_name_server').split()[0]
    management = http_head(interface, address, gateway, gateway, False)
    # 固定少量公网测试，不下载网页正文；不将系统代理的 Fake IP 作为目标。
    failures = []
    for host in ('www.apple.com.cn', 'www.baidu.com'):
        try:
            ips = dns_lookup(interface, address, dns_server, host)
            for ip in ips[:2]:
                try:
                    scope = command('/sbin/route', '-n', 'get', '-ifscope', interface, ip)
                    if 'interface: ' + interface not in scope:
                        raise RuntimeError('公网目标没有 USB 接口路由')
                    internet = http_head(interface, address, ip, host, True)
                    return {'time': datetime.datetime.now().astimezone().isoformat(),
                            'state': state, 'management': management, 'dns_server': dns_server,
                            'dns_name': host, 'dns_answers': ips, 'https': internet,
                            'remote_route_interface': 'en0',
                            'isolation_test': 'pending_wifi_and_vpn_remain_enabled'}
                except (OSError, ValueError, RuntimeError) as exc:
                    failures.append(host + ' ' + ip + ': ' + str(exc))
        except (OSError, ValueError, RuntimeError) as exc:
            failures.append(host + ': ' + str(exc))
    raise RuntimeError('; '.join(failures))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--duration', type=int, default=0, choices=range(0, 601), metavar='0..600',
                        help='持续检查秒数，每 60 秒检查一次，默认单次')
    parser.add_argument('--output', type=Path, help='以 JSONL 保存结果；不覆盖既有记录')
    args = parser.parse_args()
    out = None
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        out = os.fdopen(os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), 'w')
    started, next_check = time.monotonic(), time.monotonic()
    try:
        while True:
            result = run_check()
            text = json.dumps(result, ensure_ascii=False)
            print(text, flush=True)
            if out:
                out.write(text + '\n')
                out.flush()
            elapsed = time.monotonic() - started
            if not args.duration or elapsed >= args.duration:
                return 0
            next_check = min(next_check + 60, started + args.duration)
            time.sleep(max(0, next_check - time.monotonic()))
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired, struct.error, IndexError) as exc:
        print('CHECK FAILED: ' + str(exc), file=sys.stderr, flush=True)
        return 1
    finally:
        if out:
            out.close()


if __name__ == '__main__':
    sys.exit(main())
