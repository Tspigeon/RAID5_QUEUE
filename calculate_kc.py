# calculate_ssd_params.py
import sys
import pandas as pd
import numpy as np
from scipy.optimize import curve_fit
import json


def model(x, k, c):
    """拟合模型: y = 1 - exp(k * x^c)"""
    return 1 - np.exp(k * (x ** c))


def calculate_ssd_params(input_csv, output_json):
    """
    从CSV文件读取SSD数据，计算拟合参数k和c

    输入CSV格式:
    第一列: SSD编号 (如 1, 2, 3, 4)
    第二列: CacheSize
    第三列: HitRate
    """
    try:
        # 读取CSV文件，不需要表头
        df = pd.read_csv(input_csv, header=None)

        # 确保有三列
        if df.shape[1] < 3:
            print(f"错误: CSV文件需要至少3列，当前有{df.shape[1]}列")
            return False

        # 重命名列
        df.columns = ["SSD", "CacheSize", "HitRate"]

        # 获取所有SSD编号
        ssd_numbers = df["SSD"].unique()

        if len(ssd_numbers) == 0:
            print("错误: 未找到SSD数据")
            return False

        # 为每个SSD计算拟合参数
        results = {}

        for ssd_num in ssd_numbers:
            ssd_str = str(int(ssd_num))  # 确保是整数并转换为字符串

            try:
                # 提取该SSD的数据
                data = df[df["SSD"] == ssd_num]

                if len(data) < 2:
                    print(f"警告: SSD {ssd_str} 数据点不足 ({len(data)}个)，跳过")
                    continue

                x = data["CacheSize"].values
                y = data["HitRate"].values

                # 拟合模型
                popt, _ = curve_fit(
                    model,
                    x, y,
                    p0=[-0.01, 1.0],
                    bounds=([-np.inf, 0], [0, np.inf]),
                    maxfev=10000
                )

                k, c = popt

                y_pred = model(x, k, c)
                residuals = y - y_pred
                mae = np.mean(np.abs(residuals))

                # 保存结果
                results[ssd_str] = {
                    "k": float(k),
                    "c": float(c),
                    "e": float(mae)
                }

                print(f"SSD {ssd_str}: k={k:.6f}, c={c:.6f}")

            except Exception as e:
                print(f"警告: SSD {ssd_str} 拟合失败: {e}")
                continue

        if not results:
            print("错误: 所有SSD拟合都失败")
            return False

        # 直接保存results到JSON（不需要额外的包装）
        with open(output_json, 'w', encoding='utf-8') as f:
            json.dump(results, f, indent=2, ensure_ascii=False)

        # print(f"\n计算完成！结果已保存到: {output_json}")
        # print(f"成功拟合 {len(results)} 个SSD")
        return True

    except FileNotFoundError:
        print(f"错误: 文件未找到: {input_csv}")
        return False

    except Exception as e:
        print(f"错误: {e}")
        return False


def main():
    """主函数：从命令行参数获取输入输出文件"""
    if len(sys.argv) != 3:
        print("用法: python calculate_ssd_params.py <输入CSV文件> <输出JSON文件>")
        print("\n输入CSV文件格式:")
        print("  第一列: SSD编号 (如 1, 2, 3, 4)")
        print("  第二列: CacheSize (缓存大小)")
        print("  第三列: HitRate (命中率)")
        print("\n示例内容:")
        print("  1,1.0,0.1")
        print("  1,2.0,0.2")
        print("  2,1.0,0.15")
        print("  2,2.0,0.25")
        sys.exit(1)

    input_file = sys.argv[1]
    output_file = sys.argv[2]

    if not calculate_ssd_params(input_file, output_file):
        sys.exit(1)


if __name__ == "__main__":
    main()