from importlib.metadata import files
import os
import sys
import numpy as np
import pandas as pd

page_size_in_sector = 16

# files = ['2016021614_0.csv', '2016021615_2.csv']
# folder_path = '/mnt/e/sortedTrace/'
# files = os.listdir(folder_path)

data_dict = {'Trace': [], 'Req_#': [], 'Write_Ratio': [], 'Avg_Write_Size': [], 'Avg_Read_Size': [],
             'Avg_Interval(μs)': [], 'FootPrint': [], 'Total_Size(GB)': []}

data_dict_Read = {'Trace': [], 'Req_#': [], 'Write_Ratio': [], 'Avg_Write_Size': [], 'Avg_Read_Size': [],
                  'Avg_Interval(μs)': [], 'FootPrint': [], 'Total_Size(GB)': []}



file_names = ["./trace/mds_0zip10.csv"]
# for file_path in files:
# file_names = []
# for file in files:
#     file_path = os.path.join(folder_path, file)
#     if file_path.endswith('.csv'):
#         print(file_path)
#         split_file = file_path.split('\\')
#         file_any = split_file[-1]
#         file_names.append(file_any)

# file_names = file_names[41:51]
# print(file_names)


for file in file_names:
    with open(file, "r") as f:
        line = f.readline()
        readCount = 0
        writeCount = 0
        writeSize = 0
        readSize = 0
        writeMap = {}
        readMap = {}
        startTime = 0
        endTime = 0
        WriteAmount = 0
        ReadAmount = 0
        footPrint = {}
        AllAccessCount = 0
        while line:
            ss = line.split()
            if len(ss) >= 3:
                if ss[4] == '0':
                    writeCount += 1
                    writeSize += int(ss[3])
                else:
                    readCount += 1
                    readSize += int(ss[3])
                endTime = int(ss[0])
                if startTime == 0:
                    startTime = int(ss[0])

                startLpn = int(int(ss[2]) / page_size_in_sector)
                endLpn = int(startLpn + int(ss[3]) / page_size_in_sector)

                # print(startLpn, endLpn)
                # break
                for i in range(startLpn, endLpn + 1, 1):
                    AllAccessCount += 1
                    if ss[4] == '0':
                        if writeMap.get(i):
                            writeMap[i] += 1
                        else:
                            writeMap[i] = 1
                        WriteAmount += 1
                    else:
                        if readMap.get(i):
                            readMap[i] += 1
                        else:
                            readMap[i] = 1
                        ReadAmount += 1
                    if footPrint.get(i):
                        footPrint[i] += 1
                    else:
                        footPrint[i] = 1

            line = f.readline()
        Req_Count = writeCount + readCount
        print("req #: ", Req_Count)
        print("req #: ", writeCount + readCount)
        Write_Ratio = round((float(writeCount / (writeCount + readCount) * 100)), 2)
        print("write ratio: ", Write_Ratio)
        print("write ratio: ", round((float(writeCount / (writeCount + readCount) * 100)), 2))
        Total_Size_ = round((float(writeSize + readSize) / 2 / 1024 / 1024), 2)
        print("Total_Size: ", float(writeSize + readSize) / 2 / 1024 / 1024)
        Avg_Write_Size = round((float(writeSize / writeCount) / 2), 2)
        print("write size（KB）: ", float(writeSize / writeCount) / 2)
        Avg_Read_Size = round((float(readSize / readCount) / 2), 2)
        print("read size（KB）: ", float(readSize / readCount) / 2)

        writeF = 0
        readF = 0
        hot_cold_line = 2
        values = writeMap.values()
        for v in values:
            if v >= hot_cold_line:
                writeF += 1

        values = readMap.values()
        for v in values:
            if v >= hot_cold_line:
                readF += 1
        print("write F: ", round((float(writeF / len(writeMap) * 100)), 2))
        print("read F: ", round((float(readF / len(readMap) * 100)), 2))

        print("time interval: ", endTime - startTime)
        Avg_Inter = round((float(float(endTime - startTime) / float(writeCount + readCount)) / 1000), 3)
        print("平均请求间隔(微妙):", Avg_Inter)

        print("平均请求间隔(微妙):", float(float(endTime - startTime) / float(writeCount + readCount)) / 1000)
        # print("write amount: ", (WriteAmount / 2.0)/1024/1024)
        # print("read amount: ", (ReadAmount / 2.0)/1024/1024)

        footPrint = sorted(footPrint.items(), key=lambda item: item[1], reverse=True)
        allAddress = len(footPrint)
        targetcount = AllAccessCount * 0.9
        print(AllAccessCount)
        print(WriteAmount, ReadAmount)
        FootPrint_ = round((float(len(footPrint)) * 8 / 1024 / 1024), 2)
        print("FootPrint/GB：", float(len(footPrint)) * 8 / 1024 / 1024)
        print("写FootPrint:", len(writeMap))
        print("读FootPrint:", len(readMap))
        nowCount = 0
        nowAddress = 0
        for key, value in footPrint:
            nowAddress += 1
            nowCount += value
            if nowCount >= targetcount:
                break
        print(nowAddress / float(allAddress))



        # trace_name = file[6:-4]
        # print(file)
        trace_name = file
        print(trace_name)
        print(Write_Ratio)
        if Write_Ratio > 50.0:
            print("xx")
            data_dict['Trace'].append(trace_name)
            data_dict['Req_#'].append(Req_Count)
            data_dict['Write_Ratio'].append(Write_Ratio)
            data_dict['Avg_Write_Size'].append(Avg_Write_Size)
            data_dict['Avg_Read_Size'].append(Avg_Read_Size)
            data_dict['Avg_Interval(μs)'].append(Avg_Inter)
            data_dict['FootPrint'].append(FootPrint_)
            data_dict['Total_Size(GB)'].append(Total_Size_)
            print(data_dict)
        else:
            data_dict_Read['Trace'].append(trace_name)
            data_dict_Read['Req_#'].append(Req_Count)
            data_dict_Read['Write_Ratio'].append(Write_Ratio)
            data_dict_Read['Avg_Write_Size'].append(Avg_Write_Size)
            data_dict_Read['Avg_Read_Size'].append(Avg_Read_Size)
            data_dict_Read['Avg_Interval(μs)'].append(Avg_Inter)
            data_dict_Read['FootPrint'].append(FootPrint_)
            data_dict_Read['Total_Size(GB)'].append(Total_Size_)

print(data_dict)
print(data_dict_Read)

df = pd.DataFrame(data_dict)
df_Read = pd.DataFrame(data_dict_Read)


# merged_df = pd.concat([df, pd.DataFrame(columns=[f'Column_{i}' for i in range(5)]), df_Read], axis=1)
with pd.ExcelWriter('Trace_Analysis__.xlsx', engine='openpyxl') as writer:
    df.to_excel(writer, sheet_name='Sheet1', startcol=0, startrow=0, index=False,
                header=['Trace', 'Req_#', 'Write_Ratio', 'Avg_Write_Size', 'Avg_Read_Size', 'Avg_Interval(μs)',
                        'FootPrint', 'Total_Size(GB)'])
    df_Read.to_excel(writer, sheet_name='Sheet2', startcol=0, startrow=0, index=False,
                header=['Trace', 'Req_#', 'Write_Ratio', 'Avg_Write_Size', 'Avg_Read_Size', 'Avg_Interval(μs)',
                        'FootPrint', 'Total_Size(GB)'])

# with pd.ExcelWriter('Trace_Analysis_.xlsx', engine='openpyxl') as writer:
#     df_Read.to_excel(writer, sheet_name='Sheet1', startcol=12, startrow=0, index=False,
#                      header=['Trace', 'Req_#', 'Write_Ratio', 'Avg_Write_Size', 'Avg_Read_Size', 'Avg_Interval(μs)',
#                      'FootPrint', 'Total_Size(GB)'])






