#include"../config.h"
#ifdef CONFIG_RESNET_FP32_TO_POSIT32

#include <verilated.h>
#include <verilated_vcd_c.h>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <iostream>
#include "VPvuTop.h"
#include "../../SoftPosit/source/include/softposit.h"

//---------------- 配置参数 -------------------
#define OP   7   // op7对应fp32到posit32的转换
const char* FP32_FILE     = "./test_src/fp32_data.bin";
const char* GOLDEN_FILE   = "./test_src/posit32_data.bin";
const int SAMPLE_NUM      = 1000;                           // 与生成数据一致
const char* WAVEFORM_FILE = "waveform.vcd";                 // 波形输出文件
const int VECTOR_SIZE     = 4;                              // 设置向量大小为4
//--------------------------------------------

struct TestData {
    float fp32_inputs[SAMPLE_NUM];       // IEEE-754浮点数输入
    uint32_t golden_posit[SAMPLE_NUM];   // 预期的Posit32输出
};

TestData load_testdata() {
    TestData td;

    // 读取FP32输入数据
    std::ifstream fp32_file(FP32_FILE, std::ios::binary);
    if (!fp32_file.is_open()) {
        std::cerr << "无法打开FP32数据文件: " << FP32_FILE << std::endl;
        exit(EXIT_FAILURE);
    }
    fp32_file.read(reinterpret_cast<char*>(td.fp32_inputs), sizeof(float) * SAMPLE_NUM);
    fp32_file.close();

    // 读取预期的Posit32输出数据
    std::ifstream posit_file(GOLDEN_FILE, std::ios::binary);
    if (!posit_file.is_open()) {
        std::cerr << "无法打开Posit32数据文件: " << GOLDEN_FILE << std::endl;
        exit(EXIT_FAILURE);
    }
    posit_file.read(reinterpret_cast<char*>(td.golden_posit), sizeof(uint32_t) * SAMPLE_NUM);
    posit_file.close();

    return td;
}

bool posit_equal(uint32_t a, uint32_t b) {
    if (a == b) return true;

    // 允许1ULP误差
    int32_t ia = static_cast<int32_t>(a);
    int32_t ib = static_cast<int32_t>(b);
    return std::abs(ia - ib) <= 1;
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    
    // 启用波形跟踪
    Verilated::traceEverOn(true);

    VPvuTop* top = new VPvuTop;

    // 创建 VCD 文件对象用于波形输出
    VerilatedVcdC* tfp = new VerilatedVcdC;
    top->trace(tfp, 99);  // 设置波形深度
    tfp->open(WAVEFORM_FILE);  // 打开波形输出文件

    // 加载测试数据
    TestData td = load_testdata();

    // 初始化信号
    top->clock = 0;
    top->reset = 1;
    top->eval();

    // 复位序列（2周期）
    for (int i = 0; i < 2; ++i) {
        top->clock ^= 1;
        top->eval();
        tfp->dump(i);  // 转储波形数据
    }
    top->reset = 0;

    size_t errors = 0;
    // 向量模式下，我们需要处理的样本数量是SAMPLE_NUM/VECTOR_SIZE（向上取整）
    const size_t vector_iterations = (SAMPLE_NUM + VECTOR_SIZE - 1) / VECTOR_SIZE;
    const size_t total_tests = vector_iterations * VECTOR_SIZE;

    // 测试主循环，每次处理VECTOR_SIZE个数据
    for (size_t i = 0; i < vector_iterations; ++i) {
        uint32_t fp32_bits[VECTOR_SIZE] = {0};
        uint32_t expected_posit[VECTOR_SIZE] = {0};
        
        // 准备当前向量的输入数据和预期输出
        for (int j = 0; j < VECTOR_SIZE; ++j) {
            size_t idx = i * VECTOR_SIZE + j;
            if (idx < SAMPLE_NUM) {
                float fp32_input = td.fp32_inputs[idx];
                expected_posit[j] = td.golden_posit[idx];
                
                // 将FP32浮点数的位模式转为uint32_t
                std::memcpy(&fp32_bits[j], &fp32_input, sizeof(uint32_t));
            } else {
                // 如果超出范围，使用默认值
                fp32_bits[j] = 0;
                expected_posit[j] = 0;
            }
        }

        // 设置FP32输入数据
        top->io_float_i_0 = fp32_bits[0];
        top->io_float_i_1 = fp32_bits[1];
        top->io_float_i_2 = fp32_bits[2];
        top->io_float_i_3 = fp32_bits[3];

        // 设置为0，因为此处不需要
        top->io_float_i2_0 = 0;
        top->io_float_i2_1 = 0;
        top->io_float_i2_2 = 0;
        top->io_float_i2_3 = 0;
        
        top->io_posit_i1_0 = 0;
        top->io_posit_i1_1 = 0;
        top->io_posit_i1_2 = 0;
        top->io_posit_i1_3 = 0;
        
        top->io_posit_i2_0 = 0;
        top->io_posit_i2_1 = 0;
        top->io_posit_i2_2 = 0;
        top->io_posit_i2_3 = 0;

        // 设置op代码和配置参数
        top->io_op          = OP;
        top->io_Isposit     = false;  // 输入是IEEE-754
        top->io_Outposit    = true;   // 输出是Posit
        top->io_float_mode  = 3;      // IEEE-754模式
        top->io_float_posit = true;

        // 设置数据位宽
        top->io_src_posit_width = 32;           // 32位FP32
        top->io_dst_posit_width = 32;           // 32位Posit
        top->io_vector_size     = VECTOR_SIZE;  // 向量大小为4
        
        // 运行一个时钟周期
        top->clock = 1;
        top->eval();
        tfp->dump(i*2 + 1);  // 转储波形数据

        top->clock = 0;
        top->eval();
        tfp->dump(i*2 + 2);  // 转储波形数据

        // 获取硬件计算的Posit32结果
        uint32_t hw_result[VECTOR_SIZE];
        hw_result[0] = top->io_posit_o_0;
        hw_result[1] = top->io_posit_o_1;
        hw_result[2] = top->io_posit_o_2;
        hw_result[3] = top->io_posit_o_3;

        // 比较每个向量元素的结果
        for (int j = 0; j < VECTOR_SIZE; ++j) {
            size_t idx = i * VECTOR_SIZE + j;
            if (idx >= SAMPLE_NUM) continue;  // 超出范围的样本不进行比较
            
            if (!posit_equal(hw_result[j], expected_posit[j])) {
                float fp32_input = td.fp32_inputs[idx];
                
                std::cerr << "样本 " << idx << " 不匹配\n"
                          << "  向量位置: " << j << "\n"
                          << "  输入FP32: " << fp32_input << " (0x" << std::hex << fp32_bits[j] << ")\n"
                          << "  硬件输出: 0x" << std::hex << hw_result[j] << "\n"
                          << "  预期输出: 0x" << std::hex << expected_posit[j] << "\n";
                errors++;
            }
        }

        // 进度显示
        if ((i+1) % 25 == 0) {  // 每25个向量显示一次（对应约100个样本）
            std::cout << "已测试 " << std::min((i+1)*VECTOR_SIZE, (size_t)SAMPLE_NUM) << "/" << SAMPLE_NUM 
                      << " (" << (std::min((i+1)*VECTOR_SIZE, (size_t)SAMPLE_NUM)*100/SAMPLE_NUM) << "%)" 
                      << std::endl;
        }
    }

    // 资源清理
    tfp->close();  // 关闭波形文件
    top->final();
    delete top;
    delete tfp;

    // 结果报告
    std::cout << "\n验证结果\n========="
              << "\nFP32到Posit32向量转换测试完成！"
              << "\n总样本数: " << SAMPLE_NUM
              << "\n向量宽度: " << VECTOR_SIZE
              << "\n错误数量: " << errors
              << "\n错误率:   " << std::fixed << std::setprecision(2)
              << (errors*100.0/SAMPLE_NUM) << "%\n";

    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif 