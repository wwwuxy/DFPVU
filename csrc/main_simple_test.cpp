#include"../config.h"
#ifdef CONFIG_SAMPLE_TEST

#include <verilated.h>
#include <verilated_vcd_c.h>
#include <iostream>
#include "VPvuTop.h"       // 由 Verilator 生成
#include "VPvuTop__Syms.h" // 由 Verilator 生成

#if 1
//VECTOR_SIZE = 4测试向量运算
//-----------------------------
// 检查输出是否正确
//-----------------------------
void check_outputs(VPvuTop* dut,
                   const uint32_t expected_o[4])
{
    bool pass = true;

    uint32_t actual_o[4] = {
        static_cast<uint32_t>(dut->io_posit_o_0),
        static_cast<uint32_t>(dut->io_posit_o_1),
        static_cast<uint32_t>(dut->io_posit_o_2),
        static_cast<uint32_t>(dut->io_posit_o_3)
    };

    for (int i = 0; i < 4; ++i) {
        if (actual_o[i] != expected_o[i]) {
            std::cerr << "Mismatch at io_posit_o_" << i << ": "
                      << "Expected 0x" << std::hex << expected_o[i]
                      << ", Got 0x" << actual_o[i] << std::dec << "\n";
            pass = false;
        }
    }

    if (pass) {
        std::cout << "Test Passed.\n";
    } else {
        std::cerr << "Test Failed.\n";
    }
}

void check_outputs_dot(VPvuTop* dut, uint32_t expected_o)
{
    bool pass = true;

    // 读取实际输出
    uint32_t actual_o     = static_cast<uint32_t>(dut->io_posit_dot_o);

    // 比对主输出
    if (actual_o != expected_o) {
        std::cerr << "Mismatch at io_posit_o_0: "
                  << "Expected 0x" << std::hex << expected_o
                  << ", Got 0x" << actual_o << std::dec << "\n";
        pass = false;
    }
    
    if (pass) {
        std::cout << "Test Passed.\n";
    } else {
        std::cerr << "Test Failed.\n";
    }
}


//-----------------------------
// 给输入赋值
//-----------------------------
void set_inputs(
    VPvuTop* dut,
    const uint32_t i1[4],
    const uint32_t i2[4],
    uint8_t op
) {
    //设置posit输入数据
    dut->io_posit_i1_0 = i1[0];
    dut->io_posit_i1_1 = i1[1];
    dut->io_posit_i1_2 = i1[2];
    dut->io_posit_i1_3 = i1[3];

    dut->io_posit_i2_0 = i2[0];
    dut->io_posit_i2_1 = i2[1];
    dut->io_posit_i2_2 = i2[2];
    dut->io_posit_i2_3 = i2[3];

    //设置float输入数据
    dut->io_float_i_0 = 0;
    dut->io_float_i_1 = 0;
    dut->io_float_i_2 = 0;
    dut->io_float_i_3 = 0;

    dut->io_float_i2_0 = 0;
    dut->io_float_i2_1 = 0;
    dut->io_float_i2_2 = 0;
    dut->io_float_i2_3 = 0;


    //设置信号量
    dut->io_op = op;
    dut->io_Isposit = true;
    dut->io_Outposit = true;
    dut->io_float_mode = 3;
    dut->io_float_posit = true;
    

    //设置数据位宽
    dut->io_src_posit_width = 32;
    dut->io_dst_posit_width = 32;
    dut->io_vector_size = 4;
}

//-----------------------------
// 时钟翻转并执行仿真步骤
//-----------------------------
void toggle_clock(VPvuTop* dut, VerilatedVcdC* tfp, vluint64_t &main_time) {
    // 拉高时钟
    dut->clock = 1;
    dut->eval();
    if (tfp) tfp->dump(main_time++);
    
    // 拉低时钟
    dut->clock = 0;
    dut->eval();
    if (tfp) tfp->dump(main_time++);
}

//-----------------------------
// 主函数
//-----------------------------
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // 创建顶层 DUT 模块
    VPvuTop *dut = new VPvuTop;
    
    // 波形追踪
    Verilated::traceEverOn(true);
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("pvu_top_wave.vcd");

    vluint64_t main_time = 0;

    // 复位
    dut->reset = 1;
    for (int i = 0; i < 5; ++i) {
        toggle_clock(dut, tfp, main_time);
    }
    dut->reset = 0;

    //--------------------------------------------------
    // 测试用例1: 加法示例
    //--------------------------------------------------
    // {
    //     printf("Test Add\n");
    //     uint32_t test_i1[4] = {0x5A000000, 0x5A000000, 0x48000000, 0x60000000};
    //     uint32_t test_i2[4] = {0x48000000, 0x48000000, 0x50000000, 0x60000000};
    //     uint8_t  test_op    = 1; // 加法

    //     uint32_t expected_o[4]  = {0x5C000000, 0x5C000000, 0x54000000, 0x64000000}; 

    //     set_inputs(dut, test_i1, test_i2, test_op);
    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }
    //     check_outputs(dut, expected_o);
    // }

    //--------------------------------------------------
    // 测试用例2: 减法示例
    //--------------------------------------------------
    // {
    //     printf("Test Sub\n");
    //     uint32_t test_i1[4] = {0x5A000000, 0x5A000000, 0x5A000000, 0x48000000};
    //     uint32_t test_i2[4] = {0x48000000, 0x48000000, 0x48000000, 0x5A000000};
    //     uint8_t  test_op    = 2; // 加法

    //     uint32_t expected_o[4]  = {0x58000000, 0x58000000, 0x58000000, 0xA8000000}; 

    //     set_inputs(dut, test_i1, test_i2, test_op);
    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }
    //     check_outputs(dut, expected_o);
    // }


    // //--------------------------------------------------
    // // 测试用例3: 乘法示例
    // //--------------------------------------------------
    // {
    //     printf("Test Mul\n");
    //     uint32_t test_i1[4] = {0x40000000, 0x4c000000, 0x50000000, 0x54000000};
    //     uint32_t test_i2[4] = {0x48000000, 0x48000000, 0x48000000, 0x48000000};
    //     uint8_t  test_op    = 3; // 乘法

    //     uint32_t expected_o[4]  = {0x48000000, 0x54000000, 0x58000000, 0x5c000000};  

    //     set_inputs(dut, test_i1, test_i2, test_op);
    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }
    //     check_outputs(dut, expected_o);
    // }


    // //--------------------------------------------------
    // // 测试用例4: 除法示例
    // //--------------------------------------------------
//     {
//         printf("Test Div\n");
//         uint32_t test_i1[4] = {0x00000000, 0x00000000, 0x00000000, 0x00000000};
//         uint32_t test_i2[4] = {0x00000000, 0x00000000, 0x00000000, 0x00000000};
//         uint8_t  test_op    = 4; // 除法

//         uint32_t expected_o[4]  = {0x00000000, 0x00000000, 0x00000000, 0x00000000}; 
// 00     set_inputs(dut, test_i1, test_i2, test_op);
//         for (int cycle = 0; cycle < 2; ++cycle) {
//             toggle_clock(dut, tfp, main_time);
//         }
//         check_outputs(dut, expected_o);
//     }


    // //--------------------------------------------------
    // // 测试用例5: 点积示例
    // //--------------------------------------------------
    {
        printf("Test Dot\n");
        uint32_t test_i1[4] = {0x40000000, 0x4c000000, 0x50000000, 0x54000000};
        uint32_t test_i2[4] = {0x48000000, 0x48000000, 0x48000000, 0x48000000};
        uint8_t  test_op    = 5; // 点积

        uint32_t expected_o  = 0x63000000; 

        set_inputs(dut, test_i1, test_i2, test_op);
        for (int cycle = 0; cycle < 2; ++cycle) {
            toggle_clock(dut, tfp, main_time);
        }
        check_outputs_dot(dut, expected_o);
    }


    // 收尾
    tfp->close();
    delete tfp;
    delete dut;
    return 0;
}

#else
//每个数组只有 一个 元素，便于波形调试

//----------------------------------------------------------------------
// 1) 检查输出：仅对单一 io_posit_o_0 以及 io_posit_dot_o 进行比较
//----------------------------------------------------------------------
void check_outputs(VPvuTop* dut, uint32_t expected_o)
{
    bool pass = true;

    // 读取实际输出
    uint32_t actual_o     = static_cast<uint32_t>(dut->io_posit_o_0);

    // 比对主输出
    if (actual_o != expected_o) {
        std::cerr << "Mismatch at io_posit_o_0: "
                  << "Expected 0x" << std::hex << expected_o
                  << ", Got 0x" << actual_o << std::dec << "\n";
        pass = false;
    }

    if (pass) {
        std::cout << "Test Passed.\n";
    } else {
        std::cerr << "Test Failed.\n";
    }
}

void check_outputs_dot(VPvuTop* dut, uint32_t expected_o)
{
    bool pass = true;

    // 读取实际输出
    uint32_t actual_o     = static_cast<uint32_t>(dut->io_posit_dot_o);

    // 比对主输出
    if (actual_o != expected_o) {
        std::cerr << "Mismatch at io_posit_o_0: "
                  << "Expected 0x" << std::hex << expected_o
                  << ", Got 0x" << actual_o << std::dec << "\n";
        pass = false;
    }
    
    if (pass) {
        std::cout << "Test Passed.\n";
    } else {
        std::cerr << "Test Failed.\n";
    }
}

//----------------------------------------------------------------------
// 2) 设置输入：只给单一的 i1, i2, 和一个 op
//----------------------------------------------------------------------
void set_inputs(VPvuTop* dut, uint32_t i1, uint32_t i2, uint8_t op)
{
    dut->io_posit_i1_0 = i1;  // 只设置 i1_0
    dut->io_posit_i2_0 = i2;  // 只设置 i2_0

    //设置信号量
    dut->io_op = op;
    dut->io_Isposit = true;
    dut->io_Outposit = true;
    dut->io_float_mode = 3;
    dut->io_float_posit = true;
        
    
    //设置数据位宽
    dut->io_src_posit_width = 32;
    dut->io_dst_posit_width = 32;
    dut->io_vector_size = 1;
}

//----------------------------------------------------------------------
// 3) 翻转时钟
//----------------------------------------------------------------------
void toggle_clock(VPvuTop* dut, VerilatedVcdC* tfp, vluint64_t &main_time) {
    // 拉高时钟
    dut->clock = 1;
    dut->eval();
    if (tfp) tfp->dump(main_time++);

    // 拉低时钟
    dut->clock = 0;
    dut->eval();
    if (tfp) tfp->dump(main_time++);
}

//----------------------------------------------------------------------
// 4) 主测试程序
//----------------------------------------------------------------------
int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);

    // 创建顶层 DUT
    VPvuTop* dut = new VPvuTop;

    // 如果需要波形
    Verilated::traceEverOn(true);
    VerilatedVcdC* tfp = new VerilatedVcdC;
    dut->trace(tfp, 99);
    tfp->open("pvu_top_wave.vcd");

    vluint64_t main_time = 0;

    // ----------------- 复位 -----------------
    dut->reset = 1;
    for (int i = 0; i < 5; ++i) {
        toggle_clock(dut, tfp, main_time);
    }
    dut->reset = 0;

    //===============================================================
    // 测试用例1:测试加法
    // ===============================================================
    // {
    //     printf("Test Add\n");
    //     uint32_t i1  = 0x48000000;
    //     uint32_t i2  = 0x50000000;
    //     uint8_t  op  = 1; // 假设 0 代表加法

    //     uint32_t expected_o = 0x54000000; 

    //     // 写入输入
    //     set_inputs(dut, i1, i2, op);

    //     // 等待两拍时钟
    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }

    //     // 比对输出
    //     check_outputs(dut, expected_o);
    // }


    //===============================================================
    // 测试用例2: 测试减法
    //===============================================================
    // {
    //     printf("Test Sub\n");
    //     uint32_t i1  = 0x5A000000;
    //     uint32_t i2  = 0x48000000;
    //     uint8_t  op  = 2; // 假设 0 代表加法

    //     // 期望输出：2.0 => 0x6000(?), dot => 0x6000(若 dot=同样结果?)
    //     // 如果 dot 是某些累加结果，需要根据设计改
    //     uint32_t expected_o = 0x58000000; 

    //     // 写入输入
    //     set_inputs(dut, i1, i2, op);

    //     // 等待两拍时钟
    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }

    //     // 比对输出
    //     check_outputs(dut, expected_o);
    // }

    //===============================================================
    // 测试用例3: 测试乘法
    //===============================================================
    // {
    //     printf("Test Mul\n");
    //     uint32_t i1  = 0x4c000000; 
    //     uint32_t i2  = 0x54000000; 
    //     uint8_t  op  = 3;      // 乘法

    //     uint32_t expected_o = 0x60800000;  

    //     set_inputs(dut, i1, i2, op);

    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }

    //     check_outputs(dut, expected_o);
    // }

    //===============================================================
    // 测试用例4: 测试除法
    //===============================================================
    // {
    //     printf("Test Div\n");
    //     uint32_t i1  = 0x54000000; 
    //     uint32_t i2  = 0x48000000; 
    //     uint8_t  op  = 4;      // 除法

    //     uint32_t expected_o = 0x4C000000;  

    //     set_inputs(dut, i1, i2, op);

    //     for (int cycle = 0; cycle < 2; ++cycle) {
    //         toggle_clock(dut, tfp, main_time);
    //     }

    //     check_outputs(dut, expected_o);
    // }


    // 仿真结束收尾
    tfp->close();
    delete tfp;
    delete dut;
    return 0;
}

#endif

#endif