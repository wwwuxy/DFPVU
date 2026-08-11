def remove_last_lines(file_path, lines_to_remove=3):
    """
    移除指定文件末尾的若干行。

    参数：
    - file_path (str): 要处理的文件路径。
    - lines_to_remove (int): 要移除的末尾行数，默认为 3。
    """
    try:
        # 以读取模式打开文件并读取所有行
        with open(file_path, 'r') as file:
            lines = file.readlines()

        # 打印原始行数（可选，用于调试）
        print(f"原始行数: {len(lines)}")

        # 移除指定数量的尾部行
        if lines_to_remove > 0:
            lines = lines[:-lines_to_remove]
            print(f"移除最后 {lines_to_remove} 行后，剩余行数: {len(lines)}")

        # 将修改后的内容写回文件
        with open(file_path, 'w') as file:
            file.writelines(lines)

        print(f"成功移除文件 '{file_path}' 的最后 {lines_to_remove} 行。")
    except FileNotFoundError:
        print(f"错误: 文件 '{file_path}' 未找到。")
    except Exception as e:
        print(f"发生错误: {e}")

if __name__ == "__main__":
    # 指定要处理的文件路径和要移除的行数
    target_file = './vsrc/PvuTop.sv'
    lines_to_remove = 3

    # 调用函数移除尾部行
    remove_last_lines(target_file, lines_to_remove)

