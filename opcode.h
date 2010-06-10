#ifndef _OPCODE_H
#define _OPCODE_H

#include <string>
using namespace std;

struct INSTR {
        string opc;
        int decode_type;
};

extern struct INSTR instr[];

#endif

