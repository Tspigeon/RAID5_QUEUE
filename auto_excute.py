import subprocess

def run_c_program(program_path, input_file=None):
    command = [program_path]+input_file
    print(command)

    try:
        #print("ssd:"+program_path+" trace: "+input_file[1])
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,bufsize=1, universal_newlines=True)
        while True:
             process.stdin.flush()
             output = process.stdout.readline()
             if output == '' and process.poll() is not None:
                break
             print(output,end="",flush=True)
        if result.returncode == 0:
            print("程序成功执行")
        else:
            print(f"程序执行失败，返回码: {result.returncode}")
            print("trace: "+ input_file[2])
            print("错误输出:\n", result.stderr)
    except Exception as e:
        print(f"发生错误: {e}")
        print("trace: "+ input_file[2])

if __name__ == "__main__":
    c_program_path = "./ssd"  # 替换为你的C语言程序的路径
    arg = [
        ["1","./trace/1616_0.csv","4"],   #128mb/4(final num)
        # ["1","trace/1617_4.csv","16"],
        # ["1","trace/1620_4.csv","16"],
    ]
    result = subprocess.run(["make","clean"], shell=False, text=True, capture_output=True)
    print("程序输出:\n", result.stdout)
    result = subprocess.run(["make","all"], shell=False, text=True, capture_output=True)
    print("程序输出:\n", result.stdout)
    for i in arg:
        run_c_program(c_program_path,i)
