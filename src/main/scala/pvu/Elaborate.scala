// src/main/scala/pvu/Elaborate.scala
package pvu

import chisel3._
import chisel3.stage._
import circt.stage.ChiselStage
import chisel3.util._


object Elaborate extends App{
    var filltlflag = Array[String]()
    var firtoolOpts = Array[String]()

    filltlflag = filltlflag ++ Array(
        "--target-dir", "vsrc",
        "--target:systemverilog"
        )
    firtoolOpts = firtoolOpts ++ Array(
    "--preserve-aggregate=all",
    "--disable-all-randomization"
)

    ChiselStage.emitSystemVerilogFile(
        new PvuTop(
            MAX_POSIT_WIDTH = 32, 
            MAX_VECTOR_SIZE = 4, 
            MAX_ALIGN_WIDTH = 30, 
            ES = 2,
            FLOAT_MODE = 3
        ),
        filltlflag,
        firtoolOpts
    )
}