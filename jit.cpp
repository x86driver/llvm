//===-- examples/HowToUseJIT/HowToUseJIT.cpp - An example use of the JIT --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This small program provides an example of how to quickly build a small
//  module with two functions and execute it with the JIT.
//
// Goal:
//  The goal of this snippet is to create in the memory
//  the LLVM module consisting of two functions as follow: 
//
// int add1(int x) {
//   return x+1;
// }
//
// int foo() {
//   return add1(10);
// }
//
// then compile the module via JIT, then execute the `foo'
// function and return result to a driver, i.e. to a "host program".
//
// Some remarks and questions:
//
// - could we invoke some code using noname functions too?
//   e.g. evaluate "foo()+foo()" without fears to introduce
//   conflict of temporary function name with some real
//   existing function name?
//
//===----------------------------------------------------------------------===//

#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Instructions.h"
#include "llvm/ExecutionEngine/JIT.h"
#include "llvm/ExecutionEngine/Interpreter.h"
#include "llvm/ExecutionEngine/GenericValue.h"
#include "llvm/Target/TargetSelect.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/raw_ostream.h"
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <string>
#include <iostream>
#include "cpu.h"
#include "opcode.h"

using namespace llvm;
using namespace std;

struct INSTR instr[] = {
        {"add", 0},	// opcode: 0
        {"sub", 0},
        {"ldr", 1},
        {"str", 1},
};

map<unsigned char, INSTR> opcode;

void init_table()
{
	for (unsigned int i = 0; i < sizeof(instr)/sizeof(instr[0]); ++i) {
		opcode[i] = instr[i];
	}
}

/* For add,sub */
void parse0(unsigned char opc, unsigned char regdst,
	unsigned char regsrc1, unsigned char regsrc2)
{
	cout << "\t";

	map<unsigned char, INSTR>::iterator it = opcode.find(opc);
	if (it != opcode.end()) {
		cout << (it->second).opc << "\t";
	} else {
		cout << "undefined instruction" << endl;
		return;
	}

	cout << "r" << static_cast<int>(regdst) << ", r" <<
		static_cast<int>(regsrc1) << ", r" << static_cast<int>(regsrc2) << endl;
}

/* For ldr, str */
void parse1(unsigned char opc, unsigned char regdst,
        unsigned char addrL, unsigned char addrH)	/* little endian */
{
	cout << "\t";

	map<unsigned char, INSTR>::iterator it = opcode.find(opc);
	if (it != opcode.end()) {
		cout << (it->second).opc << "\t";
	} else {
		cout << "undefined instruction" << endl;
		return;
	}

	unsigned short addr = (addrH << 8) | addrL;
	cout << "r" << static_cast<int>(regdst) << ", 0x" << hex << addr << endl;
}

typedef void (*parse_func)(unsigned char, unsigned char, unsigned char, unsigned char);
parse_func parse[] = {
	parse0,
	parse1,
};

unsigned char buf[512];			// global 512 bytes buffer

void readfile(int argc, char **argv)
{
	if (argc != 2) {
		printf("Usage: %s [input file]\n", argv[0]);
		exit(1);
	}

	FILE *fp = fopen(argv[1], "rb");
	if (!fp) {
		printf("Can't open %s\n", argv[1]);
		perror("fopen");
		exit(1);
	}

	fseek(fp, 0, SEEK_END);
	int file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	int ret = fread((void*)&buf[0], sizeof(buf), 1, fp);
	if (ret != 0) {
		cout << ret << endl;
		perror("fread");
		fclose(fp);
		exit(1);
	}
	fclose(fp);

	file_size &= 0x0fc;	// Align 4 bytes

	init_table();

	for (int i = 0; i < file_size; i+=4) {
		map<unsigned char, INSTR>::iterator it = opcode.find(buf[i]);
		if (it != opcode.end()) {
			parse[(it->second).decode_type](buf[i], buf[i+1], buf[i+2], buf[i+3]);
		}
	}
}

int main(int argc, char **argv)
{
	readfile(argc, argv);

	InitializeNativeTarget();

	LLVMContext Context;

	// Create some module to put our function into it.
	Module *M = new Module("test", Context);

	// Create the add1 function entry and insert this entry into module M.  The
	// function will have a return type of "int" and take an argument of "int".
	// The '0' terminates the list of argument types.
	Function *Add1F =
	  cast<Function>(M->getOrInsertFunction("add1", Type::getInt32Ty(Context),
						Type::getInt32Ty(Context),
						(Type *)0));

	// Add a basic block to the function. As before, it automatically inserts
	// because of the last argument.
	BasicBlock *BB = BasicBlock::Create(Context, "EntryBlock", Add1F);

	// Get pointers to the constant `1'.
	Value *One = ConstantInt::get(Type::getInt32Ty(Context), 1);

	// Get pointers to the integer argument of the add1 function...
	assert(Add1F->arg_begin() != Add1F->arg_end()); // Make sure there's an arg
	Argument *ArgX = Add1F->arg_begin();  // Get the arg
	ArgX->setName("AnArg");            // Give it a nice symbolic name for fun.

	// Create the add instruction, inserting it into the end of BB.
	Instruction *Add = BinaryOperator::CreateAdd(One, ArgX, "addresult", BB);

	// Create the return instruction and add it to the basic block
	ReturnInst::Create(Context, Add, BB);

	// Now, function add1 is ready.


	// Now we going to create function `foo', which returns an int and takes no
	// arguments.
	Function *FooF =
	  cast<Function>(M->getOrInsertFunction("foo", Type::getInt32Ty(Context),
						(Type *)0));

	// Add a basic block to the FooF function.
	BB = BasicBlock::Create(Context, "EntryBlock", FooF);

	// Get pointers to the constant `10'.
	Value *Ten = ConstantInt::get(Type::getInt32Ty(Context), 10);

	// Pass Ten to the call call:
	CallInst *Add1CallRes = CallInst::Create(Add1F, Ten, "add1", BB);
	Add1CallRes->setTailCall(true);

	// Create the return instruction and add it to the basic block.
	ReturnInst::Create(Context, Add1CallRes, BB);

	// Now we create the JIT.
	ExecutionEngine* EE = EngineBuilder(M).create();

	outs() << "We just constructed this LLVM module:\n\n" << *M;
	outs() << "\n\nRunning foo: ";
	outs().flush();

	// Call the `foo' function with no arguments:
	std::vector<GenericValue> noargs;
	GenericValue gv = EE->runFunction(FooF, noargs);

	// Import result of execution:
	outs() << "Result: " << gv.IntVal << "\n";
	EE->freeMachineCodeForFunction(FooF);
	delete EE;
	llvm_shutdown();
	return 0;
}
