/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set ts=8 sts=2 et sw=2 tw=80:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "jit/AliasAnalysis.h"

#include <stdio.h>

#include "jit/Ion.h"
#include "jit/IonBuilder.h"
#include "jit/JitSpewer.h"
#include "jit/MIR.h"
#include "jit/MIRGraph.h"

#include "vm/Printer.h"

using namespace js;
using namespace js::jit;

namespace js {
namespace jit {

class LoopAliasInfo : public TempObject {
 private:
  LoopAliasInfo* outer_;
  MBasicBlock* loopHeader_;
  MInstructionVector invariantLoads_;

 public:
  LoopAliasInfo(TempAllocator& alloc, LoopAliasInfo* outer,
                MBasicBlock* loopHeader)
      : outer_(outer), loopHeader_(loopHeader), invariantLoads_(alloc) {}

  MBasicBlock* loopHeader() const { return loopHeader_; }
  LoopAliasInfo* outer() const { return outer_; }
  bool addInvariantLoad(MInstruction* ins) {
    return invariantLoads_.append(ins);
  }
  const MInstructionVector& invariantLoads() const { return invariantLoads_; }
  MInstruction* firstInstruction() const { return *loopHeader_->begin(); }
};

}  // namespace jit
}  // namespace js

void AliasAnalysis::spewDependencyList() {
#ifdef JS_JITSPEW
  if (JitSpewEnabled(JitSpew_AliasSummaries)) {
    Fprinter& print = JitSpewPrinter();
    JitSpewHeader(JitSpew_AliasSummaries);
    print.printf("Dependency list for other passes:\n");

    for (ReversePostorderIterator block(graph_.rpoBegin());
         block != graph_.rpoEnd(); block++) {
      for (MInstructionIterator def(block->begin()),
           end(block->begin(block->lastIns()));
           def != end; ++def) {
        if (!def->dependency()) {
          continue;
        }
        if (!def->getAliasSet().isLoad()) {
          continue;
        }

        JitSpewHeader(JitSpew_AliasSummaries);
        print.printf(" ");
        MDefinition::PrintOpcodeName(print, def->op());
        print.printf("%d marked depending on ", def->id());
        MDefinition::PrintOpcodeName(print, def->dependency()->op());
        print.printf("%d\n", def->dependency()->id());
      }
    }
  }
#endif
}

// Unwrap any slot or element to its corresponding object.
static inline const MDefinition* MaybeUnwrap(const MDefinition* object) {
  while (object->isSlots() || object->isElements() ||
         object->isConvertElementsToDoubles()) {
    MOZ_ASSERT(object->numOperands() == 1);
    object = object->getOperand(0);
  }

  if (object->isTypedArrayElements()) {
    return nullptr;
  }
  if (object->isTypedObjectElements()) {
    return nullptr;
  }
  if (object->isConstantElements()) {
    return nullptr;
  }

  return object;
}

// Get the object of any load/store. Returns nullptr if not tied to
// an object.
static inline const MDefinition* GetObject(const MDefinition* ins) {
  if (!ins->getAliasSet().isStore() && !ins->getAliasSet().isLoad()) {
    return nullptr;
  }

  // Note: only return the object if that object owns that property.
  // I.e. the property isn't on the prototype chain.
  const MDefinition* object = nullptr;
  switch (ins->op()) {
    case MDefinition::Opcode::InitializedLength:
    case MDefinition::Opcode::LoadElement:
    case MDefinition::Opcode::LoadUnboxedScalar:
    case MDefinition::Opcode::LoadUnboxedObjectOrNull:
    case MDefinition::Opcode::LoadUnboxedString:
    case MDefinition::Opcode::StoreElement:
    case MDefinition::Opcode::StoreUnboxedObjectOrNull:
    case MDefinition::Opcode::StoreUnboxedString:
    case MDefinition::Opcode::StoreUnboxedScalar:
    case MDefinition::Opcode::SetInitializedLength:
    case MDefinition::Opcode::ArrayLength:
    case MDefinition::Opcode::SetArrayLength:
    case MDefinition::Opcode::StoreElementHole:
    case MDefinition::Opcode::FallibleStoreElement:
    case MDefinition::Opcode::TypedObjectDescr:
    case MDefinition::Opcode::Slots:
    case MDefinition::Opcode::Elements:
    case MDefinition::Opcode::MaybeCopyElementsForWrite:
    case MDefinition::Opcode::MaybeToDoubleElement:
    case MDefinition::Opcode::TypedArrayLength:
    case MDefinition::Opcode::TypedArrayByteOffset:
    case MDefinition::Opcode::SetTypedObjectOffset:
    case MDefinition::Opcode::SetDisjointTypedElements:
    case MDefinition::Opcode::ArrayPopShift:
    case MDefinition::Opcode::ArrayPush:
    case MDefinition::Opcode::ArraySlice:
    case MDefinition::Opcode::LoadTypedArrayElementHole:
    case MDefinition::Opcode::StoreTypedArrayElementHole:
    case MDefinition::Opcode::LoadFixedSlot:
    case MDefinition::Opcode::LoadFixedSlotAndUnbox:
    case MDefinition::Opcode::StoreFixedSlot:
    case MDefinition::Opcode::GetPropertyPolymorphic:
    case MDefinition::Opcode::SetPropertyPolymorphic:
    case MDefinition::Opcode::GuardShape:
    case MDefinition::Opcode::GuardReceiverPolymorphic:
    case MDefinition::Opcode::GuardObjectGroup:
    case MDefinition::Opcode::GuardObjectIdentity:
    case MDefinition::Opcode::GuardUnboxedExpando:
    case MDefinition::Opcode::LoadUnboxedExpando:
    case MDefinition::Opcode::LoadSlot:
    case MDefinition::Opcode::StoreSlot:
    case MDefinition::Opcode::InArray:
    case MDefinition::Opcode::LoadElementHole:
    case MDefinition::Opcode::TypedArrayElements:
    case MDefinition::Opcode::TypedObjectElements:
    case MDefinition::Opcode::CopyLexicalEnvironmentObject:
    case MDefinition::Opcode::IsPackedArray:
      object = ins->getOperand(0);
      break;
    case MDefinition::Opcode::GetPropertyCache:
    case MDefinition::Opcode::GetDOMProperty:
    case MDefinition::Opcode::GetDOMMember:
    case MDefinition::Opcode::Call:
    case MDefinition::Opcode::Compare:
    case MDefinition::Opcode::GetArgumentsObjectArg:
    case MDefinition::Opcode::SetArgumentsObjectArg:
    case MDefinition::Opcode::GetFrameArgument:
    case MDefinition::Opcode::SetFrameArgument:
    case MDefinition::Opcode::CompareExchangeTypedArrayElement:
    case MDefinition::Opcode::AtomicExchangeTypedArrayElement:
    case MDefinition::Opcode::AtomicTypedArrayElementBinop:
    case MDefinition::Opcode::AsmJSLoadHeap:
    case MDefinition::Opcode::AsmJSStoreHeap:
    case MDefinition::Opcode::WasmLoadTls:
    case MDefinition::Opcode::WasmLoad:
    case MDefinition::Opcode::WasmStore:
    case MDefinition::Opcode::WasmCompareExchangeHeap:
    case MDefinition::Opcode::WasmAtomicBinopHeap:
    case MDefinition::Opcode::WasmAtomicExchangeHeap:
    case MDefinition::Opcode::WasmLoadGlobalVar:
    case MDefinition::Opcode::WasmLoadGlobalCell:
    case MDefinition::Opcode::WasmStoreGlobalVar:
    case MDefinition::Opcode::WasmStoreGlobalCell:
    case MDefinition::Opcode::WasmLoadRef:
    case MDefinition::Opcode::WasmStoreRef:
    case MDefinition::Opcode::ArrayJoin:
      return nullptr;
    default:
#ifdef DEBUG
      // Crash when the default aliasSet is overriden, but when not added in the
      // list above.
      if (!ins->getAliasSet().isStore() ||
          ins->getAliasSet().flags() != AliasSet::Flag::Any) {
        MOZ_CRASH(
            "Overridden getAliasSet without updating AliasAnalysis GetObject");
      }
#endif

      return nullptr;
  }

  MOZ_ASSERT(!ins->getAliasSet().isStore() ||
             ins->getAliasSet().flags() != AliasSet::Flag::Any);
  object = MaybeUnwrap(object);
  MOZ_ASSERT_IF(object, object->type() == MIRType::Object);
  return object;
}

// Generic comparing if a load aliases a store using TI information.
MDefinition::AliasType AliasAnalysis::genericMightAlias(
    const MDefinition* load, const MDefinition* store) {
  const MDefinition* loadObject = GetObject(load);
  const MDefinition* storeObject = GetObject(store);
  if (!loadObject || !storeObject) {
    return MDefinition::AliasType::MayAlias;
  }

  if (!loadObject->resultTypeSet() || !storeObject->resultTypeSet()) {
    return MDefinition::AliasType::MayAlias;
  }

  if (loadObject->resultTypeSet()->objectsIntersect(
          storeObject->resultTypeSet())) {
    return MDefinition::AliasType::MayAlias;
  }

  return MDefinition::AliasType::NoAlias;
}

// Whether there might be a path from src to dest, excluding loop backedges.
// This is approximate and really ought to depend on precomputed reachability
// information.
static inline bool BlockMightReach(MBasicBlock* src, MBasicBlock* dest) {
  while (src->id() <= dest->id()) {
    if (src == dest) {
      return true;
    }
    switch (src->numSuccessors()) {
      case 0:
        return false;
      case 1: {
        MBasicBlock* successor = src->getSuccessor(0);
        if (successor->id() <= src->id()) {
          return true;  // Don't iloop.
        }
        src = successor;
        break;
      }
      default:
        return true;
    }
  }
  return false;
}

static void IonSpewDependency(MInstruction* load, MInstruction* store,
                              const char* verb, const char* reason) {
#ifdef JS_JITSPEW
  if (!JitSpewEnabled(JitSpew_Alias)) {
    return;
  }

  Fprinter& out = JitSpewPrinter();
  out.printf("Load ");
  load->printName(out);
  out.printf(" %s on store ", verb);
  store->printName(out);
  out.printf(" (%s)\n", reason);
#endif
}

static void IonSpewAliasInfo(const char* pre, MInstruction* ins,
                             const char* post) {
#ifdef JS_JITSPEW
  if (!JitSpewEnabled(JitSpew_Alias)) {
    return;
  }

  Fprinter& out = JitSpewPrinter();
  out.printf("%s ", pre);
  ins->printName(out);
  out.printf(" %s\n", post);
#endif
}

// [SMDOC] IonMonkey Alias Analysis
//
// This pass annotates every load instruction with the last store instruction
// on which it depends. The algorithm is optimistic in that it ignores explicit
// dependencies and only considers loads and stores.
//
// Loads inside loops only have an implicit dependency on a store before the
// loop header if no instruction inside the loop body aliases it. To calculate
// this efficiently, we maintain a list of maybe-invariant loads and the
// combined alias set for all stores inside the loop. When we see the loop's
// backedge, this information is used to mark every load we wrongly assumed to
// be loop invariant as having an implicit dependency on the last instruction of
// the loop header, so that it's never moved before the loop header.
//
// The algorithm depends on the invariant that both control instructions and
// effectful instructions (stores) are never hoisted.
bool AliasAnalysis::analyze() {
  Vector<MInstructionVector, AliasSet::NumCategories, JitAllocPolicy> stores(
      alloc());

  // Initialize to the first instruction.
  MInstruction* firstIns = *graph_.entryBlock()->begin();
  for (unsigned i = 0; i < AliasSet::NumCategories; i++) {
    MInstructionVector defs(alloc());
    if (!defs.append(firstIns)) {
      return false;
    }
    if (!stores.append(std::move(defs))) {
      return false;
    }
  }

  // Type analysis may have inserted new instructions. Since this pass depends
  // on the instruction number ordering, all instructions are renumbered.
  uint32_t newId = 0;

  for (ReversePostorderIterator block(graph_.rpoBegin());
       block != graph_.rpoEnd(); block++) {
    if (mir->shouldCancel("Alias Analysis (main loop)")) {
      return false;
    }

    if (block->isLoopHeader()) {
      JitSpew(JitSpew_Alias, "Processing loop header %d", block->id());
      loop_ = new (alloc().fallible()) LoopAliasInfo(alloc(), loop_, *block);
      if (!loop_) {
        return false;
      }
    }

    for (MPhiIterator def(block->phisBegin()), end(block->phisEnd());
         def != end; ++def) {
      def->setId(newId++);
    }

    for (MInstructionIterator def(block->begin()),
         end(block->begin(block->lastIns()));
         def != end; ++def) {
      def->setId(newId++);

      AliasSet set = def->getAliasSet();
      if (set.isNone()) {
        continue;
      }

      // For the purposes of alias analysis, all recoverable operations
      // are treated as effect free as the memory represented by these
      // operations cannot be aliased by others.
      if (def->canRecoverOnBailout()) {
        continue;
      }

      if (set.isStore()) {
        for (AliasSetIterator iter(set); iter; iter++) {
          if (!stores[*iter].append(*def)) {
            return false;
          }
        }

#ifdef JS_JITSPEW
        if (JitSpewEnabled(JitSpew_Alias)) {
          Fprinter& out = JitSpewPrinter();
          out.printf("Processing store ");
          def->printName(out);
          out.printf(" (flags %x)\n", set.flags());
        }
#endif
      } else {
        // Find the most recent store on which this instruction depends.
        MInstruction* lastStore = firstIns;

        for (AliasSetIterator iter(set); iter; iter++) {
          MInstructionVector& aliasedStores = stores[*iter];
          for (int i = aliasedStores.length() - 1; i >= 0; i--) {
            MInstruction* store = aliasedStores[i];
            if (genericMightAlias(*def, store) !=
                    MDefinition::AliasType::NoAlias &&
                def->mightAlias(store) != MDefinition::AliasType::NoAlias &&
                BlockMightReach(store->block(), *block)) {
              if (lastStore->id() < store->id()) {
                lastStore = store;
              }
              break;
            }
          }
        }

        def->setDependency(lastStore);
        IonSpewDependency(*def, lastStore, "depends", "");

        // If the last store was before the current loop, we assume this load
        // is loop invariant. If a later instruction writes to the same
        // location, we will fix this at the end of the loop.
        if (loop_ && lastStore->id() < loop_->firstInstruction()->id()) {
          if (!loop_->addInvariantLoad(*def)) {
            return false;
          }
        }
      }
    }

    // Renumber the last instruction, as the analysis depends on this and the
    // order.
    block->lastIns()->setId(newId++);

    if (block->isLoopBackedge()) {
      MOZ_ASSERT(loop_->loopHeader() == block->loopHeaderOfBackedge());
      JitSpew(JitSpew_Alias, "Processing loop backedge %d (header %d)",
              block->id(), loop_->loopHeader()->id());
      LoopAliasInfo* outerLoop = loop_->outer();
      MInstruction* firstLoopIns = *loop_->loopHeader()->begin();

      const MInstructionVector& invariant = loop_->invariantLoads();

      for (unsigned i = 0; i < invariant.length(); i++) {
        MInstruction* ins = invariant[i];
        AliasSet set = ins->getAliasSet();
        MOZ_ASSERT(set.isLoad());

        bool hasAlias = false;
        for (AliasSetIterator iter(set); iter; iter++) {
          MInstructionVector& aliasedStores = stores[*iter];
          for (int i = aliasedStores.length() - 1;; i--) {
            MInstruction* store = aliasedStores[i];
            if (store->id() < firstLoopIns->id()) {
              break;
            }
            if (genericMightAlias(ins, store) !=
                    MDefinition::AliasType::NoAlias &&
                ins->mightAlias(store) != MDefinition::AliasType::NoAlias) {
              hasAlias = true;
              IonSpewDependency(ins, store, "aliases", "store in loop body");
              break;
            }
          }
          if (hasAlias) {
            break;
          }
        }

        if (hasAlias) {
          // This instruction depends on stores inside the loop body. Mark it as
          // having a dependency on the last instruction of the loop header. The
          // last instruction is a control instruction and these are never
          // hoisted.
          MControlInstruction* controlIns = loop_->loopHeader()->lastIns();
          IonSpewDependency(ins, controlIns, "depends",
                            "due to stores in loop body");
          ins->setDependency(controlIns);
        } else {
          IonSpewAliasInfo("Load", ins,
                           "does not depend on any stores in this loop");

          if (outerLoop &&
              ins->dependency()->id() < outerLoop->firstInstruction()->id()) {
            IonSpewAliasInfo("Load", ins, "may be invariant in outer loop");
            if (!outerLoop->addInvariantLoad(ins)) {
              return false;
            }
          }
        }
      }
      loop_ = loop_->outer();
    }
  }

  spewDependencyList();

  MOZ_ASSERT(loop_ == nullptr);
  return true;
}
