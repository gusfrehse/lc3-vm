#include <iostream>
#include <cstdint>

uint16_t sign_extend(uint16_t x, int bit_count);
void update_flags(uint16_t r);


// 65536 locations
uint16_t memory[UINT16_MAX];

enum
{
    R_R0 = 0,
    R_R1,
    R_R2,
    R_R3,
    R_R4,
    R_R5,
    R_R6,
    R_R7,
    R_PC, // program counter
    R_COND,
    R_COUNT
};
uint16_t reg[R_COUNT];

enum
{
    OP_BR = 0,
    OP_ADD,
    OP_LD,
    OP_ST,
    OP_JSR,
    OP_AND,
    OP_LDR,
    OP_STR,
    OP_RTI,
    OP_NOT,
    OP_LDI,
    OP_STI,
    OP_JMP,
    OP_RES,
    OP_LEA,
    OP_TRAP
};

enum
{
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2,
};

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cout << "vm [image-file1] ..." << std::endl;
        exit(2);
    }

    for (int j = 1; j < argc; ++j)
    {
        if (!read_image(argv[j]))
        {
            std::cout << "failed to load image: %s\n", argv[j]);
        }
    }

    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = intr >> 12;

        switch(op)
        {
        case OP_ADD:
            // destination register
            uint16_t dr = (intr >> 9) & 0x7;
            // first argument
            uint16_t a = reg[(intr >> 6) & 0x7];
            // second argument
            uint16_t b;
            if (intr >> 5 & 0x1) /* is bit 5 set? */
            {
                // bit 5 is set
                b = sign_extend(instr & 0x1F, 5);
            }
            else
            {
                b = reg[instr & 0x7];
            }
            reg[dr] = a + b;
            update_flags(dr);
            break;
        case OP_AND:
            // destination register
            uint16_t dr = (intr >> 9) & 0x7;
            // first argument
            uint16_t a = reg[(intr >> 6) & 0x7];
            // second argument
            uint16_t b;
            if (intr >> 5 & 0x1) /* is bit 5 set? */
            {
                // bit 5 is set
                b = sign_extend(instr & 0x1F, 5);
            }
            else
            {
                b = reg[instr & 0x7];
            }
            reg[dr] = a & b;
            update_flags(dr);
            break;
        case OP_NOT:
            uint16_t dr = (instr >> 9) & 0x7;
            uint16_t sr = (instr >> 6) & 0x7;
            reg[dr] = ~reg[sr];
            update_flags(dr);
            break;
        case OP_BR:
            uint16_t cond_flag = instr >> 9 & 0x7;
            
            if (cond_flag & reg[R_COND])
            {
                reg[R_PC] += sign_extend(instr & 0x1FF, 9);
            }
            break;
        case OP_JMP:
            reg[R_PC] = reg[instr >> 6 & 0x7];
            break;
        case OP_JSR:
            reg[R_R7] = reg[R_PC];
            if (instr >> 11 & 1)
            {
                reg[R_PC] += sign_extend(instr & 0x7FF, 11);
            }
            else
            {
                reg[R_PC] = instr >> 6 & 0x7;
            }
            break;
        case OP_LD:
            uint16_t dr = instr >> 9 & 0x7;
            reg[dr] =
                mem_read(reg[R_PC] + sign_extend(instr & 0x1FF, 9));
            update_flags(dr);
            break;
        case OP_LDI:
            uint16_t dr = (instr >> 9) & 0x7;
            uint16 pc_offset = sign_extend(instr & 0x1FF, 9);
            reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset));
            update_flags(dr);
            break;
        case OP_LDR:
            uint16_t dr = instr >> 9 & 0x7;
            uint16_t sr = instr >> 6 & 0x7;
            uint16_t offset = sign_extend(instr & 0x3F, 6)
            reg[dr] = mem_read(reg[sr] + offset);
            update_flags(dr);
            break;
        case OP_LEA:
            uint16_t dr = instr >> 9 & 0x7;
            uint16_t offset = sign_extend(instr & 0x1FF, 9);
            reg[dr] = reg[R_PC] + offset;
            update_flags(dr);
            break;
        case OP_ST:
            uint16_t sr = instr >> 9 & 0x7;
            uint16_t offset = sign_extend(instr & 0x1FF, 9);
            mem_write(reg[R_PC] + offset, reg[sr]);
            break;
        case OP_STI:
            uint16_t sr = instr >> 9 & 0x7;
            uint16_t offset = sign_extend(instr & 0x1FF, 9);
            mem_write(mem_read(reg[R_PC] + offset, reg[sr]));
            break;
        case OP_STR:
            uint16_t sr = instr >> 9 & 0x7;
            uint16_t base = instr >> 6 & 0x7;
            uint16_t offset = sign_extend(instr & 0x3F, 6);
            mem_write(reg[base] + offset, reg[sr]);
            break;
        case OP_TRAP:
            break;
        case OP_RES:
            break;
        case OP_RTI:
            break;
        default:
            break;
        }            
    }
}

uint16_t sign_extend(uint16_t x, int bit_count)
{
    if ((x >> (bit_count - 1)) & 1) {
        x |= (0xFFFF << bit_count);
    }
    return x;
}

void update_flags(uint16_t r)
{
    if (reg[r] == 0)
    {
        reg[R_COND] = FL_ZRO;
    }
    else if (reg[r] >> 15)
    {
        reg[R_COND] = FL_NEG;
    }
    else
    {
        reg[R_COND] = FL_POS;
    }
}
