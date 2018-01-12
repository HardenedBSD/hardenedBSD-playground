//===- IndirectBrExpandPass.cpp - Expand indirectbr to switch -------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Implements an expansion pass to turn `indirectbr` instructions in the IR
/// into `switch` instructions. This works by enumerating the basic blocks in
/// a dense range of integers, replacing each `blockaddr` constant with the
/// corresponding integer constant, and then building a switch that maps from
/// the integers to the actual blocks.
///
/// While this is generically useful if a target is unable to codegen
/// `indirectbr` natively, it is primarily useful when there is some desire to
/// get the builtin non-jump-table lowering of a switch even when the input
/// source contained an explicit indirect branch construct.
///
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "indirectbr-expand"

namespace {

class IndirectBrExpandPass : public FunctionPass {
  const TargetLowering *TLI = nullptr;

public:
  static char ID; // Pass identification, replacement for typeid

  IndirectBrExpandPass() : FunctionPass(ID) {
    initializeIndirectBrExpandPassPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override;
};

} // end anonymous namespace

char IndirectBrExpandPass::ID = 0;

INITIALIZE_PASS(IndirectBrExpandPass, DEBUG_TYPE,
                "Expand indirectbr instructions", false, false)

FunctionPass *llvm::createIndirectBrExpandPass() {
  return new IndirectBrExpandPass();
}

bool IndirectBrExpandPass::runOnFunction(Function &F) {
  auto &DL = F.getParent()->getDataLayout();
  auto *TPC = getAnalysisIfAvailable<TargetPassConfig>();
  if (!TPC)
    return false;

  auto &TM = TPC->getTM<TargetMachine>();
  auto &STI = *TM.getSubtargetImpl(F);
  if (!STI.enableIndirectBrExpand())
    return false;
  TLI = STI.getTargetLowering();

  SmallVector<IndirectBrInst *, 1> IndirectBrs;

  // Build a list of indirectbrs that we want to rewrite.
  for (Instruction &I : instructions(F))
    if (auto *IBr = dyn_cast<IndirectBrInst>(&I))
      IndirectBrs.push_back(IBr);

  if (IndirectBrs.empty())
    return false;

  // If we need to replace any indirectbrs we need to establish integer
  // constants that will correspond to each of the basic blocks in the function
  // whose address escapes. We do that here and rewrite all the blockaddress
  // constants to just be those integer constants cast to a pointer type.
  SmallVector<BasicBlock *, 4> BBs;
  SmallDenseMap<BasicBlock *, int, 4> BBToIndex;
  // The null pointer is a special "zero-like" value that we get to compare with
  // block addresses so arrange for us to never use a zero-index for any basic
  // blocks.
  BBs.push_back(nullptr);

  for (BasicBlock &BB : F) {
    auto IsBlockAddressUse = [&](const Use &U) {
      return isa<BlockAddress>(U.getUser());
    };
    auto BlockAddressUseIt = llvm::find_if(BB.uses(), IsBlockAddressUse);
    if (BlockAddressUseIt == BB.use_end())
      continue;

    assert(std::find_if(std::next(BlockAddressUseIt), BB.use_end(),
                        IsBlockAddressUse) == BB.use_end() &&
           "There should only ever be a single blockaddress use because it is "
           "a constant and should be uniqued.");

    auto *BA = cast<BlockAddress>(BlockAddressUseIt->getUser());

    // Skip if the constant was formed but ended up not being used (due to DCE
    // or whatever).
    if (!BA->isConstantUsed())
      continue;

    // Compute the index we want to use for this basic block.
    int BBIndex = BBs.size();
    BBToIndex.insert({&BB, BBIndex});
    BBs.push_back(&BB);

    auto *ITy = cast<IntegerType>(DL.getIntPtrType(BA->getType()));
    ConstantInt *BBIndexC = ConstantInt::get(ITy, BBIndex);

    // Now rewrite the blockaddress to an integer constant based on the index.
    // FIXME: We could potentially preserve the uses as arguments to inline asm.
    // This would allow some uses such as diagnostic information in crashes to
    // have higher quality even when this transform is enabled, but would break
    // users that round-trip blockaddresses through inline assembly and then
    // back into an indirectbr.
    BA->replaceAllUsesWith(ConstantExpr::getIntToPtr(BBIndexC, BA->getType()));
  }
#ifndef NDEBUG
  for (const auto &BBIndexPair : BBToIndex)
    assert(BBs[BBIndexPair.second] == BBIndexPair.first &&
           "Mismatch between BB and index!");
#endif

  // Now rewrite each indirectbr to cast its loaded pointer to an integer and
  // switch on it using the integer map from above.
  for (auto *IBr : IndirectBrs) {
    // Handle the degenerate case of no successors by replacing the indirectbr
    // with unreachable as there is no successor available.
    if (IBr->getNumSuccessors() == 0) {
      (void)new UnreachableInst(F.getContext(), IBr);
      IBr->eraseFromParent();
      continue;
    }

    // First, cast the address back to an integer value.
    auto *ITy =
        cast<IntegerType>(DL.getIntPtrType(IBr->getAddress()->getType()));
    auto *V = CastInst::CreatePointerCast(
        IBr->getAddress(), ITy,
        Twine(IBr->getAddress()->getName()) + ".switch_cast", IBr);

    // Select a default successor for the switch. Either use the first successor
    // or the successor mapping to the first index (if one does).
    auto *DefaultSuccBB = IBr->getSuccessor(0);
    for (auto *SuccBB : IBr->successors()) {
      // Find the existing index for this basic block, or if we never computed
      // the basic block's address just reserve an entry for it.
      auto InsertResult = BBToIndex.insert({SuccBB, BBs.size()});
      if (InsertResult.second)
        BBs.push_back(SuccBB);

      int BBIndex = InsertResult.first->second;
      if (BBIndex == 1)
        DefaultSuccBB = SuccBB;
    }

    auto *SI =
        SwitchInst::Create(V, DefaultSuccBB, IBr->getNumSuccessors() - 1, IBr);
    for (auto *SuccBB : IBr->successors()) {
      if (SuccBB == DefaultSuccBB)
        // Nothing to do for the default successor, it is already set up.
        continue;

      // Lookup the index for this successor.
      auto BBToIndexIt = BBToIndex.find(SuccBB);
      assert(BBToIndexIt != BBToIndex.end() &&
             "Should have created an entry for all succesors!");
      auto *BBIndexC = ConstantInt::get(ITy, BBToIndexIt->second);

      // Add a case for this successor of the indirectbr.
      SI->addCase(BBIndexC, SuccBB);
    }

    // Now erase the indirectbr, leaving the switch as the new terminator.
    IBr->eraseFromParent();
  }
  return true;
}
