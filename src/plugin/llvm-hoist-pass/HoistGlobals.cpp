/*
 * The Shadow Simulator
 * See LICENSE for licensing information
 */

#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS
#endif
#ifndef __STDC_CONSTANT_MACROS
#define __STDC_CONSTANT_MACROS
#endif

#if ((__clang_major__ > 3) || (__clang_major__ == 3 && __clang_minor__ >= 9))
#ifdef DEBUG
#undef DEBUG
#endif
#endif

#include "llvm/Pass.h"
#if ((__clang_major__ < 3) || (__clang_major__ == 3 && __clang_minor__ < 3))
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/GlobalVariable.h"
#include "llvm/GlobalValue.h"
#include "llvm/DataLayout.h"
#include "llvm/BasicBlock.h"
#else
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/BasicBlock.h"
#endif
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#if ((__clang_major__ < 3) || (__clang_major__ == 3 && __clang_minor__ < 5))
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/DebugLoc.h"
#include "llvm/Analysis/Verifier.h"
#else
#include "llvm/IR/CallSite.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Verifier.h"
#endif

//#ifdef VERBOSE
#include "llvm/Support/raw_ostream.h"
//#endif

#include <string>

using namespace llvm;
using std::string;

#define HOIST_LOG_PREFIX "hoist-globals: "

static std::vector<Function*> parseGlobalCtors(GlobalVariable *GV) {
	if (GV->getInitializer()->isNullValue())
		return std::vector<Function *>();
	ConstantArray *CA = cast<ConstantArray>(GV->getInitializer());
	std::vector<Function *> Result;
	Result.reserve(CA->getNumOperands());
	for (User::op_iterator i = CA->op_begin(), e = CA->op_end(); i != e; ++i) {
		ConstantStruct *CS = cast<ConstantStruct>(*i);
		Result.push_back(dyn_cast<Function>(CS->getOperand(1)));
	}
	return Result;
}

static void replaceAllUsesWithKeepDebugInfo(GlobalVariable* From, Constant* To) {
    From->replaceAllUsesWith(To);
    return;
    //assert(!From->isConstant());
    //while(From->getNumUses() > 0) {
	    //User* u = From->use_back();

        // this cant be done on a constant, and we asserted that from is not a constant
        //u->replaceUsesOfWith(From, To);

        // metadata (gdb debug info) does not count as a use, so we have to update this manually
        //if(Instruction *ins = dyn_cast < Instruction > (u)) {
		    //MDNode *N = ins->getMetadata(LLVMContext::MD_dbg);
            // replace any debug info that references From to instead reference To
            // ??
        //}
    //}
}

namespace {
class HoistGlobalsPass: public ModulePass {

public:
	static char ID;
	HoistGlobalsPass() : ModulePass(ID) {}

	bool runOnModule(Module &M) {
	    DataLayout* DL = new DataLayout(&M);

		{
#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "HoistGlobals is running as an LLVM ModulePass plugin\n";
#endif

	        // _plugin_ctors needs to exist in order to call all of the
	        // constructors that need to get called at plugin load time
#ifdef VERBOSE
			errs() << HOIST_LOG_PREFIX << "Searching for constructor initializer function '_plugin_ctors'\n";
#endif
			Function *initFunc = M.getFunction("_plugin_ctors");
			if(initFunc) {
#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "Found '_plugin_ctors'!\n";
#endif
			} else {
#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "Did not find '_plugin_ctors', injecting it now\n";
#endif
                FunctionType *FT = FunctionType::get(Type::getVoidTy(M.getContext()), false);
                Constant* tmpfunc = M.getOrInsertFunction("_plugin_ctors", FT);
                initFunc = cast<Function>(tmpfunc);
                assert(initFunc);
			}
#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "Injecting global constructors 'llvm.global_ctors' into '_plugin_ctors': ";
#endif
			Function::BasicBlockListType &blocks = initFunc->getBasicBlockList();
			GlobalVariable *GV = M.getGlobalVariable("llvm.global_ctors");
			if (GV != NULL) {
				std::vector<Function*> ctors = parseGlobalCtors(GV);
				// Create a new basic block that calls all the global_ctors
				BasicBlock *block = BasicBlock::Create(M.getContext(), "call_global_ctors");
				for (std::vector<Function*>::iterator i = ctors.begin(), e = ctors.end(); i != e; ++i) {
					ArrayRef<Value*> args;
					Value* ctorv = *i;
#ifdef VERBOSE
            if(i+1 == e) {
                errs() << ctorv->getName();
            } else {
                errs() << ctorv->getName() << ", ";
            }
#endif

					CallInst *ins = CallInst::Create(ctorv, args, "", block);
				}
				//BranchInst *ret = BranchInst::Create(&blocks.front(), block);
				ReturnInst::Create(M.getContext(), block);
				blocks.push_front(block);
			}
#ifdef VERBOSE
            errs() << "\n";
#endif
		}

#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "Iterating existing global variables\n";
#endif

		// lists to store our globals, their types, and their initial values
		SmallVector<GlobalVariable*, 16> Globals;
		SmallVector<Type*, 16> GlobalTypes;
		SmallVector<Constant*, 16> GlobalInitializers;

		for (Module::global_iterator i = M.global_begin(), e = M.global_end(); i != e; ++i) {
			// Skip anything that isn't a global variable
			if (GlobalVariable *GV = dyn_cast<GlobalVariable>(i)) {
				// If this is not a definition, skip it
				if (GV->isDeclaration())
					continue;

				// If it's constant, it can be shared
				if (GV->isConstant())
					continue;

				GlobalTypes.push_back(cast<PointerType>(GV->getType())->getElementType());
				GlobalInitializers.push_back(GV->getInitializer());
				Globals.push_back(GV);
			}
		}

		// hack to make sure we have at least one global variable, so that the
		// hoisted_globals struct gets created as required by shadow
		if(Globals.empty()) {
#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "No globals exist, injecting one now to ensure a non-empty hoisted_globals struct\n";
#endif

		    PointerType* PointerTy_0 = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
		    GlobalVariable* hidden_gv = new GlobalVariable(M, PointerTy_0, false, GlobalValue::CommonLinkage, 0, "__hoisted_placeholder__");
		    ConstantPointerNull* const_ptr_2 = ConstantPointerNull::get(PointerTy_0);
		    hidden_gv->setInitializer(const_ptr_2);

            GlobalTypes.push_back(cast<PointerType>(hidden_gv->getType())->getElementType());
            GlobalInitializers.push_back(hidden_gv->getInitializer());
            Globals.push_back(hidden_gv);
		}

#ifdef VERBOSE
            errs() << HOIST_LOG_PREFIX << "Injecting new storage objects\n";
#endif

		// Now we need a new structure to store all of the globals we found
		// its type is a combination of the types of all of its elements
		// we will initialize each of the elements as done previously
		// linkage types:
		// GlobalValue::ExternalLinkage (extern)
		// GlobalValue::CommonLinkage (global, must have zero initializer)
		// GlobalValue::InternalLinkage (static)
		// GlobalValue::PrivateLinkage
		//
		// lets use thread-local storage so each thread gets its own copy
		// thread options:
		// GlobalVariable::NotThreadLocal
		// GlobalVariable::GeneralDynamicTLSModel
		// GlobalVariable::LocalDynamicTLSModel
		// GlobalVariable::InitialExecTLSModel
		// GlobalVariable::LocalExecTLSModel

		Type *Int32Ty = Type::getInt32Ty(M.getContext());

		StructType *HoistedStructType = StructType::create(GlobalTypes, "hoisted_globals");
		Constant *HoistedStructInitializer = ConstantStruct::get(HoistedStructType, GlobalInitializers);
		GlobalVariable *HoistedStruct = new GlobalVariable(M, HoistedStructType,
				false, GlobalValue::ExternalLinkage, HoistedStructInitializer,
				"__hoisted_globals", 0, GlobalVariable::NotThreadLocal, 0);

		// and we need the size of the struct so we know how much to copy in
		// and out for each node

		uint64_t rawsize = DL->getTypeAllocSize(HoistedStructType);
		Constant *HoistedStructSize = ConstantInt::get(Int32Ty, rawsize, false);
		GlobalVariable *HoistedSize = new GlobalVariable(M, Int32Ty, true,
				GlobalValue::ExternalLinkage, HoistedStructSize,
				"__hoisted_globals_size", 0, GlobalVariable::NotThreadLocal, 0);

#ifdef VERBOSE
		errs() << HOIST_LOG_PREFIX << "Hoisting globals: ";
#endif

		// replace all accesses to the original variables with pointer indirection
		// this replaces uses with pointers into the global struct
		uint64_t Field = 0;
		for (GlobalVariable **gv = Globals.begin(), **e = Globals.end(); gv != e; ++gv) {
			GlobalVariable *GV = *gv;
			assert(GV);

			SmallVector<Value*, 2> GEPIndexes;
			GEPIndexes.push_back(ConstantInt::get(Int32Ty, 0));
			GEPIndexes.push_back(ConstantInt::get(Int32Ty, Field++));

#if ((__clang_major__ > 3) || (__clang_major__ == 3 && __clang_minor__ > 6))
            Constant *GEP = ConstantExpr::getGetElementPtr(HoistedStruct->getValueType(), HoistedStruct, GEPIndexes, true);
#else
            Constant *GEP = ConstantExpr::getGetElementPtr(HoistedStruct, GEPIndexes, true);
#endif

			// we have to do this manually so we can preserve debug info
//			GV->replaceAllUsesWith(GEP);
			replaceAllUsesWithKeepDebugInfo(GV, GEP);

#ifdef VERBOSE
			if(gv+1 == e) {
			    errs() << GV->getName();
			} else {
                errs() << GV->getName() << ", ";
			}
#endif

			assert(GV->use_empty());
			GV->eraseFromParent();
		}

#ifdef VERBOSE
		errs() << "\n";
#endif

		// now create a new pointer variable that will be loaded and evaluated
		// before accessing the hoisted globals struct

		PointerType *HoistedPointerType = PointerType::get(HoistedStructType, 0);

		GlobalVariable *HoistedPointer = new GlobalVariable(M,
				HoistedPointerType, false, GlobalValue::ExternalLinkage,
				HoistedStruct, "__hoisted_globals_pointer", 0,
				GlobalVariable::NotThreadLocal, 0);

//      Constant *GEPIndexes[] = {ConstantInt::get(Int32Ty, 0), ConstantInt::get(Int32Ty, 0)};
//      Constant *GEP = ConstantExpr::getGetElementPtr(HoistedPointer, GEPIndexes, true);
//      Constant *NewPtr = ConstantExpr::getBitCast(HoistedPointer, HoistedStruct->getType());
//      HoistedStruct->replaceAllUsesWith(GEP);

#ifndef NDEBUG
		verifyModule(M);
#endif
        errs() << HOIST_LOG_PREFIX << "LLVM ModulePass is complete, hoisted " << Globals.size() << " variables\n";
		return true;
	}
};

char HoistGlobalsPass::ID = 0;
RegisterPass<HoistGlobalsPass> X("hoist-globals", "Turn globals into indirections via a single pointer");
} // namespace

ModulePass *createHoistGlobalsPass() {
	return new HoistGlobalsPass();
}
