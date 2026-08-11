#include"../config.h"
#ifdef CONFIG_RESNET_MUL_P32

#include <verilated.h>
#include <verilated_vcd_c.h>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <iostream>
#include "VPvuTop.h"
#include "../../SoftPosit/source/include/softposit.h"

//---------------- 配置参数 -------------------
#define OP   3
const char* ACT_FILE    = "./test_src/posit_activations.bin";
const char* WEIGHT_FILE = "./test_src/posit_weights.bin";
const char* GOLDEN_FILE = "./test_src/mul_results.bin";
const int SAMPLE_NUM    = 9000;  // 与生成数据一致
const char* WAVEFORM_FILE = "waveform.vcd";  // 波形输出文件
//--------------------------------------------

struct TestData {
    uint32_t activations[SAMPLE_NUM][4]; 
    uint32_t weights[SAMPLE_NUM][4];     
    uint32_t golden[SAMPLE_NUM][4];          
};

// 跳过 fa0 并读取有效数据
uint32_t read_valid_data(std::ifstream& file) {
    uint32_t data;
    while (file.read(reinterpret_cast<char*>(&data), sizeof(uint32_t))) {
        if (data != 0xfa0) {  // 忽略头部信息 fa0
            return data;
        }
    }
    return 0;  // 如果读取失败，返回 0
}

TestData load_testdata() {
    TestData td;

    // 读取激活数据
    std::ifstream act(ACT_FILE, std::ios::binary);
    if (!act.is_open()) {
        std::cerr << "无法打开激活数据文件: " << ACT_FILE << std::endl;
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < SAMPLE_NUM; ++i) {
        for (int j = 0; j < 4; ++j) {
            td.activations[i][j] = read_valid_data(act);
        }
    }

    // 读取权重数据
    std::ifstream weight(WEIGHT_FILE, std::ios::binary);
    if (!weight.is_open()) {
        std::cerr << "无法打开权重数据文件: " << WEIGHT_FILE << std::endl;
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < SAMPLE_NUM; ++i) {
        for (int j = 0; j < 4; ++j) {
            td.weights[i][j] = read_valid_data(weight);
        }
    }

    // 读取预期输出数据
    std::ifstream golden(GOLDEN_FILE, std::ios::binary);
    if (!golden.is_open()) {
        std::cerr << "无法打开预期输出数据文件: " << GOLDEN_FILE << std::endl;
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < SAMPLE_NUM; ++i) {
        for (int j = 0; j < 4; ++j) {
            td.golden[i][j] = read_valid_data(golden);
        }
    }

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
    const size_t total = SAMPLE_NUM;

    // 测试主循环
    for (size_t i = 0; i < total; ++i) {
        // 设置posit输入数据
        uint32_t* act = td.activations[i];
        uint32_t* weight = td.weights[i];
        uint32_t* golden = td.golden[i];

        top->io_posit_i1_0 = act[0];
        top->io_posit_i1_1 = act[1];
        top->io_posit_i1_2 = act[2];
        top->io_posit_i1_3 = act[3];
        
        top->io_posit_i2_0 = weight[0];
        top->io_posit_i2_1 = weight[1];
        top->io_posit_i2_2 = weight[2];
        top->io_posit_i2_3 = weight[3];
        

        //设置float输入数据
        top->io_float_i_0 = 0;
        top->io_float_i_1 = 0;
        top->io_float_i_2 = 0;
        top->io_float_i_3 = 0;

        top->io_float_i2_0 = 0;
        top->io_float_i2_1 = 0;
        top->io_float_i2_2 = 0;
        top->io_float_i2_3 = 0;


        //设置信号量
        top->io_op = OP;
        top->io_Isposit = true;
        top->io_Outposit = true;
        top->io_float_mode = 3;
        top->io_float_posit = true;
        
        //设置数据位宽
        top->io_src_posit_width = 32;
        top->io_dst_posit_width = 32;
        top->io_vector_size = 4;


        // 运行一次点积计算
        top->clock = 1;
        top->eval();
        tfp->dump(i*2 + 1);  // 转储波形数据

        top->clock = 0;
        top->eval();
        tfp->dump(i*2 + 2);  // 转储波形数据

        // 结果比较
        uint32_t hw_result[4] = {0};
        hw_result[0] = top->io_posit_o_0;
        hw_result[1] = top->io_posit_o_1;
        hw_result[2] = top->io_posit_o_2;
        hw_result[3] = top->io_posit_o_3;

        for(int j = 0; j < 4; j++){
            if (!posit_equal(hw_result[j], golden[j])) {
            std::cerr << "样本 " << i << " 不匹配\n"
                      << "  硬件: 0x" << std::hex <<hw_result[0] << ", " << hw_result[1] << ", " << hw_result[2] << "," << hw_result[3] << "\n"
                      << "  预期: 0x" << std::hex << golden[0] << ", " <<  golden[1] << ", " <<  golden[2] << "," <<  golden[3] << "\n"
                      << "  激活数据: " << std::hex << act[0] << ", " << act[1] << ", " << act[2] << "," << act[3] << "\n"
                      << "  权重数据: " << std::hex << weight[0] << ", " << weight[1] << ", " << weight[2] << "," << weight[3] << "\n";
            errors++;
        }
        }
       

        // 进度显示
        if ((i+1) % 100 == 0) {
            std::cout << "已测试 " << (i+1) << "/" << total 
                      << " (" << ((i+1)*100/total) << "%)" 
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
              << "\nposit_mul测试完成！"
              << "\n总样本数: " << total
              << "\n错误数量: " << errors
              << "\n错误率:   " << std::fixed << std::setprecision(2)
              << (errors*100.0/total) << "%\n";

    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

#endif