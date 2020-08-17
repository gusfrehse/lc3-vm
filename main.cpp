#include <iostream>
#include <fstream>
#include <cstdint>

#include <signal.h>
/* unix */
#include <unistd.h>
#include <fcntl.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>

uint16_t sign_extend(uint16_t x, int bit_count);
void update_flags(uint16_t r);
bool read_image(std::string path);
void read_image_file(std::ifstream &file);
uint16_t swap16(uint16_t x);
void mem_write(uint16_t address, uint16_t val);
uint16_t mem_read(uint16_t address);
uint16_t check_key();
void disable_input_buffering();
void restore_input_buffering();
void handle_interrupt(int signal);

// 65536 locations
uint16_t memory[UINT16_MAX];

// registers
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

// operators
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

// flags
enum
{
    FL_POS = 1 << 0,
    FL_ZRO = 1 << 1,
    FL_NEG = 1 << 2,
};

// trap operations
enum
{
    TRAP_GETC  = 0x20,
    TRAP_OUT   = 0x21,
    TRAP_PUTS  = 0x22,
    TRAP_IN    = 0x23,
    TRAP_PUTSP = 0x24,
    TRAP_HALT  = 0x25
};

// memory mapped registers
enum
{
    MR_KBSR = 0xFE00, // keyboard status
    MR_KBDR = 0xFE02 // keyboard data
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
            std::cout << "failed to load image: "
                      << argv[j] << std::endl;
        }
    }

    signal(SIGINT, handle_interrupt);
    disable_input_buffering();

    enum { PC_START = 0x3000 };
    reg[R_PC] = PC_START;

    int running = 1;
    while (running)
    {
        uint16_t instr = mem_read(reg[R_PC]++);
        uint16_t op = instr >> 12;

        switch(op)
        {
        case OP_ADD:
        {
            // destination register
            uint16_t dr = (instr >> 9) & 0x7;
            // first argument
            uint16_t a = reg[(instr >> 6) & 0x7];
            // second argument
            uint16_t b;
            if (instr >> 5 & 0x1) /* is bit 5 set? */
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
        }break;
        case OP_AND:
        {
            // destination register
            uint16_t dr = (instr >> 9) & 0x7;
            // first argument
            uint16_t a = reg[(instr >> 6) & 0x7];
            // second argument
            uint16_t b;
            if (instr >> 5 & 0x1) /* is bit 5 set? */
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
        }break;
        case OP_NOT:
        {
            uint16_t dr = (instr >> 9) & 0x7;
            uint16_t sr = (instr >> 6) & 0x7;
            reg[dr] = ~reg[sr];
            update_flags(dr);
        }break;
        case OP_BR:
        {
            uint16_t cond_flag = instr >> 9 & 0x7;
            
            if (cond_flag & reg[R_COND])
            {
                reg[R_PC] += sign_extend(instr & 0x1FF, 9);
            }
        }break;
        case OP_JMP:
        {
            reg[R_PC] = reg[instr >> 6 & 0x7];
        }break;
        case OP_JSR:
        {
            reg[R_R7] = reg[R_PC];
            if (instr >> 11 & 1)
            {
                reg[R_PC] += sign_extend(instr & 0x7FF, 11);
            }
            else
            {
                reg[R_PC] = instr >> 6 & 0x7;
            }
        }break;
        case OP_LD:
        {
            uint16_t dr = instr >> 9 & 0x7;
            reg[dr] =
                mem_read(reg[R_PC] + sign_extend(instr & 0x1FF, 9));
            update_flags(dr);
        }break;
        case OP_LDI:
        {
            uint16_t dr = (instr >> 9) & 0x7;
            uint16_t pc_offset = sign_extend(instr & 0x1FF, 9);
            reg[dr] = mem_read(mem_read(reg[R_PC] + pc_offset));
            update_flags(dr);
        }break;
        case OP_LDR:
        {
            uint16_t dr = instr >> 9 & 0x7;
            uint16_t sr = instr >> 6 & 0x7;
            uint16_t offset = sign_extend(instr & 0x3F, 6);
            reg[dr] = mem_read(reg[sr] + offset);
            update_flags(dr);
        }break;
        case OP_LEA:
        {
            uint16_t dr = instr >> 9 & 0x7;
            uint16_t offset = sign_extend(instr & 0x1FF, 9);
            reg[dr] = reg[R_PC] + offset;
            update_flags(dr);
        }break;
        case OP_ST:
        {
            uint16_t sr = instr >> 9 & 0x7;
            uint16_t offset = sign_extend(instr & 0x1FF, 9);
            mem_write(reg[R_PC] + offset, reg[sr]);
        }break;
        case OP_STI:
        {
            uint16_t sr = instr >> 9 & 0x7;
            uint16_t offset = sign_extend(instr & 0x1FF, 9);
            mem_write(mem_read(reg[R_PC] + offset), reg[sr]);
        }break;
        case OP_STR:
        {
            uint16_t sr = instr >> 9 & 0x7;
            uint16_t base = instr >> 6 & 0x7;
            uint16_t offset = sign_extend(instr & 0x3F, 6);
            mem_write(reg[base] + offset, reg[sr]);
        }break;
        case OP_TRAP:
        {
            switch (instr & 0xFF)
            {
            case TRAP_GETC:
            {
                char chr;
                std::cin >> chr;
                reg[R_R0] = (uint16_t) chr;
            }break;
            case TRAP_OUT:
            {
                char chr = reg[R_R0] & 0xFF;
                std::cout << chr;
            }break;
            case TRAP_PUTS:
            {
                uint16_t* c = memory + reg[R_R0];
                std::cout << (char*) c;
            }break;
            case TRAP_IN:
            {
                char chr;
                std::cout << "Enter a character: ";
                std::cin >> chr;
                std::cout << chr;
                reg[R_R0] = (uint16_t) chr;
            }break;
            case TRAP_PUTSP:
            {
                uint16_t* c = memory + reg[R_R0];
                char chr;
                while(*c)
                {
                    chr  = *c & 0xFF;
                    std::cout << (char) chr;
                    chr = *c >> 8;
                    if (chr) std::cout << (char) chr;
                    ++c;
                }
            }break;
            case TRAP_HALT:
            {
                std::cout << "HALT" << std::endl;
                running = 0;
            }break;
            }
        }break;
        case OP_RES:
        {}break;
        case OP_RTI:
        {}break;
        default:
        {}break;
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

bool read_image(std::string path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::cout << "VM: Could not open file " << path << std::endl;
        return false;
    }
    read_image_file(file);
    return true;
}

void read_image_file(std::ifstream &file)
{
    uint16_t origin;
    file.read((char *) &origin, sizeof(origin));
    origin = swap16(origin);

    uint16_t max_read = UINT16_MAX - origin;
    uint16_t* p = memory + origin;
    file.read((char *) p, max_read);
    size_t read = file.gcount();
    while (read-- > 0)
    {
        *p = swap16(*p);
        ++p;

    }
}

uint16_t swap16(uint16_t x)
{
    return (x << 8) | (x >> 8);
}

void mem_write(uint16_t address, uint16_t val)
{
    memory[address] = val;
}

uint16_t mem_read(uint16_t address)
{
    if (address == MR_KBSR)
    {
        if (check_key())
        {
            memory[MR_KBSR] = 1 << 15;
            memory[MR_KBDR] = getchar();
        }
        else
        {
            memory[MR_KBSR] = 0;
        }

    }
    return memory[address];
}

uint16_t check_key()
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

struct termios original_tio;

void disable_input_buffering()
{
    tcgetattr(STDIN_FILENO, &original_tio);
    struct termios new_tio = original_tio;
    new_tio.c_lflag &= ~ICANON & ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);
}

void restore_input_buffering()
{
    tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

void handle_interrupt(int signal)
{
    restore_input_buffering();
    printf("\n");
    exit(-2);
}
