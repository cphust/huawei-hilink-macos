#!/bin/sh
set -eu
cd "$(dirname "$0")"
sh build.sh
xcrun clang -O0 -g -Wall -Wextra -Werror -arch arm64 \
    -o build/test-policy tests/test_policy.c \
    -framework IOKit -framework CoreFoundation -framework DiskArbitration
./build/test-policy
if ./build/hilink-switch --invalid >build/invalid-arguments.txt 2>&1; then
    echo 'FAIL: 非法参数应拒绝执行' >&2
    exit 1
else
    test "$?" -eq 2
fi
for script in build.sh test.sh status.sh install.sh uninstall.sh; do
    sh -n "$script"
done
plutil -lint local.hilink-switch.plist.in
/usr/bin/python3 - <<'PY'
import py_compile
for name in ('check.py', 'snapshot.py'):
    py_compile.compile(name, cfile='build/' + name + '.pyc', doraise=True)
print('PASS: Python 脚本语法')
PY
