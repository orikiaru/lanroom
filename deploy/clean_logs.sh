#!/bin/bash
# 由 crontab 定时跑。单个日志文件超过上限就清空；logs 目录总量超过上限，从最早的日期目录开始删，当天的不删。
# 写日志的进程一直开着文件，删掉不会释放空间，所以单个文件只清空不删。
set -u

LIMIT=$((1024 * 1024 * 1024))
LOG_ROOT=/home/jiangxueyang/logs
APP=/home/jiangxueyang/lanroom

for file in "${APP}"/*.log "${APP}/logd/logd.out" "${LOG_ROOT}"/*/*; do
	[ -f "${file}" ] || continue
	size=$(stat -c %s "${file}")
	if [ "${size}" -gt "${LIMIT}" ]; then
		: > "${file}"
		echo "$(date '+%F %T') truncate ${file} ${size}"
	fi
done

today=$(ls -1d "${LOG_ROOT}"/*/ 2>/dev/null | sort | tail -n 1)
for dir in $(ls -1d "${LOG_ROOT}"/*/ 2>/dev/null | sort); do
	total=$(du -sb "${LOG_ROOT}" | cut -f1)
	if [ "${total}" -le "${LIMIT}" ] || [ "${dir}" = "${today}" ]; then
		break
	fi
	rm -rf "${dir}"
	echo "$(date '+%F %T') remove ${dir} total=${total}"
done
