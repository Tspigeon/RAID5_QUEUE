def analyze_trace_intervals(file_path):
    prev_time = None
    min_interval = float('inf')
    max_interval = -float('inf')
    total_intervals = 0
    count = 0
    interval_count = 0
    gc_idle = 0

    with open(file_path, 'r') as file:
        for line in file:
            parts = line.strip().split()
            if not parts:
                continue  # 跳过空行
            current_time = int(parts[0])
            if prev_time is not None:
                interval = current_time - prev_time
                if interval > 30900000:
                    gc_idle+=1
                min_interval = min(min_interval, interval)
                max_interval = max(max_interval, interval)
                total_intervals += interval
                count += 1
            prev_time = current_time

    if count == 0:
        print("文件中的数据不足以计算间隔（需要至少两个请求）")
        return

    average_interval = total_intervals / count
    with open(file_path, 'r') as file:
        for line in file:
            parts = line.strip().split()
            if not parts:
                continue  # 跳过空行
            current_time = int(parts[0])
            if prev_time is not None:
                interval = current_time - prev_time
                if(interval > average_interval):
                    interval_count += 1
            prev_time = current_time
    print(f"最大间隔时间: {max_interval}")
    print(f"最小间隔时间: {min_interval}")
    print(f"平均间隔时间: {average_interval}")
    print(f"over average的间隔时间数: {interval_count/count}")
    print(f"可以进行一次gc的时间间隔: {gc_idle}")
    print(f"总间隔: {count}")
    # return average_interval



def analyze_write_intervals(file_path):
    # 解析文件内容
    write_times = []
    with open(file_path, 'r') as file:
        for line in file:
            parts = line.strip().split()
            if not parts:
                continue
            if len(parts) < 5:
                continue  # 跳过不完整的行
            req_type = parts[4]
            if req_type == '0':  # 写请求
                try:
                    timestamp = int(parts[0])
                    write_times.append(timestamp)
                except ValueError:
                    continue  # 无效时间戳，跳过
    
    # 计算间隔时间
    intervals = []
    for i in range(1, len(write_times)):
        intervals.append(write_times[i] - write_times[i-1])
    
    if not intervals:
        return "没有足够的写请求来计算间隔时间。"
    
    max_interval = max(intervals)
    min_interval = min(intervals)
    avg_interval = sum(intervals) / len(intervals)
    
    return {
        "max_interval": max_interval,
        "min_interval": min_interval,
        "avg_interval": avg_interval
    }

def calculate_update_intervals(file_path):
    # 数据结构：{lpn: [timestamp1, timestamp2, ...]}
    lpn_dict = {}

    # 读取数据并过滤写请求
    with open(file_path, 'r') as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) < 5:
                continue
            timestamp, _, lpn, _, operation = parts[:5]
            if operation == '0':  # 写操作
                lpn = int(lpn)
                timestamp = int(timestamp)
                if lpn not in lpn_dict:
                    lpn_dict[lpn] = []
                lpn_dict[lpn].append(timestamp)
                

    # 计算所有间隔
    total_intervals = 0
    sum_intervals = 0

    for lpn, timestamps in lpn_dict.items():
        if len(timestamps) < 2:
            continue  # 至少需要两次写入才能形成间隔
        
        # 按时间排序并计算相邻间隔
        timestamps.sort()
        for i in range(1, len(timestamps)):
            delta = timestamps[i] - timestamps[i-1]
            sum_intervals += delta
            total_intervals += 1

    # 计算平均值
    if total_intervals == 0:
        return 0
    return sum_intervals / total_intervals

def calculate_average_update_interval(file_path):
    with open(file_path, 'r') as f:
        last_write = {}  # 记录每个LPN最后一次写入的行号
        total_gap = 0  # 总间隔请求数
        count = 0  # 间隔次数
        global_index = 0  # 全局行号计数器（包含所有请求）

        for line in f:
            parts = line.strip().split()
            if len(parts) < 5:
                global_index += 1
                continue  # 跳过无效行但行号继续递增

            lpn = parts[2]
            op = parts[-1]

            if op == '0':  # 仅处理写请求
                if lpn in last_write:  # 说明是更新
                    # 计算间隔：当前行号 - 上次行号 - 1
                    if count == 0:
                        prev = global_index
                        count += 1
                        continue
                    gap = global_index - prev
                    if gap == 1:
                        a=1
                    total_gap += gap
                    count += 1
                    prev = global_index
                # 更新最后一次写入位置（无论是否首次）
                last_write[lpn] = global_index
                global_index += 1  # 行号严格递增

    return total_gap / count if count > 0 else 0


# 调用函数并打印结果
# average_interval = calculate_average_update_interval('./trace/1617_0.csv')
# print(f"平均更新请求间隔的请求数: {average_interval}")


result = analyze_write_intervals('./trace/src2_0zip10.csv')
if isinstance(result, dict):
    print(f"最大写间隔时间: {result['max_interval']}")
    print(f"最小写间隔时间: {result['min_interval']}")
    print(f"平均写间隔时间: {result['avg_interval']:.2f}")
else:
    print(result)
# avg_interval = calculate_update_intervals('./trace/ts_0.csv')
# print(f"平均更新间隔时间/平均间隔: {avg_interval/result} 时间单位")

