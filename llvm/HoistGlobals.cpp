/**
 * The Shadow Simulator
 *
 * Copyright (c) 2012 David Chisnall
 *
 * This file is part of Shadow.
 *
 * Shadow is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Shadow is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Shadow.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/CallSite.h"

#include "llvm/Support/raw_ostream.h"

#include <string>

#include "llvm/Analysis/Verifier.h"

using namespace llvm;
using std::string;

namespace
{
  class HoistGlobalsPass : public ModulePass
  {
    public:
    static char ID;
    HoistGlobalsPass() : ModulePass(ID) {}

    virtual bool runOnModule(Module &M) {
      bool modified = false;
      SmallVector<Type*, 16> GlobalTypes;
      SmallVector<Constant*, 16> GlobalInitializers;
      SmallVector<GlobalVariable*, 16> Globals;

      errs() << "Running\n";

      for (Module::global_iterator i = M.global_begin(), e = M.global_end() ; i != e ; ++i) {
        // Skip anything that isn't a global variable
        GlobalVariable *GV = dyn_cast<GlobalVariable>(i);
        if (GV) {
          // If this is not a definition, skip it
          if (GV->isDeclaration()) continue;
          // If it's constant, it can be shared
          if (GV->isConstant()) continue;
          modified = true;
          GlobalTypes.push_back(cast<PointerType>(GV->getType())->getElementType());
          GlobalInitializers.push_back(GV->getInitializer());
          Globals.push_back(GV);
        }
      }

      if (!modified) return false;

      errs() << "Modified!\n";

      StructType *HoistedTy = StructType::create(GlobalTypes, "hoisted_globals");
      Constant *HoistedInit = ConstantStruct::get(HoistedTy, GlobalInitializers);
      // PrivateLinkage InternalLinkage ExternalLinkage
      GlobalVariable *Hoisted = new GlobalVariable(M, HoistedTy, false,
          GlobalValue::PrivateLinkage, HoistedInit, "__hoisted_globals");
      Type *Int32Ty = Type::getInt32Ty(M.getContext());
      uint64_t Field = 0;
      for (GlobalVariable **i = Globals.begin(), **e=Globals.end() ; i!=e ; ++i) {
        GlobalVariable *GV = *i;
        Constant *GEPIndexes[] = {
          ConstantInt::get(Int32Ty, 0),
          ConstantInt::get(Int32Ty, Field++) };

        errs() << "Replacing!\n";

        Constant *GEP = ConstantExpr::getGetElementPtr(Hoisted, GEPIndexes, true);
        GV->replaceAllUsesWith(GEP);
        assert(GV->use_empty());
        GV->eraseFromParent();
      }
#ifndef NDEBUG
      verifyModule(M);
#endif
      return true;
    }
  };

  char HoistGlobalsPass::ID = 0;
  RegisterPass<HoistGlobalsPass> X("hoist-globals",
          "Turn globals into indirections via a single pointer");
}

ModulePass *createHoistGlobalsPass(void)
{
  return new HoistGlobalsPass();
}
