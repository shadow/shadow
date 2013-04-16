/*
 * The Shadow Simulator
 *
 * Copyright (c) 2012 David Chisnall
 * Copyright (c) 2011-2013
 * To the extent that a federal employee is an author of a portion
 * of this software or a derivative work thereof, no copyright is
 * claimed by the United States Government, as represented by the
 * Secretary of the Navy ("GOVERNMENT") under Title 17, U.S. Code.
 * All Other Rights Reserved.
 *
 * Permission to use, copy, and modify this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * GOVERNMENT ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" CONDITION
 * AND DISCLAIMS ANY LIABILITY OF ANY KIND FOR ANY DAMAGES WHATSOEVER
 * RESULTING FROM THE USE OF THIS SOFTWARE.
 */

#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/GlobalVariable.h"
#include "llvm/GlobalValue.h"
#include "llvm/DataLayout.h"
#include "llvm/BasicBlock.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/DebugLoc.h"

#ifdef DEBUG
#include "llvm/Support/raw_ostream.h"
#endif

#include <string>

#include "llvm/Analysis/Verifier.h"

using namespace llvm;
using std::string;

namespace {
class HoistGlobalsPass: public ModulePass {

private:
	// require DataLayout so we can get variable sizes
	void getAnalysisUsage(AnalysisUsage &AU) const {
		AU.addRequired<DataLayout>();
	}

public:
	static char ID;
	HoistGlobalsPass() : ModulePass(ID) {}

	bool runOnModule(Module &M) {
		bool modified = false;

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

				// We found a global variable, keep track of it
				modified = true;

				GlobalTypes.push_back(cast<PointerType>(GV->getType())->getElementType());
				GlobalInitializers.push_back(GV->getInitializer());
				Globals.push_back(GV);
			}
		}

		// There is nothing to do if we have no globals
		if (!modified)
			return false;

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

		uint64_t rawsize = getAnalysis<DataLayout>().getTypeStoreSize(HoistedStructType);
		Constant *HoistedStructSize = ConstantInt::get(Int32Ty, rawsize, false);
		GlobalVariable *HoistedSize = new GlobalVariable(M, Int32Ty, true,
				GlobalValue::ExternalLinkage, HoistedStructSize,
				"__hoisted_globals_size", 0, GlobalVariable::NotThreadLocal, 0);

#ifdef DEBUG
		errs() << "Hoisting globals: ";
#endif

		// replace all accesses to the original variables with pointer indirection
		// this replaces uses with pointers into the global struct
		uint64_t Field = 0;
		for (GlobalVariable **i = Globals.begin(), **e = Globals.end(); i != e; ++i) {
			GlobalVariable *GV = *i;
			assert(GV);

			SmallVector<Value*, 2> GEPIndexes;
			GEPIndexes.push_back(ConstantInt::get(Int32Ty, 0));
			GEPIndexes.push_back(ConstantInt::get(Int32Ty, Field++));
//			ArrayRef<Value*>* a = new ArrayRef<Value*>(GEPIndexes);

			// we have to do this manually so we can preserve debug info
			Constant *GEP = ConstantExpr::getGetElementPtr(HoistedStruct, GEPIndexes, true);
			GV->replaceAllUsesWith(GEP);

//			for (Value::use_iterator ui = GV->use_begin(); ui != GV->use_end(); ++ui) {
//				if(Instruction *ins = dyn_cast < Instruction > (*ui)) {
//					if(User *u = dyn_cast < User > (*ui)) {
//						GetElementPtrInst *GEP = GetElementPtrInst::CreateInBounds(HoistedStruct, *a, "", ins);
//						u->replaceUsesOfWith(GV, GEP);
//					}
//				}
//			    // replace user with new user
//				// copy metadata, change storage location reference
//				// create load instruction to load the GEP before existing user
//				if(Instruction *ins = dyn_cast < Instruction > (*ui)) {
//					MDNode *N = ins->getMetadata(LLVMContext::MD_dbg);
//					GetElementPtrInst *GEP = GetElementPtrInst::CreateInBounds(HoistedStruct, *a, "", ins);
//					ins->eraseFromParent();
//					LoadInst *loadGEP = new LoadInst(GEP, "");
//					BasicBlock::iterator ii(ins);
//					ReplaceInstWithInst(ins->getParent()->getInstList(), ii, loadGEP);
//				}
//			}

#ifdef DEBUG
			errs() << GV->getName() << ", ";
#endif

			assert(GV->use_empty());
			GV->eraseFromParent();
		}

#ifdef DEBUG
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
		return true;
	}
};

char HoistGlobalsPass::ID = 0;
RegisterPass<HoistGlobalsPass> X("hoist-globals", "Turn globals into indirections via a single pointer");
} // namespace

ModulePass *createHoistGlobalsPass() {
	return new HoistGlobalsPass();
}
