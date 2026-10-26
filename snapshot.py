#!/usr/bin/env python3
"""只读记录网络与 NoMachine 状态；文件只允许当前用户读取。"""
import datetime
import json
import os
from pathlib import Path
import plistlib
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def run(*args):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=15)
        return {"code": p.returncode, "stdout": p.stdout, "stderr": p.stderr}
    except (OSError, subprocess.TimeoutExpired) as exc:
        return {"code": -1, "stderr": str(exc)}


def snapshot():
    with open('/Library/Preferences/SystemConfiguration/preferences.plist', 'rb') as f:
        p = plistlib.load(f)
    services = {}
    for key, service in p.get('NetworkServices', {}).items():
        interface = service.get('Interface', {})
        services[key] = {
            'name': service.get('UserDefinedName'),
            'interface': interface.get('DeviceName'),
            'hardware': interface.get('Hardware'),
            'IPv4': service.get('IPv4', {}),
            'IPv6': service.get('IPv6', {}),
            'DNS': service.get('DNS', {}),
        }
    current = p.get('CurrentSet', '').rsplit('/', 1)[-1]
    order = p.get('Sets', {}).get(current, {}).get('Network', {}).get('Global', {}).get('IPv4', {}).get('ServiceOrder', [])
    return {
        'time': datetime.datetime.now().astimezone().isoformat(),
        'services': services,
        'order': order,
        'en0': run('/sbin/ifconfig', 'en0'),
        'en1': run('/sbin/ifconfig', 'en1'),
        'nomachine': run('/usr/sbin/lsof', '-nP', '-iTCP:4000'),
        'remote_route': run('/sbin/route', '-n', 'get', '192.168.0.69'),
        'default_route': run('/sbin/route', '-n', 'get', 'default'),
        'router_route': run('/sbin/route', '-n', 'get', '192.168.8.1'),
        'dns': run('/usr/sbin/scutil', '--dns'),
        'network_state': run('/usr/sbin/scutil', '--nwi'),
        'usb': run(str(ROOT / 'build/hilink-switch'), '--status'),
    }


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: python3 snapshot.py artifacts/before.json')
    path = Path(sys.argv[1])
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    data = snapshot()
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(path)
    route = data['remote_route']
    print(route.get('stdout', route.get('stderr')))
    if route['code'] != 0:
        sys.exit('快照已保存，但路由查询失败，不能据此证明远程链路保持不变。')
