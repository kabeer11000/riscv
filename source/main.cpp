#include <iostream>
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <fstream>

class RISCVCpu {
public:
    std::vector<uint32_t> reg;
    uint32_t pc;
    std::vector<uint8_t> mem;

    RISCVCpu(size_t memory_size = 1024 * 1024) : reg(32, 0), pc(0), mem(memory_size, 0) {}

    void load_program(const std::vector<uint8_t>& program, uint32_t address) {
        if (address + program.size() > mem.size()) {
            mem.resize(address + program.size());
        }
        std::copy(program.begin(), program.end(), mem.begin() + address);
        pc = address;
    }

    uint32_t read_mem(uint32_t addr, size_t size) {
        if (addr + size > mem.size()) {
            throw std::out_of_range("Memory access out of bounds");
        }
        
        uint32_t value = 0;
        for (size_t i = 0; i < size; ++i) {
            value |= mem[addr + i] << (i * 8);
        }
        return value;
    }

    void write_mem(uint32_t addr, uint32_t value, size_t size) {
        if (addr + size > mem.size()) {
            throw std::out_of_range("Memory access out of bounds");
        }
        
        for (size_t i = 0; i < size; ++i) {
            mem[addr + i] = (value >> (i * 8)) & 0xFF;
        }
    }

    struct Instruction {
        uint32_t opcode;
        uint32_t rd;
        uint32_t rs1;
        uint32_t rs2;
        uint32_t funct3;
        uint32_t funct7;
        int32_t imm;
    };

    Instruction decode(uint32_t instr) {
        Instruction ins{};
        ins.opcode = instr & 0x7F;
        ins.rd = (instr >> 7) & 0x1F;
        ins.funct3 = (instr >> 12) & 0x7;
        ins.rs1 = (instr >> 15) & 0x1F;
        ins.rs2 = (instr >> 20) & 0x1F;
        ins.funct7 = (instr >> 25) & 0x7F;

        switch (ins.opcode) {
            case 0x03: case 0x13: case 0x67: case 0x73: // I-type
                ins.imm = (int32_t)(instr) >> 20;
                break;
            case 0x23: { // S-type
                ins.imm = ((instr >> 7) & 0x1F) | ((instr >> 25) << 5);
                ins.imm = (int32_t)(ins.imm << 20) >> 20;
                break;
            }
            case 0x63: { // B-type
                ins.imm = ((instr >> 7) & 0x1E) | 
                         ((instr >> 20) & 0x7E0) |
                         ((instr << 4) & 0x800) |
                         ((instr >> 31) << 11);
                ins.imm = (int32_t)(ins.imm << 19) >> 19;
                break;
            }
            case 0x37: case 0x17: // U-type
                ins.imm = instr & 0xFFFFF000;
                break;
            case 0x6F: { // J-type
                ins.imm = ((instr >> 21) & 0x3FF) |
                          ((instr >> 10) & 0x400) |
                          (instr & 0x7F800000) |
                          ((instr >> 11) & 0x80000000);
                ins.imm = (int32_t)(ins.imm << 11) >> 11;
                break;
            }
            default:
                ins.imm = 0;
        }
        return ins;
    }

    void execute(const Instruction& ins) {
        uint32_t next_pc = pc + 4;
        uint32_t rs1_val = reg[ins.rs1];
        uint32_t rs2_val = reg[ins.rs2];
        uint32_t result = 0;
        bool branch_taken = false;

        switch (ins.opcode) {
            case 0x13: // OP-IMM
                switch (ins.funct3) {
                    case 0x0: result = rs1_val + ins.imm; break; // ADDI
                    case 0x1: result = rs1_val << (ins.imm & 0x1F); break; // SLLI
                    case 0x2: result = ((int32_t)rs1_val < ins.imm) ? 1 : 0; break; // SLTI
                    case 0x3: result = (rs1_val < (uint32_t)ins.imm) ? 1 : 0; break; // SLTIU
                    case 0x4: result = rs1_val ^ ins.imm; break; // XORI
                    case 0x5: 
                        if ((ins.imm >> 5) == 0) // SRLI
                            result = rs1_val >> (ins.imm & 0x1F);
                        else // SRAI
                            result = (int32_t)rs1_val >> (ins.imm & 0x1F);
                        break;
                    case 0x6: result = rs1_val | ins.imm; break; // ORI
                    case 0x7: result = rs1_val & ins.imm; break; // ANDI
                }
                if (ins.rd) reg[ins.rd] = result;
                break;

            case 0x33: // OP
                switch (ins.funct3) {
                    case 0x0: 
                        if (ins.funct7 == 0x20) result = rs1_val - rs2_val; // SUB
                        else result = rs1_val + rs2_val; // ADD
                        break;
                    case 0x1: result = rs1_val << (rs2_val & 0x1F); break; // SLL
                    case 0x2: result = ((int32_t)rs1_val < (int32_t)rs2_val) ? 1 : 0; break; // SLT
                    case 0x3: result = (rs1_val < rs2_val) ? 1 : 0; break; // SLTU
                    case 0x4: result = rs1_val ^ rs2_val; break; // XOR
                    case 0x5: 
                        if (ins.funct7 == 0x20) // SRA
                            result = (int32_t)rs1_val >> (rs2_val & 0x1F);
                        else // SRL
                            result = rs1_val >> (rs2_val & 0x1F);
                        break;
                    case 0x6: result = rs1_val | rs2_val; break; // OR
                    case 0x7: result = rs1_val & rs2_val; break; // AND
                }
                if (ins.rd) reg[ins.rd] = result;
                break;

            case 0x03: // LOAD
                switch (ins.funct3) {
                    case 0x0: result = (int32_t)(int8_t)read_mem(rs1_val + ins.imm, 1); break; // LB
                    case 0x1: result = (int32_t)(int16_t)read_mem(rs1_val + ins.imm, 2); break; // LH
                    case 0x2: result = read_mem(rs1_val + ins.imm, 4); break; // LW
                    case 0x4: result = read_mem(rs1_val + ins.imm, 1); break; // LBU
                    case 0x5: result = read_mem(rs1_val + ins.imm, 2); break; // LHU
                }
                if (ins.rd) reg[ins.rd] = result;
                break;

            case 0x23: // STORE
                switch (ins.funct3) {
                    case 0x0: write_mem(rs1_val + ins.imm, reg[ins.rs2], 1); break; // SB
                    case 0x1: write_mem(rs1_val + ins.imm, reg[ins.rs2], 2); break; // SH
                    case 0x2: write_mem(rs1_val + ins.imm, reg[ins.rs2], 4); break; // SW
                }
                break;

            case 0x63: // BRANCH
                switch (ins.funct3) {
                    case 0x0: branch_taken = (rs1_val == rs2_val); break; // BEQ
                    case 0x1: branch_taken = (rs1_val != rs2_val); break; // BNE
                    case 0x4: branch_taken = ((int32_t)rs1_val < (int32_t)rs2_val); break; // BLT
                    case 0x5: branch_taken = ((int32_t)rs1_val >= (int32_t)rs2_val); break; // BGE
                    case 0x6: branch_taken = (rs1_val < rs2_val); break; // BLTU
                    case 0x7: branch_taken = (rs1_val >= rs2_val); break; // BGEU
                }
                if (branch_taken) next_pc = pc + ins.imm;
                break;

            case 0x6F: // JAL
                if (ins.rd) reg[ins.rd] = pc + 4;
                next_pc = pc + ins.imm;
                break;

            case 0x67: // JALR
                if (ins.rd) reg[ins.rd] = pc + 4;
                next_pc = (rs1_val + ins.imm) & ~1;
                break;

            case 0x37: // LUI
                if (ins.rd) reg[ins.rd] = ins.imm;
                break;

            case 0x17: // AUIPC
                if (ins.rd) reg[ins.rd] = pc + ins.imm;
                break;

            case 0x73: // SYSTEM
                if (ins.funct3 == 0x0) {
                    if (ins.imm == 0x0) handle_ecall();
                    else if (ins.imm == 0x1) throw std::runtime_error("EBREAK encountered");
                }
                break;

            default:
                throw std::runtime_error("Unsupported instruction");
        }

        reg[0] = 0; // Ensure x0 remains zero
        pc = next_pc;
    }

    void handle_ecall() {
        switch (reg[17]) { // a7
            case 1: // exit
                exit(reg[10]); // a0
                break;
            case 4: // write
                for (uint32_t i = 0; i < reg[12]; ++i) { // a2 = count
                    std::cout << (char)read_mem(reg[11] + i, 1); // a1 = buffer
                }
                break;
            default:
                throw std::runtime_error("Unsupported syscall");
        }
    }

    void run() {
        try {
            while (true) {
                uint32_t instr = read_mem(pc, 4);
                execute(decode(instr));
            }
        } catch (const std::exception& e) {
            std::cerr << "\nEmulation stopped: " << e.what() << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <riscv_binary>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Could not open file: " << argv[1] << std::endl;
        return 1;
    }

    std::vector<uint8_t> program((std::istreambuf_iterator<char>(file)),
                                  std::istreambuf_iterator<char>());

    RISCVCpu cpu;
    cpu.load_program(program, 0x00000000);
    cpu.run();

    return 0;
}
